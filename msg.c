#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "main.h"

struct Msg default_msg()
{
    struct Msg m;
    memset(&m, 0, sizeof(struct Msg));
    return m;
}

void send_bytes(const int fd, const char *data, int bytes)
{
    int res;
    int offset = 0;

    while (1)
    {
        res = send(fd, data + offset, bytes, 0);
        if (res == -1)
        {
            fprintf(stderr, "Sending %i bytes to %i.\n", bytes, fd);
            break;
        }
        if (res == bytes)
            break;
        offset += res;
        bytes -= res;
    }
}

int recv_bytes(const int fd, char *data, int bytes)
{
    int res;
    int offset = 0;

    while (1)
    {
        res = recv(fd, data + offset, bytes, 0);
        if (res == -1)
        {
            fprintf(stderr, "Receiving %i bytes from %i.\n", bytes, fd);
            break;
        }
        if (res == bytes)
            break;
        offset += res;
        bytes -= res;
    }

    return res;
}

void send_msg(const int fd, const struct Msg *m)
{
    fprintf(stdout, "send msg\n");
    int res;

    // if (0)
    //     msgdump(stderr, m);
    res = send(fd, m, sizeof(*m), 0);
    if (res == -1 || res != sizeof(*m))
        perror("send_msg");
    // fprintf(stdout, "Sending a message to %i, sent %i bytes, should "
    //                 "send %i.",
    //         fd,
    //         res, sizeof(*m));
}

int recv_msg(const int fd, struct Msg *m)
{
    int res;

    res = recv(fd, m, sizeof(*m), 0);
    if (res == -1)
        fprintf(stderr, "Unknown error from %i.\n", fd);
    // if (res == sizeof(*m) && 0)
    //     msgdump(stderr, m);
    if (res != sizeof(*m) && res > 0)
        fprintf(stdout, "Receiving a message from %i, received %i bytes, "
                        "should have received %i.\n",
                fd,
                res, (int)sizeof(*m));

    return res;
}

// void send_ints(const int fd, const int *data, int num)
// {
//     int res;
//     res = write(fd, &num, sizeof(int));
//     if (res == -1)
//         fprintf(stderr, "Sending to %i.", fd);

//     if (num)
//     {
//         res = write(fd, data, num * sizeof(int));
//         if (res == -1)
//             fprintf(stderr, "Sending %i bytes to %i.", num * sizeof(int), fd);
//     }
// }

// int *recv_ints(const int fd, int *num)
// {
//     int res;
//     res = read(fd, num, sizeof(int));
//     if (res == -1)
//         fprintf(stderr, "Receiving from %i.", fd);

//     int *data = 0;
//     if (*num)
//     {
//         data = (int *)malloc(*num * sizeof(int));
//         res = read(fd, data, sizeof(int) * *num);
//         if (res == -1)
//             fprintf(stderr, "Receiving %d bytes from %i.", sizeof(int) * *num, fd);
//     }
//     return data;
// }