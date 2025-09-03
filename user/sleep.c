// hw1 sleep
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if (argc == 1)
    {
        fprintf(2, "sleep: no argument error\n");
        exit(1);
    }
    else if (argc != 2)
    {
        fprintf(2, "sleep: too many arguments error\n");
        exit(1);
    }
    else
    {
        int time_sleep = atoi(argv[1]);
        sleep(time_sleep);
        exit(0);
    }
}