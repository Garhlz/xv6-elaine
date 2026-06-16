#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char **argv) {
    char buf[8];
    int parent_to_child[2];
    int child_to_parent[2];
    int pid;

    if (argc != 1) {
        fprintf(2, "usage: pingpong\n");
        exit(1);
    }

    if (pipe(parent_to_child) < 0 || pipe(child_to_parent) < 0) {
        fprintf(2, "pingpong: pipe failed\n");
        exit(1);
    }

    pid = fork();
    if (pid < 0) {
        fprintf(2, "pingpong: fork failed\n");
        exit(1);
    }

    if (pid == 0) {
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        if (read(parent_to_child[0], buf, 4) != 4) {
            fprintf(2, "pingpong: child read failed\n");
            exit(1);
        }
        buf[4] = '\0';
        printf("%d: received %s\n", getpid(), buf);
        if (write(child_to_parent[1], "pong", 4) != 4) {
            fprintf(2, "pingpong: child write failed\n");
            exit(1);
        }
        close(parent_to_child[0]);
        close(child_to_parent[1]);
        exit(0);
    }

    close(parent_to_child[0]);
    close(child_to_parent[1]);
    if (write(parent_to_child[1], "ping", 4) != 4) {
        fprintf(2, "pingpong: parent write failed\n");
        exit(1);
    }
    if (read(child_to_parent[0], buf, 4) != 4) {
        fprintf(2, "pingpong: parent read failed\n");
        exit(1);
    }
    buf[4] = '\0';
    printf("%d: received %s\n", getpid(), buf);
    close(parent_to_child[1]);
    close(child_to_parent[0]);
    wait(0);
    exit(0);
}
