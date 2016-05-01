#include <stdio.h>
#include <stdlib.h>
#include "tsh_cmd.h"

int tsh_help(int argc, char* argv[])
{
    int cmd_idx;
    for (cmd_idx = 0 ; cmd_idx < tsh_cmd_num ; cmd_idx ++)
    {
        printf("[ %s ]: %s\n", tsh_cmds[cmd_idx].cmd_name, tsh_cmds[cmd_idx].cmd_info);
    }
    return 0;
}

int tsh_exit(int argc, char* argv[])
{
    exit(0);
}
