#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#include "main.h"
#include "signals.c"

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

char *append_each_env_str(char *env_str, struct Env *env)
{
    if (env_str == NULL)
        fprintf(stderr, "env_str is NULL");
    strcat(env_str, env->key);
    strcat(env_str, "=");
    strcat(env_str, env->values[0]);
    for (int j = 1; env->values[j] != NULL; j++)
    {
        strcat(env_str, ";");
        strcat(env_str, env->values[j]);
    }
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
    {
        fprintf(stderr, "env_str is NULL");
        exit(1);
    }

    for (i = 0; env[i] != NULL; i++)
    {
        if (i != 0)
            strcat(env_str, " ");
        append_each_env_str(env_str, env[i]);
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

void go_background()
{
    int pid = fork();

    switch (pid)
    {
    case -1:
        fprintf(stderr, "Error: fork failed\n");
        exit(1);
    case 0:
        close(0);
        close(1);
        close(2);
        setsid();
        break;
    default:
        exit(0);
    }
}

void c_shutdown_server(int server_socket)
{
    struct Msg m;

    m.type = KillServer_C;
    send_msg(server_socket, &m);
}

void c_submit_job(int server_socket, char **command, struct Env **env, int deadtime, int cpus_per_task)
{
    struct Msg m = default_msg();
    char *command_str = get_command(command, env);

    m.type = SubmitJob_C;
    m.newjob.deadtime = deadtime;
    m.newjob.cpus_per_task = cpus_per_task;
    m.newjob.command_size = strlen(command_str);

    send_msg(server_socket, &m);
    send_bytes(server_socket, command_str, m.newjob.command_size);

    recv_msg(server_socket, &m);
    if (m.type != SubmitResponse_S)
    {
        fprintf(stderr, "Error: server did not respond with SubmitResponse_S\n");
        exit(1);
    }
    if (m.submit_response.job_status == Queued)
    {
        printf("Job queued\n");
        // go_background();
        wait_for_server_command_and_then_execute(server_socket, command, env);
    }
    else
        printf("Job rejected\n");

    free(command_str);
}

void run_child(int outfd, char **command, struct Env **env)
{
    struct timeval starttime;

    if (command == NULL)
    {
        fprintf(stderr, "Error: command is NULL\n");
        exit(1);
    }
    if (env != NULL)
    {
        for (int i = 0; env[i] != NULL; i++)
        {
            int env_size = 0;
            for (int j = 0; env[i]->values[j] != NULL; j++)
                env_size += strlen(env[i]->values[j]) + 1;
            char *env_str = (char *)malloc(env_size);
            if (env_str == NULL)
            {
                fprintf(stderr, "Error: malloc failed\n");
                exit(1);
            }
            append_each_env_str(env_str, env[i]);
            fprintf(stdout, "env_str: %s\n", env_str);
            putenv(env_str);
        }
    }

    struct Msg m = default_msg();
    m.type = RunJobOk_C;
    m.runjob_ok.pid = getpid();

    gettimeofday(&starttime, NULL);
    write(outfd, &starttime, sizeof(struct timeval));

    execvp(command[0], command);
}

void run_parent(int outfd, int server_socket, int pid)
{
    struct Msg m;
    int status;
    struct timeval starttime;
    struct timeval endtime;
    int res;

    res = read(outfd, &starttime, sizeof(struct timeval));
    if (res == -1)
    {
        fprintf(stderr, "Error: read failed\n");
        exit(1);
    }
    else if (res == 0)
    {
        fprintf(stderr, "Error: read returned 0\n");
        exit(1);
    }

    unblock_sigint_and_install_handler();

    fprintf(stdout, "Job started\n");

    m.type = RunJobOk_C;
    m.runjob_ok.pid = pid;
    m.runjob_ok.starttime = starttime;
    send_msg(server_socket, &m);

    close(outfd);

    wait(&status);

    fprintf(stdout, "Job finished\n");

    struct Msg res_m = default_msg();

    res_m.type = JobEnded_C;
    res_m.job_ended.pid = pid;
    gettimeofday(&endtime, NULL);
    res_m.job_ended.endtime = endtime;
    fprintf(stdout, "endtime: %ld\n", endtime.tv_sec);
    if (WIFEXITED(status))
    {
        res_m.job_ended.exit_status = Return;
    }
    else if (WIFSIGNALED(status))
    {
        res_m.job_ended.exit_status = Signal;
    }
    else
    {
        res_m.job_ended.exit_status = Error;
    }
    send_msg(server_socket, &res_m);
}

struct Msg *run_job(int server_socket, char **command, struct Env **env)
{
    int p[2];
    int pid;
    // TODO
    // char *logdir;

    block_sigint();

    if (pipe(p) == -1)
    {
        fprintf(stderr, "Error: pipe failed\n");
        exit(1);
    }

    pid = fork();

    switch (pid)
    {
    case -1:
        fprintf(stderr, "Error: fork failed\n");
        exit(1);
    case 0:
        restore_sigmask();
        close(p[0]);
        close(server_socket);
        run_child(p[1], command, env);
        fprintf(stderr, "Error: execvp failed\n");
        exit(1);
        break;
    default:
        close(p[1]);
        run_parent(p[0], server_socket, pid);
        break;
    }
}

struct Msg *wait_for_server_command_and_then_execute(int server_socket, char **command, struct Env **env)
{
    struct Msg m;
    int i;

    int res = recv_msg(server_socket, &m);
    if (res == 0)
    {
        printf("Server closed connection\n");
        exit(0);
    }
    if (res == -1)
    {
        fprintf(stderr, "Error: recv_msg failed\n");
        exit(1);
    }

    if (m.type == RunJob_S)
    {
        printf("Job to run\n");
        return run_job(server_socket, command, env);
    }
    else
    {
        fprintf(stderr, "Error: server sent wrong command %d\n", m.type);
        exit(1);
    }
}