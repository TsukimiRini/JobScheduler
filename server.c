#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>

#include "main.h"

#define MAXCONN 100

static char *path;
static char *logpath;
static FILE *logfile;
static int nconnections;

struct Client_conn
{
    int socket;
    int hasjob;
    int jobid;
};

static struct Client_conn client_cs[MAXCONN];

struct Job *init_job(int deadtime, int cpus_per_task)
{
    struct Job *j = (struct Job *)malloc(sizeof(struct Job));
    j->jobid = get_new_jobid();
    j->deadtime = deadtime;
    j->cpus_per_task = cpus_per_task;
    j->status = JobIntializing;
    return j;
}

enum JobStatus s_create_job(int s, struct Msg *m)
{
    struct Job *job = init_job(m->newjob.deadtime, m->newjob.cpus_per_task);
    int res;
    fprintf(logfile, "%i\n", m->newjob.command_size);
    if (m->newjob.command_size > 0)
    {
        job->command = (char *)malloc(m->newjob.command_size + 1);
        res = recv_bytes(s, job->command, m->newjob.command_size);
        if (res == -1)
            fprintf(logfile, "wrong bytes received");
        fprintf(logfile, "%s\n", job->command);
    }
    add_job(job);

    return job->status;
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
    case KillServer:
        fprintf(logfile, "read kill server\n");
        break;
    case SubmitJob:
        fprintf(logfile, "read submit job\n");
        s_create_job(client_cs[idx].socket, &msg);
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

                    if (b == KillServer)
                        keep_loop = 0;
                }
            }
        }

        // int client_socket = accept(socket, NULL, NULL);
        // int res = client_read(client_socket);

        // switch (res)
        // {
        // case KillServer:
        //     fprintf(logfile, "server killed\n");
        //     break;
        // default:
        //     fprintf(logfile, "Unknown message type\n");
        // }
        fflush(logfile);
    }

    end_server(socket);
}

void end_server(int socket)
{
    unlink(path);
    close(socket);
    fclose(logfile);

    free(path);
    free(logpath);
    remove_all_jobs();
}

void notify_parent(int fd)
{
    char a = 'a';
    write(fd, &a, 1);
    close(fd);
}

void server_main(int notify_fd, char *_path)
{
    int ls;
    struct sockaddr_un addr;
    int res;
    char *dirpath;

    path = _path;

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