#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#include "config.h"
#include "siparse.h"
#include "utils.h"
#include "builtins.h"

sigset_t sigmask, sigoldmask;
volatile int nForeground = 0;
int nFinished = 0;
int process[MAX_FINISHED][2];
int foreground[MAX_FOREGROUND];

void handleError(char *s) {
    if(errno == ENOENT)
        fprintf(stderr, "%s: no such file or directory\n", s);
    else if(errno == EACCES)
        fprintf(stderr, "%s: permission denied\n", s);
    else
        fprintf(stderr, "%s: exec error\n", s);
}


bool containsEmptyCommand(pipelineseq* firstln) {
	pipelineseq* ln = firstln;
	do {
		commandseq* comseq = ln -> pipeline -> commands;
	    if(comseq != comseq -> next) {
	    	commandseq* firstcom = comseq;
		    do {
		    	if(comseq -> com == NULL)
		    		return true;
		        comseq = comseq -> next;
		    } while (comseq != firstcom);
		}
		ln = ln -> next;
	} while(ln != firstln);
    return false;
}

// extracts command arguments and stores them in argbuffer
void getArguments(command* com, char** argbuffer) {
	argbuffer[0] = com -> args -> arg;
	argseq *argsequence;
	int i;
	for(argsequence = com -> args -> next, i = 1; argsequence != com -> args; i++, argsequence = argsequence -> next) {
		argbuffer[i] = argsequence -> arg;
	}
	argbuffer[i] = NULL;
}

// returns true and executes command if command is a builtin
bool executeIfBuiltin(command* com, char** argbuffer) {
	getArguments(com, argbuffer);
	int i = 0;
	for (i = 0; builtins_table[i].name != NULL; i++) {
        if(!strcmp(com -> args -> arg, builtins_table[i].name)) {
            builtins_table[i].fun (argbuffer);
            return true;
        }
    }
    return false;
}

void closePipe(int fd[], int mode) {
    if(mode & 1 && fd[PIPE_READ] > 2) {
        close(fd[PIPE_READ]);
        fd[PIPE_READ] = STDIN_FILENO;
    }
    if(mode & 2 && fd[PIPE_WRITE] > 2) {
        close(fd[PIPE_WRITE]);
        fd[PIPE_WRITE] = STDOUT_FILENO;
    }
}

void printFinished() {
	sigprocmask(SIG_BLOCK, &sigmask, &sigoldmask);
    int i;
    for (i = 0; i < nFinished; i++) {
        printf("Background process %d terminated. ", process[i][0]);
        if (WIFSIGNALED(process[i][1])) {
            printf("(killed by signal %d)\n", WTERMSIG(process[i][1]));
        }
        else {
            printf("(exited with status %d)\n", WEXITSTATUS(process[i][1]));
        }
    }
    fflush(stdout);
    nFinished = 0;
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
}

void sigchldHandler() {
    int i, status, tmperrno = errno;
    bool isBackground = true;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        isBackground = true;
        for (i = 0; i < nForeground; i++) {
            if(pid == foreground[i]) {
                foreground[i] = foreground[--nForeground];
                isBackground = false;
                break;
            }
        }
        if(isBackground && nFinished < MAX_FINISHED) {
            process[nFinished][0] = pid;
            process[nFinished++][1] = status;
        }
    }
    errno = tmperrno;
}

