#include "kernel/param.h"
#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/fs.h"
#include "user/user.h"

void mmap_test(void);
void fork_test(void);
void shared_exit_writeback_test(void);
void read_into_fresh_mmap_test(void);
char buf[BSIZE];

#define MAP_FAILED ((char *)-1)

int main(int argc, char *argv[]) {
    mmap_test();
    fork_test();
    shared_exit_writeback_test();
    read_into_fresh_mmap_test();
    printf("mmaptest: all tests succeeded\n");
    exit(0);
}

char *testname = "???";

void err(char *why) {
    printf("mmaptest: %s failed: %s, pid=%d\n", testname, why, getpid());
    exit(1);
}

void _v1(char *p) {
    int i;
    for (i = 0; i < PGSIZE * 2; i++) {
        if (i < PGSIZE + (PGSIZE / 2)) {
            if (p[i] != 'A') {
                printf("mismatch at %d, wanted 'A', got 0x%x\n", i, p[i]);
                err("v1 mismatch (1)");
            }
        } else {
            if (p[i] != 0) {
                printf("mismatch at %d, wanted zero, got 0x%x\n", i, p[i]);
                err("v1 mismatch (2)");
            }
        }
    }
}

void makefile(const char *f) {
    int i;
    int n = PGSIZE / BSIZE;

    unlink(f);
    int fd = open(f, O_WRONLY | O_CREATE);
    if (fd == -1)
        err("open");
    memset(buf, 'A', BSIZE);
    for (i = 0; i < n + n / 2; i++) {
        if (write(fd, buf, BSIZE) != BSIZE)
            err("write 0 makefile");
    }
    if (close(fd) == -1)
        err("close");
}

void mmap_test(void) {
    int fd;
    int i;
    const char *const f = "mmap.dur";
    printf("mmap_test starting\n");
    testname = "mmap_test";

    makefile(f);
    if ((fd = open(f, O_RDONLY)) == -1)
        err("open");

    printf("test mmap f\n");
    char *p = mmap(0, PGSIZE * 2, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED)
        err("mmap (1)");
    _v1(p);
    if (munmap(p, PGSIZE * 2) == -1)
        err("munmap (1)");

    printf("test mmap f: OK\n");

    printf("test mmap private\n");
    p = mmap(0, PGSIZE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED)
        err("mmap (2)");
    if (close(fd) == -1)
        err("close");
    _v1(p);
    for (i = 0; i < PGSIZE * 2; i++)
        p[i] = 'Z';
    if (munmap(p, PGSIZE * 2) == -1)
        err("munmap (2)");

    printf("test mmap private: OK\n");

    printf("test mmap read-only\n");

    if ((fd = open(f, O_RDONLY)) == -1)
        err("open");
    p = mmap(0, PGSIZE * 3, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p != MAP_FAILED)
        err("mmap call should have failed");
    if (close(fd) == -1)
        err("close");

    printf("test mmap read-only: OK\n");

    printf("test mmap read/write\n");

    if ((fd = open(f, O_RDWR)) == -1)
        err("open");
    p = mmap(0, PGSIZE * 3, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED)
        err("mmap (3)");
    if (close(fd) == -1)
        err("close");

    _v1(p);

    for (i = 0; i < PGSIZE * 2; i++)
        p[i] = 'Z';

    if (munmap(p, PGSIZE * 2) == -1)
        err("munmap (3)");

    printf("test mmap read/write: OK\n");

    printf("test mmap dirty\n");

    if ((fd = open(f, O_RDWR)) == -1)
        err("open");
    for (i = 0; i < PGSIZE + (PGSIZE / 2); i++) {
        char b;
        if (read(fd, &b, 1) != 1)
            err("read (1)");
        if (b != 'Z')
            err("file does not contain modifications");
    }
    if (close(fd) == -1)
        err("close");

    printf("test mmap dirty: OK\n");

    printf("test not-mapped unmap\n");

    if (munmap(p + PGSIZE * 2, PGSIZE) == -1)
        err("munmap (4)");

    printf("test not-mapped unmap: OK\n");

    printf("test mmap two files\n");

    int fd1;
    if ((fd1 = open("mmap1", O_RDWR | O_CREATE)) < 0)
        err("open mmap1");
    if (write(fd1, "12345", 5) != 5)
        err("write mmap1");
    char *p1 = mmap(0, PGSIZE, PROT_READ, MAP_PRIVATE, fd1, 0);
    if (p1 == MAP_FAILED)
        err("mmap mmap1");
    close(fd1);
    unlink("mmap1");

    int fd2;
    if ((fd2 = open("mmap2", O_RDWR | O_CREATE)) < 0)
        err("open mmap2");
    if (write(fd2, "67890", 5) != 5)
        err("write mmap2");
    char *p2 = mmap(0, PGSIZE, PROT_READ, MAP_PRIVATE, fd2, 0);
    if (p2 == MAP_FAILED)
        err("mmap mmap2");
    close(fd2);
    unlink("mmap2");

    if (memcmp(p1, "12345", 5) != 0)
        err("mmap1 mismatch");
    if (memcmp(p2, "67890", 5) != 0)
        err("mmap2 mismatch");

    munmap(p1, PGSIZE);
    if (memcmp(p2, "67890", 5) != 0)
        err("mmap2 mismatch (2)");
    munmap(p2, PGSIZE);

    printf("test mmap two files: OK\n");

    printf("mmap_test: ALL OK\n");
}

