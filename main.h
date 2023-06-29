enum MsgType {
    KillServer = 0,
    SubmitJob = 1,
    Unknown = 2,
};

struct Msg {
    enum MsgType type;
    char *data;
};

// Path: server_ctl.c
int server_up();
int server_down();
int close_socket();
int submit_job();

// Path: msg.c
void send_bytes(const int fd, const char *data, int bytes);
int recv_bytes(const int fd, char *data, int bytes);
void send_msg(const int fd, const struct Msg *m);
int recv_msg(const int fd, struct Msg *m);

// Path: server.c
enum MsgType client_read(int client_socket);
void server_loop(int socket);
void end_server(int socket);
void notify_parent(int fd);
void server_main(int notify_fd, char *_path);

// Path: client.c
void c_shutdown_server(int server_socket);