#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>

#include "main.h"

int main(int argc, char **argv)
{
    int ch;
    char *op = "sh", *arg = "test.sh";
    char **cmd = (char **)malloc(2 * sizeof(char *));
    cJSON* response;
    char* response_str;
    cmd[0] = op, cmd[1] = arg;
    while ((ch = getopt(argc, argv, "sknc:i:")) != -1)
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
            response = submit_job(cmd);
            response_str = cJSON_Print(response);
            fprintf(stdout, "%s\n", response_str);
            cJSON_Delete(response);
            free(response_str);
            break;
        case 'c':
            response = cancel_job(atoi(optarg));
            response_str = cJSON_Print(response);
            fprintf(stdout, "%s\n", response_str);
            cJSON_Delete(response);
            free(response_str);
            break;
        case 'i':
            response = get_job_info(atoi(optarg));
            response_str = cJSON_Print(response);
            fprintf(stdout, "%s\n", response_str);
            cJSON_Delete(response);
            free(response_str);
            break;
        }
    }

    close_socket();

    return 0;
}
