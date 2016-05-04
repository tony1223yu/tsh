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

TSH_command tsh_cmds[] =
{
    { "help", "Display the list of supported command", tsh_help },
    { "jobs", "Display the list of background process groups", tsh_jobs },
    { "fg",   "Move specific process groups to foreground", tsh_fg },
    { "bg",   "Move specific process groups to background", tsh_bg },
    { "exit", "Exit TSH", tsh_exit }
};
int tsh_cmd_num;

char *pwd;
int tsh_pid;
ProcessGroup** backgroundGroup;
ProcessGroup* foregroundGroup;
ProcessGroup* shellProcGroup;
int stdin_fd;
int stdout_fd;

void signal_handler(int signum)
{
    fprintf(stderr, "get signal!\n");
}

int main()
{
    //Initialize the global variables
    initTSH();

    // Clear the screen and print welcome message
    printf("\e[2J\e[H");
    printf("=========================================================\n");
    printf("|                                                       |\n");
    printf("|                Welcome to Tony's shell                |\n");
    printf("|                                                       |\n");
    printf("=========================================================\n");
    printf("1. Type \'help\' for supported command.\n");
    printf("\n");

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
            int prev_pipe[2] = {-1, -1};
            int curr_pipe[2] = {-1, -1};
            int num_system_cmd = 0;

            // Parse all the command
            cmd_hdr = parse_cmd_hdr(input);
            check_cmd_hdr(cmd_hdr);

            for (cmd_idx = 0 ; cmd_idx < cmd_hdr->cmd_num ; cmd_idx ++)
            {
                pid_t child_pid;
                Command* curr_cmd = cmd_hdr->cmds[cmd_idx];

                pipe(curr_pipe);

                if (findTSHCommand(curr_cmd->args[0]))
                {
                    // Check for pipe
                    if (cmd_idx != 0)
                    {
                        close(0);
                        dup2(prev_pipe[0], 0);
                        close(prev_pipe[0]);
                    }
                    if (cmd_idx != cmd_hdr->cmd_num-1)
                    {
                        close(1);
                        dup2(curr_pipe[1], 1);
                        close(curr_pipe[1]);
                    }
                    processTSHCommand(curr_cmd);

                    // Check for pipe
                    if (cmd_idx != 0)
                    {
                        close(0);
                        dup2(stdin_fd, 0);
                    }
                    if (cmd_idx != cmd_hdr->cmd_num-1)
                    {
                        close(1);
                        dup2(stdout_fd, 1);
                    }
                    prev_pipe[0] = curr_pipe[0];

                    /* build-in command */
                    curr_cmd->pid = -1;
                }
                else
                {
                    num_system_cmd ++;
                    if ((child_pid = fork()) == -1)
                    {
                        fprintf(stderr, "tsh: fork error.\n");
                        exit(1);
                    }
                    else if (child_pid == 0) // child
                    {
                        signal(SIGTTOU, signal_handler);
                        // set pgid
                        if (cur_pgid == -1)
                            setpgid(0, 0);
                        else
                            setpgid(0, cur_pgid);

                        // Check for pipe
                        if (cmd_idx != 0)
                        {
                            close(0);
                            dup2(prev_pipe[0], 0);
                            close(prev_pipe[0]);
                        }
                        if (cmd_idx != cmd_hdr->cmd_num-1)
                        {
                            close(1);
                            dup2(curr_pipe[1], 1);
                            close(curr_pipe[1]);
                        }

                        // Check for redirect
                        if (curr_cmd->inputFile != NULL)
                        {
                            close(0);
                            open(curr_cmd->inputFile, O_RDONLY);
                        }
                        if (curr_cmd->outputFile != NULL)
                        {
                            // New file would have -rw-rw-r-- permission
                            close(1);
                            open(curr_cmd->outputFile, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
                        }

                        // Execute the command
                        if (findSystemCommand(curr_cmd->args[0]))
                        {
                            check_cmd_env(curr_cmd);
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
                        // close pipe
                        if (cmd_idx != 0)
                        {
                            close(prev_pipe[0]);
                        }
                        if (cmd_idx != cmd_hdr->cmd_num-1)
                        {
                            close(curr_pipe[1]);
                        }
                        prev_pipe[0] = curr_pipe[0];

                        // wait until the child process set its pgid
                        while (getpgid(child_pid) == getpgrp());

                        // only set for first command
                        if (cur_pgid == -1)
                            cur_pgid = getpgid(child_pid);

                        curr_cmd->pid = child_pid;
                    }
                }
            }

            // Create ProcessGroup
            if (num_system_cmd != 0)
            {
                int idxPID;
                ProcessGroup* curProcGroup = (ProcessGroup*) malloc(sizeof(ProcessGroup));
                curProcGroup->pgid = cur_pgid;
                curProcGroup->proc_num = 0;
                curProcGroup->finish_num = 0;
                curProcGroup->isRunning = (int*) malloc(sizeof(int) * num_system_cmd);
                curProcGroup->status = (int*) malloc(sizeof(int) * num_system_cmd);
                curProcGroup->cmdlines = (char**) malloc(sizeof(char*) * num_system_cmd);
                curProcGroup->pids = (pid_t*) malloc(sizeof(pid_t) * num_system_cmd);
                for (idxPID = 0 ; idxPID < cmd_hdr->cmd_num ; idxPID ++)
                {
                    if (cmd_hdr->cmds[idxPID]->pid != -1)
                    {
                        curProcGroup->pids[curProcGroup->proc_num] = cmd_hdr->cmds[idxPID]->pid;
                        curProcGroup->cmdlines[curProcGroup->proc_num] = getCommandName(cmd_hdr->cmds[idxPID]);
                        curProcGroup->status[curProcGroup->proc_num] = 0;
                        curProcGroup->isRunning[curProcGroup->proc_num] = 1;
                        curProcGroup->proc_num ++;
                    }
                }

                if (curProcGroup->proc_num == 0)
                    freeProcessGroup(&curProcGroup, 0);
                else
                {
                    if (cmd_hdr->isBackGround == 1)
                    {
                        // Insert into backgroundGroup
                        insertIntoBackground(curProcGroup, 1);
                    }
                    else
                    {
                        int proc_idx;

                        // Move the command to foreground
                        moveToForeground(curProcGroup);

                        for (proc_idx = 0 ; proc_idx < curProcGroup->proc_num ; proc_idx ++)
                        {
                            int status;
                            waitpid(curProcGroup->pids[proc_idx], &status, WUNTRACED);
                            setProcessGroupStatus(&curProcGroup, 1, curProcGroup->pids[proc_idx], status, NULL, NULL);
                        }

                        if (curProcGroup->finish_num != curProcGroup->proc_num)
                            insertIntoBackground(curProcGroup, 0);

                        // Move the tsh process group to foreground
                        moveToForeground(shellProcGroup);
                    }
                }
            }
            else if (foregroundGroup != shellProcGroup) // fg command
            {
                ProcessGroup* curProcGroup = foregroundGroup;
                int proc_idx;
                for (proc_idx = 0 ; proc_idx < curProcGroup->proc_num ; proc_idx ++)
                {
                    int status;
                    waitpid(curProcGroup->pids[proc_idx], &status, WUNTRACED);
                    setProcessGroupStatus(&curProcGroup, 1, curProcGroup->pids[proc_idx], status, NULL, NULL);
                }

                if (curProcGroup->finish_num != curProcGroup->proc_num)
                    insertIntoBackground(curProcGroup, 0);

                // Move the tsh process group to foreground
                moveToForeground(shellProcGroup);
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
        while ((pid = waitpid(-1, &status, WNOHANG | WCONTINUED | WUNTRACED)) > 0)
        {
            int idxPG, idxPID;
            int isFinish = 0;
            isFinish = setProcessGroupStatus(backgroundGroup, MAX_BG_JOB, pid, status, &idxPG, &idxPID);

            if (idxPG != -1)
            {
                if (WIFCONTINUED(status))
                    continue;
                else
                {
                    if (idxPG != -1)
                    {
                        fprintf(stderr, "[%d]", idxPG);
                        fprintf(stderr, "\t%d\t", pid);
                        if (WIFEXITED(status))
                            fprintf(stderr, "exited (%d)", WEXITSTATUS(status));
                        else if (WIFSIGNALED(status))
                            fprintf(stderr, "killed (%d)", WTERMSIG(status));
                        else if (WIFSTOPPED(status))
                            fprintf(stderr, "stopped (%d)", WSTOPSIG(status));

                        fprintf(stderr, "\t\t%s\n", backgroundGroup[idxPG]->cmdlines[idxPID]);
                    }
                    if (isFinish)
                    {
                        fprintf(stderr, "[%d]\t[ Finish ]\n", idxPG);
                        freeProcessGroup(backgroundGroup, idxPG);
                    }
                }
            }
            else
            {
                isFinish = setProcessGroupStatus(&foregroundGroup, 1, pid, status, &idxPG, &idxPID);
                if ((idxPG != -1) && (isFinish))
                {
                    moveToForeground(shellProcGroup);
                }
            }
        }
    }

    free (backgroundGroup);
}

void initTSH()
{
    stdin_fd = dup(0);
    stdout_fd = dup(1);

    tsh_cmd_num = sizeof(tsh_cmds) / sizeof(TSH_command);

    pwd = strdup(getenv("PWD"));

    backgroundGroup = (ProcessGroup**) malloc(sizeof(ProcessGroup*) * MAX_BG_JOB);
    memset(backgroundGroup, 0, sizeof(ProcessGroup*) * MAX_BG_JOB);

    // Process group for tsh
    // TODO: more settings ...?
    shellProcGroup = (ProcessGroup*) malloc(sizeof(ProcessGroup));
    shellProcGroup->pgid = getpgrp();
    foregroundGroup = shellProcGroup;

    // PID of tsh
    tsh_pid = getpid();
}

// isBackGround = 1 represents that the process group
// are create to be backgroup.
//
void insertIntoBackground(ProcessGroup* group, int isBackGround)
{
    int idxPG;
    for (idxPG = 0 ; idxPG < MAX_BG_JOB ; idxPG ++)
    {
        if (backgroundGroup[idxPG] == NULL)
        {
            int idxPID;
            backgroundGroup[idxPG] = group;

            if (isBackGround == 1)
            {
                fprintf(stderr, "[%d]\t[ Start ]\n", idxPG);
                fprintf(stderr, "\t");
                for (idxPID = 0 ; idxPID < group->proc_num ; idxPID ++)
                    fprintf(stderr, "%d ", group->pids[idxPID]);
                fprintf(stderr, "\n");
            }
            else
            {
                int status;
                fprintf(stderr, "\n[%d]\n", idxPG);
                for (idxPID = 0 ; idxPID < group->proc_num ; idxPID ++)
                {
                    fprintf(stderr, "\t%d\t", group->pids[idxPID]);
                    status = group->status[idxPID];

                    if (WIFEXITED(status))
                        fprintf(stderr, "exited (%d)", WEXITSTATUS(status));
                    else if (WIFSIGNALED(status))
                        fprintf(stderr, "killed (%d)", WTERMSIG(status));
                    else if (WIFSTOPPED(status))
                        fprintf(stderr, "stopped (%d)", WSTOPSIG(status));

                    fprintf(stderr, "\t\t%s\n", group->cmdlines[idxPID]);
                }
            }

            break;
        }
    }
    if (idxPG == MAX_BG_JOB)
    {
        fprintf(stderr, "tsh: Cannot create background process group\n");
        exit(1);
    }
}

// return 1 if the given process group is finished
int setProcessGroupStatus(ProcessGroup** group, int num_group, pid_t pid, int status, int* pidxPG, int* pidxPID)
{
    int idxPG;
    for (idxPG = 0 ; idxPG < num_group ; idxPG ++)
    {
        ProcessGroup* currGroup = group[idxPG];
        if (currGroup)
        {
            int idxPID;
            for (idxPID = 0 ; idxPID < currGroup->proc_num ; idxPID ++)
            {
                if (currGroup->pids[idxPID] == pid)
                {
                    currGroup->status[idxPID] = status;
                    if (pidxPG)
                        *pidxPG = idxPG;
                    if (pidxPID)
                        *pidxPID = idxPID;

                    if (WIFEXITED(status) || WIFSIGNALED(status))
                    {
                        currGroup->isRunning[idxPID] = 0;
                        currGroup->finish_num ++;
                        if (currGroup->proc_num == currGroup->finish_num)
                        {
                            return 1;
                        }

                    }
                    else if (WIFSTOPPED(status))
                    {
                        currGroup->isRunning[idxPID] = 0;
                    }
                    else if (WIFCONTINUED(status))
                    {
                        currGroup->isRunning[idxPID] = 1;
                    }
                    return 0;
                }
            }
        }
    }
    if (pidxPG)
        *pidxPG = -1;
    if (pidxPID)
        *pidxPID = -1;

    return 0;
}

void freeProcessGroup(ProcessGroup** group, int idxPG)
{
    int idx;
    ProcessGroup* currGroup = group[idxPG];
    free (currGroup->pids);
    free (currGroup->isRunning);
    free (currGroup->status);

    for (idx = 0 ; idx < currGroup->proc_num ; idx ++)
        free (currGroup->cmdlines[idx]);
    free (currGroup->cmdlines);
    free (currGroup);
    group[idxPG] = NULL;

}

void moveToForeground(ProcessGroup* proc)
{
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    if (isatty(0)) tcsetpgrp(0, proc->pgid);
    if (isatty(1)) tcsetpgrp(1, proc->pgid);
    if (isatty(2)) tcsetpgrp(2, proc->pgid);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    foregroundGroup = proc;
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
            check_cmd(tmp_cmd);
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

void check_cmd_env(Command* cmd)
{
    // Check for environment variable
    int arg_idx;
    char *cur_arg;

    for (arg_idx = 0 ; arg_idx < cmd->arg_num ; arg_idx ++)
    {
        char *env = NULL;
        cur_arg = cmd->args[arg_idx];

        if (cur_arg && (cur_arg[0] == '$'))
        {
            printf("here ");
            env = getenv(cur_arg+1);
            free (cmd->args[arg_idx]);
            cmd->args[arg_idx] = NULL;

            if (env)
            {
                cmd->args[arg_idx] = strdup(env);
            }
        }
    }
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

    // TODO: Resize
    //
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

    // TODO use more elegant method ...
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

char* getCommandName(Command* cmd)
{
    char* ret = (char*) malloc(sizeof(char) * 1000);
    int arg_idx;
    ret[0] = '\0';

    for (arg_idx = 0 ; arg_idx < cmd->arg_num ; arg_idx ++)
    {
        if (cmd->args[arg_idx] != NULL) // redirect argument would be clear to 0
        {
            strcat(ret, cmd->args[arg_idx]);
            strcat(ret, " ");
        }
    }

    return ret;
}
