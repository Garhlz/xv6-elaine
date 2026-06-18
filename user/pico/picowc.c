#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static char buf[512];

static int wc(int fd, const char *file_name) {
    int char_cnt = 0, word_cnt = 0, line_cnt = 0;
    int n;
    int in_word = 0;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            char_cnt++;
            if (buf[i] == '\n')
                line_cnt++;
            if (isspace(buf[i]))
                in_word = 0;
            else if (in_word == 0) { // 当前字符不是空白符，而且不在word中
                word_cnt++;
                in_word = 1;
            }
        }
    }
    // n == 0， EOF
    if (n < 0) {
        fprintf(stderr, "picowc: read error\n");
        return -1;
    }

    printf("%d %d %d %s\n", line_cnt, word_cnt, char_cnt, file_name);
    return 0;
}

int main(int argc, char **argv) {
    if (argc <= 1) {
        wc(STDIN_FILENO, "STDIN");
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "picowc: cannot open %s\n", argv[i]);
            return 1;
        }
        if (wc(fd, argv[i]) < 0) {
            close(fd);
            return 1;
        }
        // 注意关闭文件
        close(fd);
    }
    return 0;
}
