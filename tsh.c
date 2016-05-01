#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <dirent.h>
#include "tsh.h"
#include "tsh_cmd.h"

Command_handler* parse_cmd_hdr(char*);
Command* parse_cmd(char*);
int findSystemCommand(char*);
int processSystemCommand(Command*);
int findTSHCommand(char*);
int processTSHCommand(Command*);
void sig_tstp_handler(int sig);

TSH_command tsh_cmds[] = 
{
    { "help", "Display the list of supported command", tsh_help },
    { "exit", "Exit TSH", tsh_exit }
};
int tsh_cmd_num;

int main()
{
    int stdin_fd = dup(0);
    int stdout_fd = dup(1);
    tsh_cmd_num = sizeof(tsh_cmds) / sizeof(TSH_command);

    // Infinite loop
    while (1)
    {
        char *pwd;
        char input[CMD_MAX_LEN];
        
        // Show the prompt
        pwd = strdup(getenv("PWD"));
        printf("tsh @ %s $ ", pwd);
        free (pwd);

        // Read the command
        if (fgets(input, CMD_MAX_LEN, stdin) != NULL)
        {
            Command_handler* cmd_hdr;
            pid_t child_pid;
            int cmd_idx;
            
            cmd_hdr = parse_cmd_hdr(input);

            for (cmd_idx = 0 ; cmd_idx < cmd_hdr->cmd_num ; cmd_idx ++)
            {
                Command* curr_cmd = cmd_hdr->cmds[cmd_idx];
                char* inputFile = NULL;
                char* outputFile = NULL;

                if (curr_cmd->arg_num == 0)
                    continue;

                // Check for redirect
                int arg_idx;
                int min_idx = curr_cmd->arg_num;
                for (arg_idx = 0 ; arg_idx < curr_cmd->arg_num ; arg_idx ++)
                {
                    if (strcmp(curr_cmd->args[arg_idx], ">") == 0)
                    {
                        free (curr_cmd->args[arg_idx]);
                        curr_cmd->args[arg_idx] = NULL;
                        outputFile = curr_cmd->args[arg_idx + 1];
                    }
                    else if (strcmp(curr_cmd->args[arg_idx], "<") == 0)
                    {
                        free (curr_cmd->args[arg_idx]);
                        curr_cmd->args[arg_idx] = NULL;
                        inputFile = curr_cmd->args[arg_idx + 1];
                    }
                }

                if (inputFile != NULL)
                {
                    // Close stdin fd
                    close(0);
                    open(inputFile, O_RDONLY);
                }
                if (outputFile != NULL)
                {
                    // Close stdout fd
                    close(1);

                    // New file would have -rw-rw-r-- permission
                    open(outputFile, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
                }

                if (findTSHCommand(curr_cmd->args[0]))
                {
                    processTSHCommand(curr_cmd);
                }
                else
                {
                    processSystemCommand(curr_cmd);
                }

                if (inputFile != NULL)
                {
                    // Close stdin fd
                    close(0);
                    dup2(stdin_fd, 0);
                }
                if (outputFile != NULL)
                {
                    // Close stdout fd
                    close(1);
                    dup2(stdout_fd, 1);
                }
            }


            // TODO: Free the cmd_hdr
        }
    }
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

void sig_tstp_handler(int sig)
{

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

int processSystemCommand(Command* cmd)
{
    // Execute the command if they are found in PATH
    if ((child_pid = fork()) < 0)
    {
        fprintf(stderr, "fork error");
        exit(1);
    }
    else if (child_pid == 0) // child
    {
        if (findSystemCommand(cmd->args[0]) == 0)
        {
            fprintf(stderr, "tsh: Command not found: %s\n", cmd->args[0]); 
            exit(0);
        }
        else
        {
            // By default, child would inherit parent's process group ID.
            execvp(cmd->args[0], cmd->args);
        }
    }
    else
    {
        // Ignore these signals, since they should be processed by
        // the child process.
        //
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, sigtstp_handler);

        waitpid(child_pid, NULL, 0);

        // After the child process is finished, parent should handle
        // the following signals.
        //
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
    }
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

Command_handler* parse_cmd_hdr(char* input)
{
    char *subStr;
    char *remainStr;
    Command_handler* ret = (Command_handler*) malloc(sizeof(Command_handler));
    int cur_num = 1;

    ret->cmd_num = 0;
    ret->cmds = (Command**) malloc(sizeof(Command*) * cur_num);

    subStr = strtok_r(input, "|", &remainStr);
    while (subStr != NULL)
    {
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

        ret->cmds[ret->cmd_num] = parse_cmd(subStr);
        ret->cmd_num ++;

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

Command* parse_cmd(char* input)
{
    char *subStr;
    char *remainStr;
    Command* ret = (Command*) malloc(sizeof(Command));
    int cur_num = 1;
    ret->arg_num = 0;
    ret->args = (char**) malloc(sizeof(char*) * cur_num);

    subStr = strtok_r(input, " \n", &remainStr);
    while (subStr != NULL)
    {
        // expand the size of the args array
        if (ret->arg_num == cur_num)
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
