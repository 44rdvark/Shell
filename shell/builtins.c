#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>

#include "builtins.h"
#include "config.h"

int echo(char*[]);
int lexit(char*[]);
int lcd(char*[]);
int lls(char*[]);
int lkill(char*[]);
int undefined(char*[]);

builtin_pair builtins_table[]={
	{"exit",	&lexit},
	{"lecho",	&echo},
	{"lcd",		&lcd},
	{"lkill",	&lkill},
	{"lls",		&lls},
	{NULL,NULL}
};

void reportError(char *arr) {
    fprintf(stderr, "Builtin %s error.\n", arr);
}

int echo(char * argv[])
{
	int i =1;
	if (argv[i]) printf("%s", argv[i++]);
	while  (argv[i])
		printf(" %s", argv[i++]);

	printf("\n");
	fflush(stdout);
	return 0;
}

int lexit(char *argv[]) {
	char* test;
	int x;
    if(argv[2]) {
        reportError("exit");
        return BUILTIN_ERROR;
    }
    if(argv[1]) {
        x = strtol(argv[1], &test, 10);
        if(*test != '\0') {
        	reportError("exit");
        	return BUILTIN_ERROR;
        }
        exit(x);
    }
    exit(0);
}

int lkill(char *argv[]) {
    if (!argv[1] || argv[3]) {
        reportError("lkill");
        return BUILTIN_ERROR;
    }
    int x, y;
    char* test, *ttest;
    if (!argv[2]) {
        x = strtol(argv[1], &test, 10);
        if (*test != '\0') {
            reportError("lkill");
            return BUILTIN_ERROR;
        }
        return kill(x, SIGTERM);
    }
    x = strtol(argv[1] + 1, &test, 10);
    y = strtol(argv[2], &ttest, 10);
    if (*test != '\0' || *ttest != '\0') {
        reportError("lkill");
        return BUILTIN_ERROR;
    }
    return kill(y, x);
}

int lls(char* argv[]) {
    if(!argv[1]) {
        DIR *dir = opendir(".");
        struct dirent *file;
        while ((file = readdir(dir)) != NULL)
            if(file -> d_name[0] != '.')
                printf("%s\n", file -> d_name);
        fflush(stdout);
        if (dir != NULL)
        	closedir(dir);
        return 0;
    }
    else {
        reportError("lls");
        return BUILTIN_ERROR;
    }
}

int lcd(char * argv[]) {
    if(argv[2]) {
        reportError("lcd");
        return BUILTIN_ERROR;
    }
    char cwd[BUFFER_SIZE];
    if(argv[1]) {
        if(argv[1][0] != '/') {
                getcwd(cwd, sizeof(cwd));
                strcat(cwd, "/");
                strcat(cwd, argv[1]);
                if(chdir(cwd) == -1) {
                	fprintf(stderr, "%s\n", cwd);
                    reportError("lcd");
                    return BUILTIN_ERROR;
                }
        }
        else if(chdir(argv[1]) == -1) {
            reportError("lcd");
            return BUILTIN_ERROR;
        }
    }
    else if(chdir(getenv("HOME")) == -1) {
        reportError("lcd");
        return BUILTIN_ERROR;
    }
    return 0;
}

int 
undefined(char * argv[])
{
	fprintf(stderr, "Command %s undefined.\n", argv[0]);
	return BUILTIN_ERROR;
}
