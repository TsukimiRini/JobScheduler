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
#include <signal.h>

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
    char *cmd, *env;

    msg.type = SubmitResponse_S;

    if (m->newjob.cpus_per_task > max_cpu_num)
    {
        status = Null;
        msg.submit_response.jobid = -1;
        msg.submit_response.job_status = status;
        if (m->newjob.command_size > 0)
        {
            cmd = (char *)malloc(m->newjob.command_size + 1);
            res = recv_bytes(s, cmd, m->newjob.command_size);
            if (res == -1)
                fprintf(logfile, "wrong bytes received\n");
            free(cmd);
        }
        if (m->newjob.env_size > 0)
        {
            env = (char *)malloc(m->newjob.env_size + 1);
            int res = recv_bytes(s, env, m->newjob.env_size);
            if (res == -1)
                fprintf(logfile, "wrong bytes received\n");
            free(env);
        }
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

        if (m->newjob.env_size > 0)
        {
            job->env = (char *)malloc(m->newjob.env_size + 1);
            res = recv_bytes(s, job->env, m->newjob.env_size);
            if (res == -1)
                fprintf(logfile, "wrong bytes received\n");
        }

        add_job(job, logfile);
        client_cs[idx].hasjob = 1;
        client_cs[idx].jobid = job->jobid;
        msg.submit_response.jobid = job->jobid;
        msg.submit_response.job_status = status;
    }

    send_msg(s, &msg);

    if (msg.submit_response.job_status != Queued)
    {
        clean_after_client_disappeared(s, idx);
    }
    fflush(logfile);
    return status;
}

void s_cancel_job(int idx, struct Msg *m)
{
    fprintf(logfile, "cancel job\n");
    int s = client_cs[idx].socket;
    int conn = find_conn_of_job(m->canceljob.jobid);
    int socket_conn = client_cs[conn].socket;
    struct Job *job = find_job(m->canceljob.jobid);
    struct Msg msg = default_msg();

    msg.type = CancelResponse_S;
    msg.cancel_response.jobid = m->canceljob.jobid;

    if (job == NULL)
    {
        fprintf(logfile, "job not found\n");
        msg.cancel_response.job_status = Null;
        msg.cancel_response.success = 0;
    }
    else
    {
        fprintf(logfile, "job found\n");
        msg.cancel_response.job_status = job->status;
        msg.cancel_response.pid = job->pid;

        if (job->status != Finished && job->status != Failed && job->status != Cancelled)
        {
            if (job->pid == -1)
            {
                struct Msg cancel_msg = default_msg();
                cancel_msg.type = CancelJob_S;
                send_msg(socket_conn, &cancel_msg);
                msg.cancel_response.success = 1;
            }
            else
            {
                int success = kill(job->pid, SIGINT);
                msg.cancel_response.success = (success == 0 ? 1 : 0);
                if (success == -1)
                {
                    fprintf(logfile, "fail to kill job %d, pid: %d\n", job->jobid, job->pid);
                }
            }
        }
        else
        {
            msg.cancel_response.success = 0;
        }
    }

    if (msg.cancel_response.success == 1)
    {
        mark_job_as_cancelled(job);
        // clean_up_job(job);
        // struct timeval endtime;
        // gettimeofday(&endtime, NULL);
        // job->endtime = endtime;

        // client_cs[conn].hasjob = 0;
        // clean_after_client_disappeared(socket_conn, conn);
    }
    send_msg(s, &msg);
    fprintf(logfile, "job cleaned\n");
    fprintf(logfile, "msg type: %d\n", msg.type);
    fflush(logfile);
}

