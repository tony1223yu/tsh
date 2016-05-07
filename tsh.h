#ifndef __TSH_H__
#define __TSH_H__

#include <wordexp.h>

#define CMD_MAX_LEN 1024
#define MAX_BG_JOB 64

typedef struct Command
{
    int arg_num;
    char ** args;
    char *inputFile;
    char *outputFile;
    pid_t pid;
    int isPath;

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
    int *isRunning;
    int *status; // store the status when isRunning = 0
    pid_t *pids;
    char** cmdlines;

} ProcessGroup;

extern ProcessGroup** backgroundGroup;
extern ProcessGroup* foregroundGroup;

void initTSH();
Command_handler* parse_cmd_hdr(char*);
Command* parse_cmd(char*);
void check_cmd(Command*);
void check_cmd_env(Command*);
void check_cmd_hdr(Command_handler*);
int findSystemCommand(char*);
int findTSHCommand(char*);
int processTSHCommand(Command*);
void moveToForeground(ProcessGroup*);
int setProcessGroupStatus(ProcessGroup**, int, pid_t, int, int*, int*);
void freeProcessGroup(ProcessGroup**, int);
char* getCommandName(Command*);
void insertIntoBackground(ProcessGroup*, int);
void signal_handler(int);
int checkCommandExpension(Command*, wordexp_t*);

#endif
