#include <stdio.h>

enum MsgType
{
    KillServer = 0,
    SubmitJob = 1,
    GetJobStatus = 2,
    CancelJob = 3,
    Unknown = 4,
};

enum JobStatus
{
    Intializing,
    Running,
    Finished,
    Failed,
    Canceled,
    Queued,
    Pending,
};

enum SubmitStatus
{
    SubmitOk = 0,
    SubmitFailed = 1,
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
            int byte_size;
            enum SubmitStatus submit_status;
        } submit_response;

        int byte_size;
        enum JobStatus status;
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
void server_main(int notify_fd, char *_path);

// Path: client.c
void c_shutdown_server(int server_socket);
void c_submit_job(int server_socket, char **command, struct Env **env, int deadtime, int cpus_per_task);

// Path: jobs.c
int get_new_jobid();
void add_job(struct Job *j);
void remove_all_jobs();