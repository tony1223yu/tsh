#ifndef __TSH_H__
#define __TSH_H__

#define CMD_MAX_LEN 1024
#define MAX_BG_JOB 64

typedef struct Command
{
    int arg_num;
    char ** args;
    char *inputFile;
    char *outputFile;
    pid_t pid;

} Command;

typedef struct Command_handler
{
    int isBackGround;
    int cmd_num;
    Command** cmds;
} Command_handler;

typedef struct ProcessGroup
{
    pid_t pgid;
    int proc_num;
    int finish_num;
    int *status;
    pid_t *pids;

} ProcessGroup;

#endif
