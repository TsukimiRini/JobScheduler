#include <stdio.h>

#include "main.h"


void c_shutdown_server(int server_socket) {
    struct Msg m;

    m.type = KillServer;
    send_msg(server_socket, &m);
}

void c_submit_job(int server_socket, char *data) {
    struct Msg m;

    m.type = SubmitJob;
    m.data = data;
    send_msg(server_socket, &m);
}