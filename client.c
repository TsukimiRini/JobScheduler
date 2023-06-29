#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "main.h"

char *get_cmd_str(char **command)
{
    int i;
    int size = 0;
    char *cmd_str;

    if (command == NULL)
        return NULL;

    for (i = 0; command[i] != NULL; i++)
        size += strlen(command[i]) + 1;
    cmd_str = (char *)malloc(size);
    if (cmd_str == NULL)
        return NULL;

    strcat(cmd_str, command[0]);
    for (i = 1; command[i] != NULL; i++)
    {
        strcat(cmd_str, " ");
        strcat(cmd_str, command[i]);
    }
    return cmd_str;
}

char *get_env_str(struct Env **env)
{
    int i;
    int size = 0;
    char *env_str;

    if (env == NULL)
        return NULL;

    for (i = 0; env[i] != NULL; i++)
    {
        size += strlen(env[i]->key) + 1;
        for (int j = 0; env[i]->values[j] != NULL; j++)
            size += strlen(env[i]->values[j]) + 1;
    }
    env_str = (char *)malloc(size);
    if (env_str == NULL)
        return NULL;

    for (i = 0; env[i] != NULL; i++)
    {
        strcat(env_str, env[i]->key);
        strcat(env_str, "=");
        strcat(env_str, env[i]->values[0]);
        for (int j = 1; env[i]->values[j] != NULL; j++)
        {
            strcat(env_str, ";");
            strcat(env_str, env[i]->values[j]);
        }
    }
    return env_str;
}

char *get_command(char **command, struct Env **env)
{
    int i;
    int size = 0;
    char *command_str = get_cmd_str(command);
    char *env_str = get_env_str(env);
    char *res = (char *)malloc((command_str ? strlen(command_str) : 0) + (env_str ? strlen(env_str) : 0) + 1);
    if (env_str)
        strcat(res, env_str);
    if (command_str)
        strcat(res, command_str);

    if (command_str)
        free(command_str);
    if (env_str)
        free(env_str);
    return res;
}

void c_shutdown_server(int server_socket)
{
    struct Msg m;

    m.type = KillServer;
    send_msg(server_socket, &m);
}

void c_submit_job(int server_socket, char **command, struct Env **env, int deadtime, int cpus_per_task)
{
    struct Msg m = default_msg();
    char *command_str = get_command(command, env);

    m.type = SubmitJob;
    m.newjob.deadtime = deadtime;
    m.newjob.cpus_per_task = cpus_per_task;
    m.newjob.command_size = strlen(command_str);

    send_msg(server_socket, &m);
    send_bytes(server_socket, command_str, m.newjob.command_size);

    free(command_str);
}