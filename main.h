#include <stdio.h>
#include <unistd.h>
#include <sched.h>

enum MsgType
{
    KillServer_C,
    SubmitJob_C,
    GetJobStatus_C,
    CancelJob_C,
    SubmitResponse_S,
    RunJob_S,
    Unknown,
};

enum JobStatus
{
    Initializing,
    Running,
    Finished,
    Failed,
    Canceled,
    Queued,
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
            enum JobStatus job_status;
        } submit_response;
    };
};

struct Job
{
    struct Job *next;
    enum JobStatus status;
    int jobid;
    int starttime;
    int deadtime;
    int cpus_per_task;
    char *command;
    int pid;
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
int submit_job(char **cmd);

// Path: msg.c
void send_bytes(const int fd, const char *data, int bytes);
int recv_bytes(const int fd, char *data, int bytes);
void send_msg(const int fd, const struct Msg *m);
int recv_msg(const int fd, struct Msg *m);
struct Msg default_msg();

// Path: server.c
enum MsgType client_read(int client_socket);
void server_loop(int socket);
void end_server(int socket);
void notify_parent(int fd);
cpu_set_t prepare_cpus(int cpu_cnt);
void notify_client_to_run_job(struct Job *j, cpu_set_t cpus_to_occupy);
void server_main(int notify_fd, char *_path);

// Path: client.c
void c_shutdown_server(int server_socket);
void c_submit_job(int server_socket, char **command, struct Env **env, int deadtime, int cpus_per_task);
void wait_for_server_command(int server_socket);

// Path: jobs.c
int get_new_jobid();
struct Job *init_queued_job(int deadtime, int cpus_per_task);
void add_job(struct Job *j, FILE *logfile);
void remove_job(struct Job *j, FILE *logfile);
void remove_all_jobs(FILE *logfile);
struct Job *get_next_job_to_run(int free_cpu, FILE *log);
void mark_job_as_running(struct Job *j);