void s_get_job_info(int idx, struct Msg *m)
{
    fprintf(logfile, "get job info\n");
    int s = client_cs[idx].socket;
    struct Job *job = find_job(m->getjobinfo.jobid);
    struct Msg msg = default_msg();

    msg.type = GetJobInfoResponse_S;

    if (job == NULL)
    {
        fprintf(logfile, "job not found\n");
        msg.getjobinfo_response.job_status = Null;
        msg.getjobinfo_response.deadtime = -1;
        msg.getjobinfo_response.cpus_per_task = -1;
        msg.getjobinfo_response.cmd_size = -1;
        msg.getjobinfo_response.logfname_size = -1;
        msg.getjobinfo_response.env_size = -1;
    }
    else
    {
        fprintf(logfile, "job found\n");
        msg.getjobinfo_response.job_status = job->status;
        msg.getjobinfo_response.deadtime = job->deadtime;
        msg.getjobinfo_response.cpus_per_task = job->cpus_per_task;
        msg.getjobinfo_response.cmd_size = strlen(job->command);
        if (job->logfile != NULL)
            msg.getjobinfo_response.logfname_size = strlen(job->logfile);
        else
            msg.getjobinfo_response.logfname_size = 0;
        if (job->env != NULL)
            msg.getjobinfo_response.env_size = strlen(job->env);
        else
            msg.getjobinfo_response.env_size = 0;
    }
    send_msg(s, &msg);
    if (msg.getjobinfo_response.cmd_size > 0)
        send_bytes(s, job->command, strlen(job->command));
    if (msg.getjobinfo_response.logfname_size > 0)
        send_bytes(s, job->logfile, strlen(job->logfile));
    if (msg.getjobinfo_response.env_size > 0)
        send_bytes(s, job->env, strlen(job->env));
    fprintf(logfile, "msg type: %d\n", msg.type);
    fflush(logfile);
}

void kill_timeout_jobs()
{
    struct Job *job = get_queued_job();
    struct timeval now;
    gettimeofday(&now, NULL);
    while (job != NULL)
    {
        if (job->status == Running && job->deadtime != 0)
        {
            if (now.tv_sec - job->starttime.tv_sec > job->deadtime)
            {
                int conn = find_conn_of_job(job->jobid);
                fprintf(logfile, "job %d timeout, duration %f\n", job->jobid, now.tv_sec - job->starttime.tv_sec + now.tv_usec / 1000000.0 - job->starttime.tv_usec / 1000000.0);
                kill(job->pid, SIGINT);
                job->endtime = now;
                mark_job_as_timeout(job);
                clean_up_job(job);

                client_cs[conn].hasjob = 0;
                clean_after_client_disappeared(client_cs[conn].socket, conn);
            }
        }
        job = job->next;
    }
}

void clean_up_job(struct Job *job)
{
    fprintf(logfile, "finish job\n");
    available_cpu_num += CPU_COUNT(&job->occupied_cpus);
    CPU_XOR(&occupied_cpus, &job->occupied_cpus, &occupied_cpus);
    fprintf(logfile, "available cpu num: %d\n", available_cpu_num);
}

void handle_job_run(int idx, struct Msg *m)
{
    fprintf(logfile, "handle job run\n");
    int s = client_cs[idx].socket;
    struct Job *job = find_job(client_cs[idx].jobid);

    if (job == NULL)
    {
        fprintf(logfile, "job not found\n");
        clean_after_client_disappeared(s, idx);
    }
    else
    {
        job->logfile = (char *)malloc(m->runjob_ok.logfname_size + 1);
        recv_bytes(s, job->logfile, m->runjob_ok.logfname_size);

        fprintf(logfile, "job found\n");
        fprintf(logfile, "job logfile: %s\n", job->logfile);
        mark_job_as_running(job);
        job->pid = m->runjob_ok.pid;
        job->starttime = m->runjob_ok.starttime;
        fprintf(logfile, "job pid: %d\n", job->pid);
        fprintf(logfile, "job starttime: %f\n", job->starttime.tv_sec + job->starttime.tv_usec / 1000000.);
    }
    fflush(logfile);
}

