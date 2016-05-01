#ifndef __TSH_H__
#define __TSH_H__

#define CMD_MAX_LEN 1024

typedef struct Command
{
    int arg_num;
    char ** args;
} Command;

typedef struct Command_handler
{
    int cmd_num;
    Command** cmds;
} Command_handler;

#endif
