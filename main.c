#include <stdio.h>
#include <getopt.h>

#include "main.h"

int main(int argc, char **argv)
{
    int ch;
    while ((ch = getopt(argc, argv, "skn:")) != -1)
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
            submit_job();
            break;
        }
    }

    close_socket();

    return 0;
}
