#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "main.h"

int server_socket;
char *socket_path;

void create_socket(char **path)
{
    // socket fd
    char userid[20];
    server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_socket == -1)
    {
        printf("error: getting the server socket");
        exit(-1);
    }

    // socket path
    *path = getenv("JOB_SCHED_SOCKET");
    if (*path != 0)
    {
        *path = (char *)malloc(strlen(*path) + 1);
        strcpy(*path, getenv("JOB_SCHED_SOCKET"));
    }

    char *tmpdir = "/tmp";
    sprintf(userid, "%u", (unsigned int)getuid());
    *path = (char *)malloc(strlen(tmpdir) + strlen("/socket-sched.") + strlen(userid) + 1);
    sprintf(*path, "%s/socket-sched.%s", tmpdir, userid);
    fprintf(stdout, "%s", *path);

    // binding
    // int bind_res = bind(server_socket, )
}

int fork_server(int *server_pid)
{
    int pid;
    int p[2];

    pipe(p);

    pid = fork();
    switch (pid)
    {
    case 0:
        close(p[0]);
        close(server_socket);
        close(0);
        close(1);
        close(2);
        setsid();
        server_main(p[1], socket_path);
        exit(0);
        break;
    case -1:
        return -1;
    default:
        *server_pid = pid;
        close(p[1]);
    }

    return p[0];
}

void wait_server_up(int pipe)
{
    char ch;
    read(pipe, &ch, 1);
    close(pipe);
}

int try_connect(int socket)
{
    struct sockaddr_un addr;
    int res;

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, socket_path);

    res = connect(socket, (struct sockaddr *)&addr, sizeof(addr));

    if (res == -1)
    {
        fprintf(stderr, "Error connecting to the server.\n");
        return -1;
    }
    // switch (res)
    // {
    // case 0:
    //     free(socket_path);
    //     break;
    // case -1:
    //     fprintf(stderr, "The server didn't come up.\n");
    //     break;
    // }

    return res;
}

int close_socket()
{
    return close(server_socket);
}

void free_env()
{
    close_socket();
    free(socket_path);
}

int server_up()
{
    int server_pid;

    fprintf(stdout, "start to run server\n");
    create_socket(&socket_path);
    int wait_pipe = fork_server(&server_pid);
    fprintf(stdout, "server pid: %d\n", server_pid);

    wait_server_up(wait_pipe);
    int res = try_connect(server_socket);

    free_env();
    return 0;
}

int server_down()
{
    create_socket(&socket_path);
    int res = try_connect(server_socket);
    c_shutdown_server(server_socket);

    free_env();
    return 0;
}

cJSON* submit_job(char **cmd)
{
    cJSON *response;
    create_socket(&socket_path);
    int res = try_connect(server_socket);
    if (res == -1)
    {
        response = cJSON_CreateObject();
        cJSON_AddNumberToObject(response, "code", 502);
        cJSON_AddStringToObject(response, "message", "server not up");
        return response;
    }
    response = c_submit_job(server_socket, cmd, NULL, 0, 1);

    free_env();
    return response;
}

int cancel_job(int jobid)
{
    create_socket(&socket_path);
    int res = try_connect(server_socket);
    c_cancel_job(server_socket, jobid);

    free_env();
    return 0;
}

int get_job_info(int jobid)
{
    create_socket(&socket_path);
    int res = try_connect(server_socket);
    c_get_job_info(server_socket, jobid);

    free_env();
    return 0;
}