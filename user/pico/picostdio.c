#include <stdio.h>

int main(int argc, char **argv) {
    const char *path;
    char buf[64];
    FILE *file;
    size_t n;

    path = argc > 1 ? argv[1] : "README";

    file = fopen(path, "r");
    if (file == NULL) {
        printf("fopen %s failed\n", path);
        return 1;
    }

    n = fread(buf, 1, sizeof(buf), file);
    if (fclose(file) != 0) {
        printf("fclose %s failed\n", path);
        return 1;
    }

    printf("fread %ld bytes from %s\n", (long)n, path);
    fwrite(buf, 1, n, stdout);
    printf("\n");

    return 0;
}
