#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include "tsh.h"
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
    extern pid_t tsh_pid;
    kill(tsh_pid, SIGINT);
    return 0;
}

int tsh_unset(int argc, char* argv[])
{
    if (argc < 2 || !argv[1])
    {
        fprintf(stderr, "Usage: unset <ENV_VAR>\n");
        return 0;
    }

    unsetenv(argv[1]);
    return 0;
}

int tsh_export(int argc, char* argv[])
{
    if (argc < 3 || !argv[1] || !argv[2])
    {
        fprintf(stderr, "Usage: export <ENV_VAR> <VAL>\n");
        return 0;
    }

    setenv(argv[1], argv[2], 1);
    return 0;
}

int tsh_fg(int argc, char* argv[])
{
    if (argc < 2 || !argv[1] || (argv[1][0] != '%'))
    {
        fprintf(stderr, "Usage: fg %%<job>\n");
        return 0;
    }

    int jobID = atoi(&(argv[1][1]));
    if (jobID >= MAX_BG_JOB)
    {
        fprintf(stderr, "tsh: fg %%%d: no such job\n", jobID);
        return 0;
    }

    ProcessGroup* currGroup = backgroundGroup[jobID];
    if (currGroup == NULL)
        fprintf(stderr, "tsh: fg %%%d: no such job\n", jobID);
    else
    {
        int idxPID;
        for (idxPID = 0 ; idxPID < currGroup->proc_num ; idxPID ++)
        {
            int status = currGroup->status[idxPID];
            if ((currGroup->isRunning[idxPID] == 0) && WIFSTOPPED(currGroup->status[idxPID]))
            {
                currGroup->isRunning[idxPID] = 1;
                kill(currGroup->pids[idxPID], SIGCONT);
            }
            fprintf(stderr, "[%d]", jobID);
            fprintf(stderr, "\t%d\t", currGroup->pids[idxPID]);
            if (currGroup->isRunning[idxPID])
                fprintf(stderr, "running");
            else if (WIFEXITED(status))
                fprintf(stderr, "exited (%d)", WEXITSTATUS(status));
            else if (WIFSIGNALED(status))
                fprintf(stderr, "killed (%d)", WTERMSIG(status));
            else if (WIFSTOPPED(status))
                fprintf(stderr, "stopped (%d)", WSTOPSIG(status));
            fprintf(stderr, "\t\t%s\n", currGroup->cmdlines[idxPID]);
        }
        moveToForeground(currGroup);
        backgroundGroup[jobID] = NULL;
    }
    return 0;
}

int tsh_bg(int argc, char* argv[])
{
    if (argc < 2 || !argv[1] || (argv[1][0] != '%'))
    {
        fprintf(stderr, "Usage: bg %%<job>\n");
        return 0;
    }

    int jobID = atoi(&(argv[1][1]));
    if (jobID >= MAX_BG_JOB)
    {
        fprintf(stderr, "tsh: bg %%%d: no such job\n", jobID);
        return 0;
    }

    ProcessGroup* currGroup = backgroundGroup[jobID];
    if (currGroup == NULL)
        fprintf(stderr, "tsh: bg %%%d: no such job\n", jobID);
    else
    {
        int idxPID;
        for (idxPID = 0 ; idxPID < currGroup->proc_num ; idxPID ++)
        {
            if ((currGroup->isRunning[idxPID] == 0) && WIFSTOPPED(currGroup->status[idxPID]))
            {
                currGroup->isRunning[idxPID] = 1;
                fprintf(stderr, "[%d]\t%d\tcontinued\t\t%s\n", jobID, currGroup->pids[idxPID], currGroup->cmdlines[idxPID]);
                kill(currGroup->pids[idxPID], SIGCONT);
            }
        }
    }
    return 0;
}

int tsh_jobs(int argc, char* argv[])
{
    int idxPG;
    for (idxPG = 0 ; idxPG < MAX_BG_JOB ; idxPG ++)
    {
        ProcessGroup* currGroup = backgroundGroup[idxPG];
        if (currGroup)
        {
            int idxPID;
            int status;

            fprintf(stderr, "[%d]\n", idxPG);
            for (idxPID = 0 ; idxPID < currGroup->proc_num ; idxPID ++)
            {
                status = currGroup->status[idxPID];

                fprintf(stderr, "\t%d\t", currGroup->pids[idxPID]);
                if (currGroup->isRunning[idxPID])
                    printf("running");
                else if (WIFEXITED(status))
                    printf("exited (%d)", WEXITSTATUS(status));
                else if (WIFSIGNALED(status))
                    printf("killed (%d)", WTERMSIG(status));
                else if (WIFSTOPPED(status))
                    printf("stopped (%d)", WSTOPSIG(status));
                printf("\t\t%s\n", currGroup->cmdlines[idxPID]);
            }
        }
    }
    return 0;
}
