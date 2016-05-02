#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include "tsh.h"
#include "tsh_cmd.h"

Command_handler* parse_cmd_hdr(char*);
Command* parse_cmd(char*);
void check_cmd(Command*);
void check_cmd_hdr(Command_handler*);
int findSystemCommand(char*);
int findTSHCommand(char*);
int processTSHCommand(Command*);
void signal_handler(int);

TSH_command tsh_cmds[] =
{
    { "help", "Display the list of supported command", tsh_help },
    { "exit", "Exit TSH", tsh_exit }
};
int tsh_cmd_num;

int tsh_pid;
ProcessGroup** backgroundGroup;

int main()
{
    char *pwd;
    int stdin_fd = dup(0);
    int stdout_fd = dup(1);

    tsh_cmd_num = sizeof(tsh_cmds) / sizeof(TSH_command);

    pwd = strdup(getenv("PWD"));

    backgroundGroup = (ProcessGroup**) malloc(sizeof(ProcessGroup*) * MAX_BG_JOB);
    memset(backgroundGroup, 0, sizeof(ProcessGroup*) * MAX_BG_JOB);

    // PID of tsh
    tsh_pid = getpid();

    // Infinite loop
    while (1)
    {
        char input[CMD_MAX_LEN];

        // Show the prompt
        fprintf(stdout, "tsh @ %s $ ", pwd);
        fflush(stdout);

        // Read the command
        if (fgets(input, CMD_MAX_LEN, stdin) != NULL)
        {
            Command_handler* cmd_hdr;
            int cmd_idx;
            int cur_pgid = -1;

            cmd_hdr = parse_cmd_hdr(input);
            check_cmd_hdr(cmd_hdr);

            for (cmd_idx = 0 ; cmd_idx < cmd_hdr->cmd_num ; cmd_idx ++)
            {
                pid_t child_pid;
                Command* curr_cmd = cmd_hdr->cmds[cmd_idx];
                check_cmd(curr_cmd);

                if ((child_pid = fork()) == -1)
                {
                    fprintf(stderr, "tsh: fork error.\n");
                    exit(1);
                }
                else if (child_pid == 0) // child
                {
                    // set pgid
                    if (cur_pgid == -1)
                        setpgid(0, 0);
                    else
                        setpgid(0, cur_pgid);

                    // Check for redirect
                    if (curr_cmd->inputFile != NULL)
                    {
                        // Close stdin fd
                        close(0);
                        open(curr_cmd->inputFile, O_RDONLY);
                    }
                    if (curr_cmd->outputFile != NULL)
                    {
                        // Close stdout fd
                        // New file would have -rw-rw-r-- permission
                        close(1);
                        open(curr_cmd->outputFile, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
                    }

                    // Execute the command
                    if (findTSHCommand(curr_cmd->args[0]))
                    {
                        processTSHCommand(curr_cmd);
                    }
                    else if (findSystemCommand(curr_cmd->args[0]))
                    {
                        if (execvp(curr_cmd->args[0], curr_cmd->args) == -1)
                        {
                            fprintf(stderr, "tsh: execvp error: %s, %d\n", curr_cmd->args[0], errno);
                        }
                    }
                    else
                    {
                        fprintf(stderr, "tsh: command not found: %s\n", curr_cmd->args[0]);
                    }

                    // Child process should exit here
                    exit(0);
                }
                else if (child_pid > 0) // parent process
                {
                    // wait until the child process set its pgid
                    while (getpgid(child_pid) == getpgrp());

                    // only set for first command
                    if (cur_pgid == -1)
                        cur_pgid = getpgid(child_pid);

                    curr_cmd->pid = child_pid;
                }
            }

            if (cmd_hdr->isBackGround == 1)
            {
                // Create ProcessGroup
                int idxPID;
                ProcessGroup* curProcGroup = (ProcessGroup*) malloc(sizeof(ProcessGroup));
                curProcGroup->pgid = cur_pgid;
                curProcGroup->proc_num = cmd_hdr->cmd_num;
                curProcGroup->finish_num = 0;
                curProcGroup->status = (int*) malloc(sizeof(int) * curProcGroup->proc_num);
                curProcGroup->pids = (pid_t*) malloc(sizeof(pid_t) * curProcGroup->proc_num);
                for (idxPID = 0 ; idxPID < curProcGroup->proc_num ; idxPID ++)
                    curProcGroup->pids[idxPID] = cmd_hdr->cmds[idxPID]->pid;

                // Insert into backgroundGroup
                int idxPG;
                for (idxPG = 0 ; idxPG < MAX_BG_JOB ; idxPG ++)
                {
                    if (backgroundGroup[idxPG] == NULL)
                    {
                        int idxPID;
                        backgroundGroup[idxPG] = curProcGroup;

                        fprintf(stderr, "[%d] start\n", idxPG);
                        fprintf(stderr, "\t");
                        for (idxPID = 0 ; idxPID < curProcGroup->proc_num ; idxPID ++)
                            fprintf(stderr, "%d ", curProcGroup->pids[idxPID]);
                        fprintf(stderr, "\n");

                        break;
                    }
                }
                if (idxPG == MAX_BG_JOB)
                {
                    fprintf(stderr, "tsh: Cannot create background process group\n");
                    exit(1);
                }
            }
            else
            {
                signal(SIGTTOU, SIG_IGN);
                tcsetpgrp(STDIN_FILENO, cur_pgid);
                signal(SIGTTOU, SIG_DFL);

                for (cmd_idx = 0 ; cmd_idx < cmd_hdr->cmd_num ; cmd_idx ++)
                    waitpid(cmd_hdr->cmds[cmd_idx]->pid, NULL);

                signal(SIGTTOU, SIG_IGN);
                tcsetpgrp(STDIN_FILENO, getpgrp());
                signal(SIGTTOU, SIG_DFL);
            }

            // Free the cmd_hdr
            for (cmd_idx = 0 ; cmd_idx < cmd_hdr->cmd_num ; cmd_idx ++)
            {
                Command* curr_cmd = cmd_hdr->cmds[cmd_idx];
                int arg_idx;

                for (arg_idx = 0 ; arg_idx < curr_cmd->arg_num ; arg_idx ++)
                    if (curr_cmd->args[arg_idx])
                        free (curr_cmd->args[arg_idx]);
                free (curr_cmd->args);

                if (curr_cmd->inputFile)
                    free (curr_cmd->inputFile);
                if (curr_cmd->outputFile)
                    free (curr_cmd->outputFile);

                free (curr_cmd);
            }
            free (cmd_hdr->cmds);
            free (cmd_hdr);
        }

        // Check for the exit status of background process
        pid_t pid;
        int status;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        {
            int idxPG;
            for (idxPG = 0 ; idxPG < MAX_BG_JOB ; idxPG ++)
            {
                ProcessGroup* currGroup = backgroundGroup[idxPG];
                if (currGroup)
                {
                    int idxPID;
                    for (idxPID = 0 ; idxPID < currGroup->proc_num ; idxPID ++)
                    {
                        if (currGroup->pids[idxPID] == pid)
                        {
                            currGroup->status[idxPID] = status;
                            break;
                        }
                    }

                    if (idxPID != currGroup->proc_num)
                    {
                        currGroup->finish_num ++;
                        if (currGroup->proc_num == currGroup->finish_num)
                        {
                            int idx;
                            fprintf(stderr, "[%d] finish\n", idxPG);
                            for (idx = 0 ; idx < currGroup->proc_num ; idx ++)
                            {
                                fprintf(stderr, "\t%d\tstatus: %d\n", currGroup->pids[idx], currGroup->status[idx]);
                            }

                            free (currGroup->pids);
                            free (currGroup->status);
                            free (currGroup);
                            backgroundGroup[idxPG] = NULL;
                        }
                    }
                }
            }
        }
    }

    free (backgroundGroup);
}

int findTSHCommand(char* cmd_name)
{
    int cmd_idx;
    for (cmd_idx = 0 ; cmd_idx < tsh_cmd_num ; cmd_idx ++)
    {
        if (strcmp(tsh_cmds[cmd_idx].cmd_name, cmd_name) == 0)
            return 1;
    }
    return 0;
}

int processTSHCommand(Command* cmd)
{
    char* cmd_name = cmd->args[0];
    int cmd_idx;
    for (cmd_idx = 0 ; cmd_idx < tsh_cmd_num ; cmd_idx ++)
    {
        if (strcmp(tsh_cmds[cmd_idx].cmd_name, cmd_name) == 0)
            return tsh_cmds[cmd_idx].cmd_func(cmd->arg_num, cmd->args);
    }

    // Should not be here since we would call findTSHCommand first.

    return 0;
}

int findSystemCommand(char* cmd_name)
{
    char *path = strdup(getenv("PATH"));
    if (path != NULL)
    {
        char *remainStr;
        char *subPath;

        subPath = strtok_r(path, ":", &remainStr);
        while (subPath != NULL)
        {
            DIR* dp;
            struct dirent* entry;

            if ((dp = opendir(subPath)) != NULL)
            {
                while((entry = readdir(dp)) != NULL)
                {
                    if (strcmp(entry->d_name, cmd_name) == 0)
                    {
                        closedir(dp);
                        free (path);
                        return 1;
                    }
                }
            }
            closedir(dp);
            subPath = strtok_r(remainStr, ":", &remainStr);
        }
    }
    free (path);

    return 0;
}

void check_cmd_hdr(Command_handler* cmd_hdr)
{
    int cmd_idx;
    for (cmd_idx = 0 ; cmd_idx < cmd_hdr->cmd_num ; cmd_idx ++)
    {
        Command* cmd = cmd_hdr->cmds[cmd_idx];
        int arg_idx;
        for (arg_idx = 0 ; arg_idx < cmd->arg_num ; arg_idx ++)
        {
            if ((cmd->args[arg_idx] != NULL) && (strcmp(cmd->args[arg_idx], "&") == 0))
            {
                free (cmd->args[arg_idx]);
                cmd->args[arg_idx] = NULL;
                if (cmd_idx != cmd_hdr->cmd_num - 1)
                    fprintf(stderr, "tsh: Unrecognized format.\n");
                else
                    cmd_hdr->isBackGround = 1;
            }
        }
    }
}

Command_handler* parse_cmd_hdr(char* input)
{
    char *subStr;
    char *remainStr;
    Command_handler* ret = (Command_handler*) malloc(sizeof(Command_handler));
    int cur_num = 1;

    ret->isBackGround = 0;
    ret->cmd_num = 0;
    ret->cmds = (Command**) malloc(sizeof(Command*) * cur_num);

    subStr = strtok_r(input, "|", &remainStr);
    while (subStr != NULL)
    {
        Command* tmp_cmd;
        if (ret->cmd_num == cur_num)
        {
            Command** tmp = (Command**) malloc(sizeof(Command*) * ret->cmd_num);
            memcpy (tmp, ret->cmds, ret->cmd_num * sizeof(Command*));

            free (ret->cmds);
            cur_num *= 2;

            ret->cmds = (Command**) malloc(sizeof(Command*) * cur_num);
            memcpy (ret->cmds, tmp, ret->cmd_num * sizeof(Command*));

            free (tmp);
        }

        if ((tmp_cmd = parse_cmd(subStr)) != NULL)
        {
            ret->cmds[ret->cmd_num] = tmp_cmd;
            ret->cmd_num ++;
        }

        subStr = strtok_r(remainStr, "|", &remainStr);
    }

    if (ret->cmd_num < cur_num)
    {
        Command** tmp = (Command**) malloc(sizeof(Command*) * ret->cmd_num);
        memcpy (tmp, ret->cmds, ret->cmd_num * sizeof(Command*));

        free (ret->cmds);
        cur_num = ret->cmd_num;

        ret->cmds = (Command**) malloc(sizeof(Command*) * cur_num);
        memcpy (ret->cmds, tmp, ret->cmd_num * sizeof(Command*));

        free (tmp);
    }

    return ret;
}

void check_cmd(Command* cmd)
{
    // Check for redirect
    int arg_idx;
    int min_idx = cmd->arg_num;
    for (arg_idx = 0 ; arg_idx < cmd->arg_num ; arg_idx ++)
    {
        if (cmd->args[arg_idx] == NULL)
            continue;
        else if (strcmp(cmd->args[arg_idx], ">") == 0)
        {
            free (cmd->args[arg_idx]);
            cmd->args[arg_idx] = NULL;
            cmd->outputFile = cmd->args[arg_idx + 1];
            cmd->args[arg_idx + 1] = NULL;
            arg_idx ++;
        }
        else if (strcmp(cmd->args[arg_idx], "<") == 0)
        {
            free (cmd->args[arg_idx]);
            cmd->args[arg_idx] = NULL;
            cmd->inputFile = cmd->args[arg_idx + 1];
            cmd->args[arg_idx + 1] = NULL;
            arg_idx ++;
        }
    }
}

Command* parse_cmd(char* input)
{
    char *subStr;
    char *remainStr;
    Command* ret = (Command*) malloc(sizeof(Command));
    int cur_num = 2;
    ret->inputFile = NULL;
    ret->outputFile = NULL;
    ret->arg_num = 0;
    ret->args = (char**) malloc(sizeof(char*) * cur_num);

    subStr = strtok_r(input, " \n", &remainStr);
    while (subStr != NULL)
    {
        // expand the size of the args array
        if ((ret->arg_num + 1) >= cur_num)
        {
            char** tmp = (char**) malloc(sizeof(char*) * ret->arg_num);
            memcpy (tmp, ret->args, ret->arg_num * sizeof(char*));

            free (ret->args);
            cur_num *= 2;

            ret->args = (char**) malloc(sizeof(char*) * cur_num);
            memcpy (ret->args, tmp, ret->arg_num * sizeof(char*));

            free (tmp);
        }

        ret->args[ret->arg_num] = strdup(subStr);
        ret->arg_num ++;

        subStr = strtok_r(remainStr, " \n", &remainStr);
    }

    if (ret->arg_num == 0)
    {
        free (ret->args);
        free (ret);
        return NULL;
    }

    ret->args[ret->arg_num] = NULL;
    ret->arg_num ++;

    if (ret->arg_num < cur_num)
    {
        char** tmp = (char**) malloc(sizeof(char*) * ret->arg_num);
        memcpy (tmp, ret->args, ret->arg_num * sizeof(char*));

        free (ret->args);
        cur_num = ret->arg_num;

        ret->args = (char**) malloc(sizeof(char*) * cur_num);
        memcpy (ret->args, tmp, ret->arg_num * sizeof(char*));

        free (tmp);
    }

    return ret;
}