void handle_job_ended(int idx, struct Msg *m)
{
    int s = client_cs[idx].socket;
    struct Job *job = find_job(client_cs[idx].jobid);
    if (job == NULL)
    {
        fprintf(logfile, "job not found\n");
    }
    else
    {
        fprintf(logfile, "job %d ended\n", job->jobid);
        if (job->status != Cancelled)
        {
            mark_job_as_finished(job);
            job->endtime = m->job_ended.endtime;
            fprintf(logfile, "job endtime: %f\n", job->endtime.tv_sec + job->endtime.tv_usec / 1000000.);
            fprintf(logfile, "job duration: %f\n", job->endtime.tv_sec - job->starttime.tv_sec + (job->endtime.tv_usec - job->starttime.tv_usec) / 1000000.);
        }
        clean_up_job(job);
        switch (m->job_ended.exit_status)
        {
        case Error:
            job->status = Failed;
            break;
        case Return:
        case Signal:
        case OnCancel:
            break;
        default:
            fprintf(logfile, "Unknown exit status\n");
            break;
        }
    }

    client_cs[idx].hasjob = 0;
    clean_after_client_disappeared(s, idx);
    fflush(logfile);
}

enum MsgType client_read(int idx)
{
    struct Msg msg;
    int s = client_cs[idx].socket;
    int res = recv_msg(s, &msg);

    if (res == -1)
    {
        fprintf(logfile, "client recv failed\n");
        clean_after_client_disappeared(s, idx);
        return Unknown;
    }
    else if (res == 0)
    {
        fprintf(logfile, "client disconnected\n");
        clean_after_client_disappeared(s, idx);
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
    case RunJobOk_C:
        fprintf(logfile, "read run job ok\n");
        handle_job_run(idx, &msg);
        break;
    case JobEnded_C:
        fprintf(logfile, "read job ended\n");
        handle_job_ended(idx, &msg);
        break;
    case CancelJob_C:
        fprintf(logfile, "read cancel job\n");
        s_cancel_job(idx, &msg);
        break;
    case GetJobInfo_C:
        fprintf(logfile, "read get job status\n");
        s_get_job_info(idx, &msg);
        break;
    default:
        fprintf(logfile, "Unknown message type\n");
        clean_after_client_disappeared(s, idx);
        break;
    }

    return msg.type;
}

static void remove_connection(int index)
{
    int i;

    if (client_cs[index].hasjob)
    {
        remove_job(client_cs[index].jobid);
    }

    for (i = index; i < (nconnections - 1); ++i)
    {
        memcpy(&client_cs[i], &client_cs[i + 1], sizeof(client_cs[0]));
    }
    nconnections--;
}

void clean_after_client_disappeared(int socket, int index)
{
    /* Act as if the job ended. */
    int jobid = client_cs[index].jobid;
    if (client_cs[index].hasjob)
    {
        // struct Result r;

        // r.errorlevel = -1;
        // r.died_by_signal = 1;
        // r.signal = SIGKILL;
        // r.user_ms = 0;
        // r.system_ms = 0;
        // r.real_ms = 0;
        // r.skipped = 0;

        // warning("JobID %i quit while running.", jobid);
        // job_finished(&r, jobid);
        // /* For the dependencies */
        // check_notify_list(jobid);
        // /* We don't want this connection to do anything
        //  * more related to the jobid, secially on remove_connection
        //  * when we receive the EOC. */

        // TODO
        struct Job *job = find_job(jobid);
        kill(job->pid, SIGINT);
        mark_job_as_cancelled(job);
        clean_up_job(job);
        client_cs[index].hasjob = 0;
    }

    fprintf(logfile, "clean client\n");
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
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;

        kill_timeout_jobs();

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

        res = select(maxfd + 1, &readset, NULL, NULL, &tv);

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
                j->occupied_cpus = cpus_to_occupy;
                fflush(logfile);
                notify_client_to_run_job(j, cpus_to_occupy);
                if (j != NULL)
                {
                    mark_job_as_allocating(j);
                }
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
        remove_job(j->jobid);
        return;
    }

    struct Msg m;
    m.type = RunJob_S;
    m.runjob.cpuset = cpus_to_occupy;
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
            CPU_SET(i, &occupied_cpus);
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
        CPU_XOR(&occupied_cpus, &cpus_to_occupy, &cpus_to_occupy);
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

    fprintf(logfile, "max cpu num: %d\n", max_cpu_num);
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