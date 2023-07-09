#define _GNU_SOURCE
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sched.h>

#include "main.h"

#define MAXCONN 100

char *path;
char *logpath;
FILE *logfile;
int nconnections;

struct Client_conn
{
    int socket;
    int hasjob;
    int jobid;
};

struct Client_conn client_cs[MAXCONN];
cpu_set_t occupied_cpus;
int available_cpu_num;
int max_cpu_num;

enum JobStatus s_create_job(int idx, struct Msg *m)
{
    fprintf(logfile, "create job\n");
    int s = client_cs[idx].socket;
    enum JobStatus status = Initializing;
    struct Job *job;
    int res;
    struct Msg msg = default_msg();

    if (m->newjob.cpus_per_task > max_cpu_num)
    {
        status = Failed;
    }
    else
    {
        status = Queued;

        job = init_queued_job(m->newjob.deadtime, m->newjob.cpus_per_task);
        if (m->newjob.command_size > 0)
        {
            job->command = (char *)malloc(m->newjob.command_size + 1);
            res = recv_bytes(s, job->command, m->newjob.command_size);
            if (res == -1)
                fprintf(logfile, "wrong bytes received\n");
            fprintf(logfile, "%s\n", job->command);
        }
        add_job(job, logfile);
        client_cs[idx].hasjob = 1;
        client_cs[idx].jobid = job->jobid;
    }

    msg.type = SubmitResponse_S;
    msg.submit_response.job_status = status;
    send_msg(s, &msg);
    fflush(logfile);
    return status;
}

enum MsgType client_read(int idx)
{
    struct Msg msg;
    int res = recv_msg(client_cs[idx].socket, &msg);

    if (res == -1)
    {
        fprintf(logfile, "client recv failed\n");
        // clean_after_client_disappeared(s, index);
        return Unknown;
    }
    else if (res == 0)
    {
        // clean_after_client_disappeared(s, index);
        return Unknown;
    }

    /* process message */
    switch (msg.type)
    {
    case KillServer_C:
        fprintf(logfile, "read kill server\n");
        break;
    case SubmitJob_C:
        fprintf(logfile, "read submit job\n");
        enum JobStatus ret_s = s_create_job(idx, &msg);
        fprintf(logfile, "job status: %d\n", ret_s);
        break;
    default:
        fprintf(logfile, "Unknown message type\n");
        break;
    }

    return msg.type;
}

static void remove_connection(int index)
{
    int i;

    // if (client_cs[index].hasjob) {
    //     s_removejob(client_cs[index].jobid);
    // }

    for (i = index; i < (nconnections - 1); ++i)
    {
        memcpy(&client_cs[i], &client_cs[i + 1], sizeof(client_cs[0]));
    }
    nconnections--;
}

void clean_after_client_disappeared(int socket, int index)
{
    /* Act as if the job ended. */
    // int jobid = client_cs[index].jobid;
    // if (client_cs[index].hasjob) {
    //     struct Result r;

    //     r.errorlevel = -1;
    //     r.died_by_signal = 1;
    //     r.signal = SIGKILL;
    //     r.user_ms = 0;
    //     r.system_ms = 0;
    //     r.real_ms = 0;
    //     r.skipped = 0;

    //     warning("JobID %i quit while running.", jobid);
    //     job_finished(&r, jobid);
    //     /* For the dependencies */
    //     check_notify_list(jobid);
    //     /* We don't want this connection to do anything
    //      * more related to the jobid, secially on remove_connection
    //      * when we receive the EOC. */
    //     client_cs[index].hasjob = 0;
    // } else
    //     /* If it doesn't have a running job,
    //      * it may well be a notification */
    //     s_remove_notification(socket);

    close(socket);
    remove_connection(index);
}

