// user/xargs.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define MAX_LINE_LEN 512

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(2, "xargs: argument number error\n");
        exit(1);
    }

    char *command = argv[1];
    char *new_argv[MAXARG];
    int new_argc = 0;

    // 将 xargs 自身的参数（除了 xargs 本身）复制到新的参数列表
    for (int i = 1; i < argc; i++)
    {
        new_argv[new_argc++] = argv[i];
    }

    char line_buf[MAX_LINE_LEN];
    char *p = line_buf;

    // 从标准输入读入到字符数组中， 使用指针
    while (read(0, p, 1) > 0)
    {
        // 3. 遇到换行符，说明一行读取完毕
        if (*p == '\n')
        {
            *p = '\0';
            // 开始解析行内参数
            char *line_p = line_buf;
            while (*line_p)
            {
                // 跳过前导空格
                while (*line_p == ' ')
                    line_p++;
                if (*line_p == '\0')
                    break;

                // 记录单词开头， 直接把指针赋值过去
                if (new_argc < MAXARG - 1)
                {
                    new_argv[new_argc++] = line_p;
                }

                // 找到单词结尾
                while (*line_p != ' ' && *line_p != '\0')
                {
                    line_p++;
                }

                // 用'\0'截断单词
                if (*line_p == ' ')
                {
                    *line_p = '\0';
                    line_p++;
                }
            }

            new_argv[new_argc] = 0;

            if (fork() == 0)
            { // Child process
                exec(command, new_argv);
                fprintf(2, "xargs: exec %s failed\n", command);
                exit(1);
            }
            else
            { // Parent process
                wait(0);
            }

            // 重置状态，为下一行做准备
            p = line_buf;        // 重置行缓冲区的指针， 注意缓冲区没有更改， 直接覆盖了
            new_argc = argc - 1; // 重置参数计数，只保留 xargs 初始的那些参数， 此处也是直接覆盖了new_argv字符指针数组
        }
        else
        {
            // 如果没到行尾，且缓冲区未满，则继续读取下一个字符
            if (p - line_buf < MAX_LINE_LEN - 1)
            {
                p++;
            }
        }
    }

    exit(0);
}