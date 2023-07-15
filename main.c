#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>

#include "main.h"

int main(int argc, char **argv)
{
    int ch;
    char *op = "sleep", *arg = "10000";
    char **cmd = (char **)malloc(3 * sizeof(char *));
    cmd[0] = op, cmd[1] = arg;
    while ((ch = getopt(argc, argv, "sknc:")) != -1)
    {
        switch (ch)
        {
        case 's':
            server_up();
            break;
        case 'k':
            server_down();
            break;
        case 'n':
            submit_job(cmd);
            break;
        case 'c':
            cancel_job(atoi(optarg));
            break;
        }
    }

    close_socket();

    return 0;
}
