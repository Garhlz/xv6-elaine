// picodir —— 验证 Picolibc dirent shim 和 stat/fstat 类型映射。

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int is_regular_mode(mode_t mode) {
    return (mode & S_IFMT) == S_IFREG;
}

static int is_directory_mode(mode_t mode) {
    return (mode & S_IFMT) == S_IFDIR;
}

static int check_stat_types(void) {
    struct stat st;
    int fd;

    if (stat("README", &st) < 0 || !is_regular_mode(st.st_mode)) {
        printf("picodir: README stat type failed\n");
        return -1;
    }
    if ((st.st_mode & 0777) != 0644 || st.st_size <= 0) {
        printf("picodir: README stat fields failed\n");
        return -1;
    }

    if (stat(".", &st) < 0 || !is_directory_mode(st.st_mode)) {
        printf("picodir: dot stat type failed\n");
        return -1;
    }
    if ((st.st_mode & 0777) != 0755) {
        printf("picodir: dot mode failed\n");
        return -1;
    }

    fd = open(".", O_RDONLY);
    if (fd < 0) {
        printf("picodir: open dot failed\n");
        return -1;
    }
    if (fstat(fd, &st) < 0 || !is_directory_mode(st.st_mode)) {
        printf("picodir: fstat dot type failed\n");
        close(fd);
        return -1;
    }
    close(fd);
    printf("picodir: stat types OK\n");
    return 0;
}

static int scan_directory(DIR *dir, int *saw_readme, int *saw_sh) {
    struct dirent *entry;

    *saw_readme = 0;
    *saw_sh = 0;

    while ((entry = readdir(dir)) != 0) {
        if (strcmp(entry->d_name, "README") == 0) {
            if (entry->d_type != DT_REG) {
                printf("picodir: README dirent type failed\n");
                return -1;
            }
            *saw_readme = 1;
            continue;
        }
        if (strcmp(entry->d_name, "sh") == 0) {
            if (entry->d_type != DT_REG) {
                printf("picodir: sh dirent type failed\n");
                return -1;
            }
            *saw_sh = 1;
        }
    }
    return 0;
}

static int check_dirent_api(void) {
    DIR *dir;
    int fd;
    int first_fd;
    int second_fd;
    int saw_readme;
    int saw_sh;

    dir = opendir(".");
    if (dir == 0) {
        printf("picodir: opendir failed\n");
        return -1;
    }

    fd = dirfd(dir);
    if (fd < 0) {
        printf("picodir: dirfd failed\n");
        closedir(dir);
        return -1;
    }

    first_fd = fd;
    second_fd = dirfd(dir);
    if (second_fd != first_fd) {
        printf("picodir: dirfd unstable\n");
        closedir(dir);
        return -1;
    }

    if (scan_directory(dir, &saw_readme, &saw_sh) < 0) {
        closedir(dir);
        return -1;
    }
    if (!saw_readme || !saw_sh) {
        printf("picodir: expected entries not found\n");
        closedir(dir);
        return -1;
    }

    rewinddir(dir);
    if (scan_directory(dir, &saw_readme, &saw_sh) < 0) {
        closedir(dir);
        return -1;
    }
    if (!saw_readme || !saw_sh) {
        printf("picodir: rewinddir failed\n");
        closedir(dir);
        return -1;
    }

    if (closedir(dir) < 0) {
        printf("picodir: closedir failed\n");
        return -1;
    }
    printf("picodir: readdir OK\n");
    return 0;
}

static int check_invalid_opendir(void) {
    DIR *dir;

    errno = 0;
    dir = opendir("README");
    if (dir != 0) {
        printf("picodir: file unexpectedly opened as directory\n");
        closedir(dir);
        return -1;
    }
    if (errno != ENOTDIR) {
        printf("picodir: opendir file errno %d\n", errno);
        return -1;
    }
    printf("picodir: invalid opendir OK\n");
    return 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (check_stat_types() < 0)
        return 1;
    if (check_dirent_api() < 0)
        return 1;
    if (check_invalid_opendir() < 0)
        return 1;

    printf("picodir: OK\n");
    return 0;
}
