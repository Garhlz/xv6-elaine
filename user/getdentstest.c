#include "kernel/dirent.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "user/user.h"

static int entry_name_eq(struct xv6_dent *dent, const char *name) {
    return strcmp(dent->d_name, name) == 0;
}

static int find_entry(const char *dir_path, const char *name, int *type_out) {
    struct xv6_dent dents[4];
    int dent_size;
    int fd;
    int n;

    dent_size = sizeof(dents[0]);
    fd = open(dir_path, O_RDONLY);
    if (fd < 0)
        return -1;

    while ((n = getdents(fd, dents, sizeof(dents))) > 0) {
        if (n % dent_size != 0) {
            close(fd);
            return -1;
        }
        for (int i = 0; i < n / dent_size; i++) {
            if (dents[i].d_reclen != dent_size || dents[i].d_ino == 0) {
                close(fd);
                return -1;
            }
            if (entry_name_eq(&dents[i], name)) {
                if (type_out)
                    *type_out = dents[i].d_type;
                close(fd);
                return 1;
            }
        }
    }

    close(fd);
    return n < 0 ? -1 : 0;
}

static int test_root_contains_readme(void) {
    int type;

    if (find_entry(".", "README", &type) != 1) {
        printf("getdentstest: README not found\n");
        return -1;
    }
    if (type != T_FILE) {
        printf("getdentstest: README type %d, want %d\n", type, T_FILE);
        return -1;
    }
    return 0;
}

static int test_created_types(void) {
    int fd;
    int type;

    unlink("gdt_file");
    unlink("gdt_dir");

    if (mkdir("gdt_dir") < 0) {
        printf("getdentstest: mkdir failed\n");
        return -1;
    }

    fd = open("gdt_file", O_CREATE | O_RDWR | O_TRUNC);
    if (fd < 0) {
        printf("getdentstest: create file failed\n");
        unlink("gdt_dir");
        return -1;
    }
    close(fd);

    if (find_entry(".", "gdt_dir", &type) != 1 || type != T_DIR) {
        printf("getdentstest: directory type mismatch\n");
        unlink("gdt_file");
        unlink("gdt_dir");
        return -1;
    }
    if (find_entry(".", "gdt_file", &type) != 1 || type != T_FILE) {
        printf("getdentstest: file type mismatch\n");
        unlink("gdt_file");
        unlink("gdt_dir");
        return -1;
    }

    unlink("gdt_file");
    unlink("gdt_dir");
    return 0;
}

static int test_invalid_inputs(void) {
    char small[sizeof(struct xv6_dent) - 1];
    struct xv6_dent dent;
    int fd;
    int n;

    fd = open("README", O_RDONLY);
    if (fd < 0)
        return -1;
    if (getdents(fd, &dent, sizeof(dent)) != -1) {
        printf("getdentstest: file fd unexpectedly accepted\n");
        close(fd);
        return -1;
    }
    close(fd);

    fd = open(".", O_RDONLY);
    if (fd < 0)
        return -1;
    n = getdents(fd, small, sizeof(small));
    close(fd);
    if (n != 0) {
        printf("getdentstest: small buffer got %d\n", n);
        return -1;
    }
    return 0;
}

static int test_offset_progress(void) {
    struct xv6_dent dent;
    int dent_size;
    int fd;
    int count;
    int saw_readme;
    int n;

    dent_size = sizeof(dent);
    fd = open(".", O_RDONLY);
    if (fd < 0)
        return -1;

    count = 0;
    saw_readme = 0;
    while ((n = getdents(fd, &dent, sizeof(dent))) > 0) {
        if (n != dent_size || dent.d_reclen != dent_size) {
            close(fd);
            return -1;
        }
        count++;
        if (entry_name_eq(&dent, "README"))
            saw_readme = 1;
    }
    close(fd);

    if (n < 0 || count < 2 || !saw_readme) {
        printf("getdentstest: offset progress failed\n");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (test_root_contains_readme() < 0)
        return 1;
    if (test_created_types() < 0)
        return 1;
    if (test_invalid_inputs() < 0)
        return 1;
    if (test_offset_progress() < 0)
        return 1;

    printf("getdentstest: OK\n");
    return 0;
}
