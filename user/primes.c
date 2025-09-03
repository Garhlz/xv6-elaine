// hw3 primes
#include <kernel/types.h>
#include <kernel/stat.h>
#include <user/user.h>

void filter_prime(int fd_read)
{
    int prime;
    if (read(fd_read, &prime, sizeof(int)) != sizeof(int))
        exit(0);

    printf("prime %d\n", prime);
    int num;
    int fds[2];

    pipe(fds);
    int pid = fork();
    if (pid > 0)
    {
        close(fds[0]);
        while (read(fd_read, &num, sizeof(int)) > 0)
        {
            if (num % prime != 0)
                write(fds[1], &num, sizeof(int)); // 筛选一遍
        }
        close(fds[1]);
        // 关闭管道的所有输入文件描述符之后， 会自动发出一个EOF, read的返回值是0
        wait(0);
        exit(0);
    }
    else if (pid == 0)
    {
        close(fds[1]);
        filter_prime(fds[0]);
    }
}

int main(int argc, char *argv[])
{
    int fds[2];
    pipe(fds);
    int pid = fork();
    if (pid > 0)
    {
        close(fds[0]);
        for (int i = 2; i <= 35; i++)
        {
            write(fds[1], &i, sizeof(int));
        }
        close(fds[1]);
        wait(0);
        // 父进程必须等待子进程结束之后才可以退出， 否则子进程会变成孤儿
        exit(0);
    }
    else if (pid == 0)
    {
        close(fds[1]);
        filter_prime(fds[0]);
    }
}