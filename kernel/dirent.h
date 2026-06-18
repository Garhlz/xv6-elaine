#ifndef XV6_DIRENT_H
#define XV6_DIRENT_H

#include "types.h"
#include "fs.h"

// getdents() 输出给用户态的目录项视图。
// 这不是磁盘上的 struct dirent；磁盘格式仍保持 inum + name[DIRSIZ]。
struct xv6_dent {
    ushort d_ino;
    ushort d_reclen;
    uchar d_type;
    char d_name[DIRSIZ + 1];
};

#endif