void fork_test(void) {
    int fd;
    int pid;
    const char *const f = "mmap.dur";

    printf("fork_test starting\n");
    testname = "fork_test";

    makefile(f);
    if ((fd = open(f, O_RDONLY)) == -1)
        err("open");
    unlink(f);
    char *p1 = mmap(0, PGSIZE * 2, PROT_READ, MAP_SHARED, fd, 0);
    if (p1 == MAP_FAILED)
        err("mmap (4)");
    char *p2 = mmap(0, PGSIZE * 2, PROT_READ, MAP_SHARED, fd, 0);
    if (p2 == MAP_FAILED)
        err("mmap (5)");

    if (*(p1 + PGSIZE) != 'A')
        err("fork mismatch (1)");

    if ((pid = fork()) < 0)
        err("fork");
    if (pid == 0) {
        _v1(p1);
        munmap(p1, PGSIZE);
        exit(0);
    }

    int status = -1;
    wait(&status);

    if (status != 0) {
        printf("fork_test failed\n");
        exit(1);
    }

    _v1(p1);
    _v1(p2);

    printf("fork_test OK\n");
}

void shared_exit_writeback_test(void) {
    int fd;
    int pid;
    int status = -1;
    const char *const f = "mmap.exit";

    printf("shared_exit_writeback_test starting\n");
    testname = "shared_exit_writeback_test";

    unlink(f);
    if ((fd = open(f, O_CREATE | O_RDWR)) < 0)
        err("open");
    memset(buf, 'A', BSIZE);
    if (write(fd, buf, BSIZE) != BSIZE)
        err("write");
    close(fd);

    if ((pid = fork()) < 0)
        err("fork");
    if (pid == 0) {
        if ((fd = open(f, O_RDWR)) < 0)
            err("child open");
        char *p = mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED)
            err("child mmap");
        close(fd);
        p[0] = 'Z';
        p[1] = 'Z';
        exit(0);
    }

    wait(&status);
    if (status != 0)
        err("child failed");

    if ((fd = open(f, O_RDONLY)) < 0)
        err("reopen");
    if (read(fd, buf, 2) != 2)
        err("readback");
    close(fd);
    unlink(f);

    if (buf[0] != 'Z' || buf[1] != 'Z')
        err("exit writeback");

    printf("shared_exit_writeback_test OK\n");
}

void read_into_fresh_mmap_test(void) {
    int src, backing;
    const char *const srcfile = "mmap.src";
    const char *const backingfile = "mmap.backing";

    printf("read_into_fresh_mmap_test starting\n");
    testname = "read_into_fresh_mmap_test";

    unlink(srcfile);
    unlink(backingfile);

    if ((src = open(srcfile, O_CREATE | O_RDWR)) < 0)
        err("open src");
    if (write(src, "hello", 5) != 5)
        err("write src");
    close(src);

    if ((backing = open(backingfile, O_CREATE | O_RDWR)) < 0)
        err("open backing");
    memset(buf, 0, BSIZE);
    if (write(backing, buf, BSIZE) != BSIZE)
        err("write backing");
    close(backing);

    if ((src = open(srcfile, O_RDONLY)) < 0)
        err("reopen src");
    if ((backing = open(backingfile, O_RDWR)) < 0)
        err("reopen backing");

    char *p = mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, backing, 0);
    if (p == MAP_FAILED)
        err("mmap");
    close(backing);

    if (read(src, p, 5) != 5)
        err("read into mmap");
    close(src);

    if (memcmp(p, "hello", 5) != 0)
        err("content mismatch");
    if (munmap(p, PGSIZE) < 0)
        err("munmap");

    unlink(srcfile);
    unlink(backingfile);

    printf("read_into_fresh_mmap_test OK\n");
}
