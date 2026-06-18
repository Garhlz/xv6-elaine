// picogrep — 使用 picoregex 引擎的 grep PoC。

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "picoregex.h"

#define GREP_BUF_SIZE 1024

// grep_buf 保留跨 read() 的半行内容，避免一行被切开时漏匹配。
static char grep_buf[GREP_BUF_SIZE];

// 临时把当前行补成 C 字符串，匹配完成后恢复原字节。
static void emit_line_if_match(struct Regex *regex, char *line, int line_len, int has_newline) {
    char saved;

    saved = line[line_len];
    line[line_len] = '\0';

    // 如果当前行 match pattern，则将这行写到stdout
    if (regex_search(regex, line)) {
        write(STDOUT_FILENO, line, line_len);
        if (has_newline)
            write(STDOUT_FILENO, "\n", 1);
    }

    line[line_len] = saved;
}

// 从 fd 流式读取内容，按行调用 regex_search()。
int grep(struct Regex *regex, int fd) {
    int buffered_len;
    int read_len;

    buffered_len = 0;

    while ((read_len = read(fd, grep_buf + buffered_len, sizeof(grep_buf) - buffered_len - 1)) >
           0) {
        // 每次读到的长度不一定是nbyte指定的长度

        buffered_len += read_len;
        grep_buf[buffered_len] = '\0';

        char *line_start = grep_buf;

        char *newline;
        // 从buffer中找到第一次出现换行的位置
        while ((newline = strchr(line_start, '\n')) != 0) {
            int line_len;

            /*
             * line_len 不包含 '\n'。
             * newline 指向换行符，所以 line_start 到 newline 之间就是行内容。
             */
            line_len = newline - line_start;

            emit_line_if_match(regex, line_start, line_len, 1);

            line_start = newline + 1;
        }

        /*
         * line_start 之后是没有换行符的残留半行。
         * 把它移动到 buffer 开头，等待下一轮 read 补齐。
         */
        int consumed_len = line_start - grep_buf;
        int remaining_len = buffered_len - consumed_len;

        memmove(grep_buf, line_start, remaining_len);
        buffered_len = remaining_len;
    }

    /*
     * 如果文件最后一行没有 '\n'，这里仍然会留下内容。
     */
    if (buffered_len > 0) {
        emit_line_if_match(regex, grep_buf, buffered_len, 0);
    }
    return 0;
}

int main(int argc, char **argv) {
    struct Regex *regex;
    char *pattern;
    int fd, i;

    if (argc <= 1) {
        fprintf(stderr, "usage: grep pattern [file ...]\n");
        return 1;
    }
    pattern = argv[1];

    regex = malloc(sizeof(*regex));
    if (regex == 0) {
        fprintf(stderr, "grep: malloc failed\n");
        return 1;
    }

    // 先编译一次正则，再复用到所有输入文件。
    if (!regex_compile(pattern, regex)) {
        free(regex);
        return 1;
    }

    // 如果没有输入后面的文件列表，就从stdio中读入
    if (argc <= 2) {
        grep(regex, STDIN_FILENO);
        free(regex);
        return 0;
    }

    for (i = 2; i < argc; i++) {
        if ((fd = open(argv[i], O_RDONLY)) < 0) {
            fprintf(stderr, "grep: cannot open %s\n", argv[i]);
            free(regex);
            return 1;
        }
        grep(regex, fd);
        close(fd);
    }
    free(regex);
    return 0;
}
