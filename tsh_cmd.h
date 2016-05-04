#ifndef __TSH_CMD__
#define __TSH_CMD__

typedef struct TSH_command
{
    char* cmd_name;
    char* cmd_info;
    int (*cmd_func)(int, char*[]);
} TSH_command;

int tsh_help(int, char*[]);
int tsh_exit(int, char*[]);
int tsh_jobs(int, char*[]);
int tsh_fg(int, char*[]);
int tsh_bg(int, char*[]);

extern TSH_command tsh_cmds[]; 
extern int tsh_cmd_num;

#endif