int main(int argc, char *argv[])
{
	pipelineseq *ln, *firstln;
	commandseq *comseq, *firstcom;
	command *com;
	redirseq *redirsequence, *firstredir;
	redir *r;
	char buffer[BUFFER_SIZE];
	char *argbuffer[ARG_BUFFER_SIZE];
	char *l = buffer, *s = buffer, *strend;
	int left[2] = {STDIN_FILENO, STDOUT_FILENO}, right[2] = {STDIN_FILENO, STDOUT_FILENO};
	int i, v, status, isfirst;
	bool isBackground;
	struct sigaction sigintact, sigchldact;
	struct stat st;

    fstat(0, &st);

    sigintact.sa_handler = SIG_IGN;
    sigintact.sa_flags = 0;
    sigemptyset(&sigintact.sa_mask);
    sigaction(SIGINT, &sigintact, NULL);

    sigchldact.sa_handler = sigchldHandler;
    sigchldact.sa_flags = 0;
    sigemptyset(&sigchldact.sa_mask);
    sigaction(SIGCHLD, &sigchldact, NULL);

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGCHLD);

	while (1) { 
		if(S_ISCHR(st.st_mode)) { // prompt only when using character device
			if(nFinished)
				printFinished();
			write(STDOUT, PROMPT_STR, sizeof(PROMPT_STR));
		}
        if (l == strend) // check if previous line was read completely
            s = buffer;
        else if (l != buffer) {
            memcpy(buffer, l, s - l);
            s = buffer + (s - l);
		}
        l = buffer;
        v = read(0, s, MAX_LINE_LENGTH);
        if (v == 0) {
            break;
		}
		else {
			strend = s + v;
			while ((s = strchr(s, '\n')) != NULL) {
				*s = '\0';
				if(s - l > MAX_LINE_LENGTH) {
					fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
				}
				else {
					ln = parseline(l);
					if(ln == NULL) {
						fprintf(stderr, "parser error\n");
					}
					else if(ln -> pipeline -> commands -> com != NULL) {
						if (containsEmptyCommand(ln)) {
							fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
						}
						else {
							firstln = ln;
							do {
								comseq = firstcom = ln -> pipeline -> commands;
								if (!executeIfBuiltin(comseq -> com, argbuffer)) {
									isBackground = ln -> pipeline -> flags & INBACKGROUND;;
									do {
										com = comseq -> com;
										if(comseq -> next != firstcom) {
											pipe(right);
										}
									    int pid = fork();
										if(pid < 0) {
											fprintf(stderr, "fork error\n");
										}
										else if(pid == 0) {
											if (isBackground) {
								                    pid_t pid = setsid();
								            }
								            else {
								                sigintact.sa_handler = SIG_DFL;
								                sigemptyset(&sigintact.sa_mask);
								                sigaction(SIGINT, &sigintact, NULL);
            								}

											// pipes
											closePipe(left, CLOSE_WRITE);
								            closePipe(right, CLOSE_READ);

								            if(right[PIPE_WRITE] != STDOUT_FILENO) {
								                dup2(right[PIPE_WRITE], STDOUT_FILENO);
								                closePipe(right, CLOSE_WRITE);
								            }

								            if(left[PIPE_READ] != STDIN_FILENO) {
								                dup2(left[PIPE_READ], STDIN_FILENO);
								                closePipe(left, CLOSE_READ);
								            }

								            //redirections
								            int fd;
								            redirsequence = firstredir = com -> redirs;
								            if(redirsequence != NULL) {
									            do {
									                r = redirsequence -> r;
									                if(IS_RIN(r -> flags)) {
									                    fd = open(r -> filename, O_RDONLY);
									                    if (fd < 0) {
									                        handleError(r -> filename);
									                        exit(0);
									                    }
									                    dup2(fd, STDIN_FILENO);
									                    close(fd);
									                }
									                else {
									                    if(IS_RAPPEND(r -> flags)) {
									                        fd = open(r -> filename, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
									                    }
									                    else if (IS_ROUT(r -> flags)) {
									                        fd = open(r -> filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
									                    }
									                    if(fd < 0) {
								                        	handleError(r -> filename);
								                        	exit(0);
								                    	}
								                    	dup2(fd, STDOUT_FILENO);
								                    	close(fd);

									                }
									                redirsequence = redirsequence -> next;
									            } while(redirsequence != firstredir);
								        	}

								        	// execution
								            getArguments(com, argbuffer);
											execvp(com -> args -> arg, argbuffer);
											handleError(com -> args -> arg);
											exit(EXEC_FAILURE);
										}
										if(!isBackground)
        									foreground[nForeground++] = pid;
										closePipe(left, CLOSE_WRITE|CLOSE_READ);
							        	left[0] = right[0];
							        	left[1] = right[1];
							        	right[0] = STDIN_FILENO;
							        	right[1] = STDOUT_FILENO;
										comseq = comseq -> next;

									} while (comseq != firstcom);
									closePipe(left, CLOSE_WRITE|CLOSE_READ);
    								closePipe(right, CLOSE_WRITE|CLOSE_READ);
								}
								sigprocmask(SIG_BLOCK, &sigmask, &sigoldmask);
        						while (nForeground) {
            						sigsuspend(&sigoldmask);
        						}
        						sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
								ln = ln -> next;
							} while(firstln != ln);
						}
					}
				}
				s++;
				l = s;
			}
			s = strend;
		}
	}
	return 0;
}
