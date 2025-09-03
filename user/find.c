// hw4 find
#include <kernel/types.h>
#include <kernel/stat.h>
#include <user/user.h>
#include "kernel/fs.h"
// 安全地处理源地址和目标地址有内存重叠的情况
// void *
// memmove(void *vdst, const void *vsrc, int n)
// {
//     char *dst;
//     const char *src;

//     dst = vdst;
//     src = vsrc;
//     if (src > dst)
//     {
//         while (n-- > 0)
//             *dst++ = *src++;
//     }
//     else
//     {
//         dst += n;
//         src += n;
//         while (n-- > 0)
//             *--dst = *--src;
//     }
//     return vdst;
// }

char *
fmtname(char *path) // 从完整路径中提取出文件名，如果文件名不够长，用空格填充到固定宽度。
{
    static char buf[DIRSIZ + 1];
    char *p;

    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // Return blank-padded name.
    if (strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    memset(buf + strlen(p), ' ', DIRSIZ - strlen(p));
    return buf;
}

void find(char *path, char *str)
{
    int fd;
    char buf[512];
    char *p; // 表示字符串当前位置的指针
    struct dirent de;
    struct stat st;
    if ((fd = open(path, 0)) < 0)
    {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) // fstat是从fd文件描述符中获得其stat信息的
    {
        fprintf(2, "find: cannot stat %s\n", path);
        return;
    }
    if (st.type == T_FILE)
    {
        fprintf(2, "find: cannot open a file %s\n", path);
        return;
    }

    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)
    {
        printf("find: path too long\n");
        return;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';

    while (read(fd, &de, sizeof(de)) == sizeof(de)) // 循环读取目录下的文件， 解析为dirent结构体
    {
        if (de.inum == 0)
            continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        // buf是可以读取的完整地址，已经拼接好了
        // de.name是文件名
        if (stat(buf, &st) < 0) // stat是从一个路径中读取和解析文件， 解析为stat结构体
        {
            printf("find: cannot stat %s\n", buf);
            continue;
        }

        if (st.type == T_FILE && strcmp(de.name, str) == 0)
        {
            printf("%s\n", buf);
        }
        else if (st.type == T_DIR)
        {
            if ((strcmp(de.name, ".") == 0 || (strcmp(de.name, "..") == 0)))
                continue;
            find(buf, str); // 递归调用即可
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(2, "find: wrong argument number\n");
        exit(1);
    }
    char *path = argv[1];
    char *str = argv[2];
    find(path, str);
    exit(0);
}
