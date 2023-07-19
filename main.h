#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>

#include "cJSON.h"

enum MsgType
{
    KillServer_C,
    SubmitJob_C,
    GetJobInfo_C,
    CancelJob_C,
    RunJobOk_C,
    JobEnded_C,
    SubmitResponse_S,
    GetJobInfoResponse_S,
    CancelResponse_S,
    RunJob_S,
    CancelJob_S,
    Unknown,
};

enum JobStatus
{
    Initializing,
    Running,
    Finished,
    Failed,
    Cancelled,
    Queued,
    Allocating,
    Timeout,
    Null,
};

enum ExitStatus
{
    Return,
    Signal,
    Error,
};

struct Msg
{
    enum MsgType type;

    union
    {
        struct
        {
            int command_size;
            int env_size;
            int deadtime;
            int cpus_per_task;
        } newjob;
        struct
        {
            int jobid;
            enum JobStatus job_status;
        } submit_response;
        struct
        {
            cpu_set_t cpuset;
        } runjob;
        struct
        {
            int jobid;
        } canceljob;
        struct
        {
            int pid;
            struct timeval starttime;
            int logfname_size;
        } runjob_ok;
        struct
        {
            int pid;
            enum ExitStatus exit_status;
            struct timeval endtime;
        } job_ended;
        struct
        {
            int jobid;
            enum JobStatus job_status;
            int pid;
            int success;
        } cancel_response;
        struct {
            int jobid;
        } getjobinfo;
        struct {
            int deadtime;
            int cpus_per_task;
            enum JobStatus job_status;
            int cmd_size;
            int logfname_size;
        } getjobinfo_response;
    };
};

struct Job
{
    struct Job *next;
    enum JobStatus status;
    int jobid;
    struct timeval starttime;
    struct timeval endtime;
    int deadtime;
    int cpus_per_task;
    char *command;
    int pid;
    cpu_set_t occupied_cpus;
    char *logfile;
};

struct Env
{
    char *key;
    char **values;
};

// Path: server_ctl.c
int server_up();
int server_down();
int close_socket();
cJSON* submit_job(char **cmd);
int cancel_job(int jobid);
int get_job_info(int jobid);

// Path: msg.c
void send_bytes(const int fd, const char *data, int bytes);
int recv_bytes(const int fd, char *data, int bytes);
void send_msg(const int fd, const struct Msg *m);
int recv_msg(const int fd, struct Msg *m);
struct Msg default_msg();

// Path: server.c
void clean_up_job(struct Job *job);
enum MsgType client_read(int client_socket);
void server_loop(int socket);
void end_server(int socket);
int find_conn_of_job(int jobid);
void clean_after_client_disappeared(int socket, int index);
void notify_parent(int fd);
cpu_set_t prepare_cpus(int cpu_cnt);
void notify_client_to_run_job(struct Job *j, cpu_set_t cpus_to_occupy);
void server_main(int notify_fd, char *_path);

// Path: client.c
void c_shutdown_server(int server_socket);
cJSON* c_submit_job(int server_socket, char **command, struct Env **env, int deadtime, int cpus_per_task);
int c_cancel_job(int server_socket, int job_id);
void c_get_job_info(int server_socket, int job_id);
void wait_for_server_command_and_then_execute(int server_socket, char **command, struct Env **env);

// Path: jobs.c
int get_new_jobid();
struct Job* get_queued_job();
struct Job *init_queued_job(int deadtime, int cpus_per_task);
struct Job *find_job(int jobid);
void add_job(struct Job *j, FILE *logfile);
void remove_job(int jobid);
void remove_all_jobs(FILE *logfile);
struct Job *get_next_job_to_run(int free_cpu, FILE *log);
void mark_job_as_allocating(struct Job *j);
void mark_job_as_running(struct Job *j);
void mark_job_as_finished(struct Job *j);
void mark_job_as_cancelled(struct Job *j);
void mark_job_as_timeout(struct Job *j);
int kill_job_when_no_conn(struct Job *j);