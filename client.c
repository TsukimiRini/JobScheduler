#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sched.h>

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

static void fill_first_3_handles()
{
    int tmp_pipe1[2];
    int tmp_pipe2[2];
    /* This will fill handles 0 and 1 */
    pipe(tmp_pipe1);
    /* This will fill handles 2 and 3 */
    pipe(tmp_pipe2);

    close(tmp_pipe2[1]);
}

void create_closed_read_on(int dest)
{
    int p[2];
    /* Closing input */
    pipe(p);
    close(p[1]);      /* closing the write handle */
    dup2(p[0], dest); /* the pipe reading goes to dest */
    if (p[0] != dest)
        close(p[0]);
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
        fill_first_3_handles();
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

int c_cancel_job(int server_socket, int job_id)
{
    struct Msg m = default_msg();
    struct Msg received = default_msg();

    m.type = CancelJob_C;
    m.canceljob.jobid = job_id;

    send_msg(server_socket, &m);
    recv_msg(server_socket, &received);
    if (received.type != CancelResponse_S)
    {
        fprintf(stderr, "Error: server did not respond with CancelResponse_S, %d\n", received.type);
        exit(1);
    }
    if (received.cancel_response.success == 1)
        printf("Job cancelled\n");
    else
    {
        switch (received.cancel_response.job_status)
        {
        case Initializing:
        case Running:
        case Queued:
        case Allocating:
            printf("Fail to cancel the job\n");
            break;
        case Failed:
            printf("Job has failed\n");
            break;
        case Finished:
            printf("Job has finished\n");
            break;
        case Cancelled:
            printf("Job has been cancelled\n");
            break;
        case Timeout:
            printf("Job has timed out\n");
            break;
        case Null:
            printf("Job does not exist\n");
            break;
        default:
            printf("Job is in unknown state\n");
            break;
        }
    }

    return received.cancel_response.success;
}

void c_get_job_info(int server_socket, int job_id)
{
    struct Msg m = default_msg();
    struct Msg received = default_msg();
    char *cmd;
    char *logfname;

    m.type = GetJobInfo_C;
    m.getjobinfo.jobid = job_id;

    send_msg(server_socket, &m);
    recv_msg(server_socket, &received);
    if (received.type != GetJobInfoResponse_S)
    {
        fprintf(stderr, "Error: server did not respond with GetJobInfoResponse_S\n");
        exit(1);
    }
    if (received.getjobinfo_response.job_status == Null)
    {
        printf("Job does not exist\n");
        return;
    }

    cmd = (char *)malloc(received.getjobinfo_response.cmd_size + 1);
    logfname = (char *)malloc(received.getjobinfo_response.logfname_size + 1);
    recv_bytes(server_socket, cmd, received.getjobinfo_response.cmd_size);
    recv_bytes(server_socket, logfname, received.getjobinfo_response.logfname_size);

    printf("Job %d: ", job_id);
    switch (received.getjobinfo_response.job_status)
    {
    case Initializing:
        printf("Initializing\n");
        break;
    case Running:
        printf("Running\n");
        break;
    case Queued:
        printf("Queued\n");
        break;
    case Allocating:
        printf("Allocating\n");
        break;
    case Failed:
        printf("Failed\n");
        break;
    case Finished:
        printf("Finished\n");
        break;
    case Cancelled:
        printf("Cancelled\n");
        break;
    case Timeout:
        printf("Timeout\n");
        break;
    default:
        printf("Unknown\n");
        break;
    }
    printf("Deadtime: %d\n", received.getjobinfo_response.deadtime);
    printf("CPUs per task: %d\n", received.getjobinfo_response.cpus_per_task);
    printf("Command: %s\n", cmd);
    printf("Log file: %s\n", logfname);
    printf("Error file: %s.e\n", logfname);
}

void run_child(int outfd, char **command, struct Env **env)
{
    struct timeval starttime;
    int logfd, errfd;
    char *cmd = get_cmd_str(command);
    char *logfullname = (char *)malloc(strlen("/tmp/job.XXXXXX") + 1);
    char *errfile = (char *)malloc(strlen(logfullname) + 3);

    // stdout
    strcpy(logfullname, "/tmp/job.XXXXXX");
    logfd = mkstemp(logfullname);
    fprintf(stdout, "logfullname: %s\n", logfullname);
    assert(logfd != -1);
    write(logfd, cmd, strlen(cmd));
    write(logfd, "\n", 2);
    free(cmd);
    dup2(logfd, 1);
    close(logfd);
    // stderr
    sprintf(errfile, "%s.e", logfullname);
    errfd = open(errfile, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    dup2(errfd, 2);
    close(errfd);

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

    gettimeofday(&starttime, NULL);
    int logfullname_len = strlen(logfullname);
    write(outfd, &starttime, sizeof(struct timeval));
    write(outfd, &logfullname_len, sizeof(int));
    write(outfd, logfullname, strlen(logfullname) + 1);
    free(logfullname);
    close(outfd);

    create_closed_read_on(0);
    setsid();

    execvp(command[0], command);
}

void run_parent(int outfd, int server_socket, int pid)
{
    struct Msg m;
    int status;
    struct timeval starttime;
    struct timeval endtime;
    int res, fname_len;
    char *logfullname;

    read(outfd, &starttime, sizeof(struct timeval));
    read(outfd, &fname_len, sizeof(int));
    logfullname = (char *)malloc(fname_len);
    res = read(outfd, logfullname, fname_len);
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

    signals_child_pid = pid;
    unblock_sigint_and_install_handler();

    fprintf(stdout, "Job started\n");

    m.type = RunJobOk_C;
    m.runjob_ok.pid = getpid();
    m.runjob_ok.starttime = starttime;
    m.runjob_ok.logfname_size = fname_len;
    send_msg(server_socket, &m);
    send_bytes(server_socket, logfullname, fname_len);

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

void run_job(int server_socket, char **command, struct Env **env, cpu_set_t cpuset)
{
    int p[2];
    int pid;

    block_sigint();

    if (pipe(p) == -1)
    {
        fprintf(stderr, "Error: pipe failed\n");
        exit(1);
    }

    if (sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpuset) == -1)
    {
        fprintf(stderr, "Error: sched_setaffinity failed\n");
        exit(1);
    }

    pid = fork();

    sched_getaffinity(getpid(), sizeof(cpu_set_t), &cpuset);

    fprintf(stdout, "cpu: %d\n", CPU_COUNT(&cpuset));

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
        fprintf(stdout, "pid: %d\n", pid);
        run_parent(p[0], server_socket, pid);
        break;
    }
}

void wait_for_server_command_and_then_execute(int server_socket, char **command, struct Env **env)
{
    struct Msg m;
    int i;
    int logfname_size;

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

        run_job(server_socket, command, env, m.runjob.cpuset);
        return;
    }
    else if (m.type == CancelJob_C)
    {
        printf("Job to cancel\n");
        return;
    }
    else
    {
        fprintf(stderr, "Error: server sent wrong command %d\n", m.type);
        exit(1);
    }
}