void server_loop(int socket)
{
    fprintf(logfile, "server loop\n");
    fd_set readset;
    int maxfd;
    int res, i;
    int keep_loop = 1;
    while (keep_loop)
    {
        FD_ZERO(&readset);
        FD_SET(socket, &readset);
        maxfd = socket;

        if (nconnections < MAXCONN)
        {
            FD_SET(socket, &readset);
            maxfd = socket;
        }
        for (i = 0; i < nconnections; ++i)
        {
            FD_SET(client_cs[i].socket, &readset);
            if (client_cs[i].socket > maxfd)
                maxfd = client_cs[i].socket;
        }

        res = select(maxfd + 1, &readset, NULL, NULL, NULL);

        if (res != -1)
        {
            if (FD_ISSET(socket, &readset))
            {
                int cs;
                cs = accept(socket, NULL, NULL);
                if (cs == -1)
                    fprintf(logfile, "Accepting from %i", socket);
                client_cs[nconnections].hasjob = 0;
                client_cs[nconnections].socket = cs;
                ++nconnections;
            }
            for (i = 0; i < nconnections; ++i)
            {
                if (FD_ISSET(client_cs[i].socket, &readset))
                {
                    enum MsgType b;
                    b = client_read(i);
                    /* Check if we should break */
                    // if (b == CLOSE) {
                    //     warning("Closing");
                    //     /* On unknown message, we close the client,
                    //        or it may hang waiting for an answer */
                    //     clean_after_client_disappeared(client_cs[i].socket, i);
                    // }

                    if (b == KillServer_C)
                        keep_loop = 0;
                }
            }
        }

        struct Job *j = get_next_job_to_run(available_cpu_num, logfile);
        if (j != NULL)
        {
            fprintf(logfile, "try to run job %d\n", j->jobid);
            cpu_set_t cpus_to_occupy = prepare_cpus(j->cpus_per_task);
            if (CPU_COUNT(&cpus_to_occupy) == 0)
            {
                fprintf(logfile, "no available cpu\n");
            }
            else
            {
                fprintf(logfile, "occupy cpu: %d\n", CPU_COUNT(&cpus_to_occupy));
                fflush(logfile);
                notify_client_to_run_job(j, cpus_to_occupy);
                if (j != NULL)
                    mark_job_as_running(j);
            }
        }
        fflush(logfile);
    }

    end_server(socket);
}

void end_server(int socket)
{
    unlink(path);
    close(socket);

    free(path);
    free(logpath);
    remove_all_jobs(logfile);

    fclose(logfile);
}

void notify_parent(int fd)
{
    char a = 'a';
    write(fd, &a, 1);
    close(fd);
}

int find_conn_of_job(int jobid)
{
    for (int i = 0; i < nconnections; i++)
    {
        if (client_cs[i].hasjob && client_cs[i].jobid == jobid)
        {
            return i;
        }
    }
    return -1;
}

void notify_client_to_run_job(struct Job *j, cpu_set_t cpus_to_occupy)
{
    int conn = find_conn_of_job(j->jobid);
    if (conn == -1)
    {
        fprintf(logfile, "no connection for job %d\n", j->jobid);
        remove_job(j, logfile);
        return;
    }

    struct Msg m;
    m.type = RunJob_S;
    send_msg(client_cs[conn].socket, &m);
}

cpu_set_t prepare_cpus(int cpu_cnt)
{
    cpu_set_t cpus_to_occupy;
    CPU_ZERO(&cpus_to_occupy);
    for (int i = 0; i < max_cpu_num; i++)
    {
        if (!CPU_ISSET(i, &occupied_cpus))
        {
            CPU_CLR(i, &occupied_cpus);
            CPU_SET(i, &cpus_to_occupy);
            available_cpu_num--;
            cpu_cnt--;
            if (cpu_cnt == 0)
            {
                break;
            }
        }
    }

    if (cpu_cnt != 0)
    {
        fprintf(logfile, "no enough cpus\n");
        fflush(logfile);
        CPU_ZERO(&cpus_to_occupy);
    }

    return cpus_to_occupy;
}

void server_main(int notify_fd, char *_path)
{
    int ls;
    struct sockaddr_un addr;
    int res;
    char *dirpath;

    path = _path;

    max_cpu_num = sysconf(_SC_NPROCESSORS_CONF);
    available_cpu_num = max_cpu_num;
    CPU_ZERO(&occupied_cpus);

    /* Move the server to the socket directory */
    dirpath = malloc(strlen(path) + 1);
    strcpy(dirpath, path);
    chdir(dirname(dirpath));
    free(dirpath);

    logpath = malloc(strlen(path) + 5);
    sprintf(logpath, "%s.log", path);
    logfile = fopen(logpath, "w");

    fprintf(logfile, "%s", path);
    fflush(logfile);

    ls = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ls == -1)
    {
        fprintf(logfile, "cannot create the listen socket in the server\n");
        exit(-1);
    }

    nconnections = 0;

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);

    res = bind(ls, (struct sockaddr *)&addr, sizeof(addr));
    if (res == -1)
    {
        fprintf(logfile, "Error binding: %d\n", errno);
        exit(-1);
    }

    res = listen(ls, 0);
    if (res == -1)
    {
        fprintf(logfile, "Error listening.\n");
        exit(-1);
    }

    fprintf(logfile, "server main\n");

    notify_parent(notify_fd);

    server_loop(ls);
}