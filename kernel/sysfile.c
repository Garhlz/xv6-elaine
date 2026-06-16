//
// 文件系统系统调用 (file-system system calls)。
// 主要做参数校验（不信任用户态代码），校验通过后调用 file.c 和 fs.c 中的底层函数。
//

#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "sysfile_internal.h"

// 创建硬链接 link(old, new): 让 new 指向 old 的同一个 inode。
uint64 sys_link(void) {
    char name[DIRSIZ], new_path[MAXPATH], old_path[MAXPATH];
    struct inode *inode, *parent_dir;

    if (argstr(0, old_path, MAXPATH) < 0 || argstr(1, new_path, MAXPATH) < 0)
        return -1;

    begin_op();
    if ((inode = namei(old_path)) == 0) {
        end_op();
        return -1;
    }

    ilock(inode);
    if (inode->type == T_DIR) {
        // 不允许对目录创建硬链接（避免产生目录环）
        iunlockput(inode);
        end_op();
        return -1;
    }

    inode->nlink++;
    iupdate(inode);
    iunlock(inode);

    // 获取 new_path 的父目录和最终文件名
    if ((parent_dir = nameiparent(new_path, name)) == 0)
        goto bad;
    ilock(parent_dir);
    if (parent_dir->dev != inode->dev || dirlink(parent_dir, name, inode->inum) < 0) {
        iunlockput(parent_dir);
        goto bad;
    }
    iunlockput(parent_dir);
    iput(inode);

    end_op();
    return 0;

bad:
    // 回滚: 恢复 inode 的 nlink
    ilock(inode);
    inode->nlink--;
    iupdate(inode);
    iunlockput(inode);
    end_op();
    return -1;
}

// 检查目录 dir 是否为空（只有 "." 和 ".."）。
// 从第 3 个 dirent（偏移 2*sizeof(dirent)）开始扫描，因为前两个一定是 "." 和 ".."。
static int isdirempty(struct inode *dir) {
    uint entry_size = sizeof(struct dirent);
    struct dirent entry;

    for (uint offset = 2 * entry_size; offset < dir->size; offset += entry_size) {
        if (readi(dir, 0, (uint64)&entry, offset, entry_size) != entry_size)
            panic("isdirempty: readi");
        if (entry.inum != 0) // 找到非空闲条目 → 目录非空
            return 0;
    }
    return 1;
}

// unlink(path): 删除路径 path 指向的文件/目录的目录项。
uint64 sys_unlink(void) {
    struct inode *inode, *parent_dir;
    struct dirent entry;
    char name[DIRSIZ], path[MAXPATH];
    uint entry_offset;

    if (argstr(0, path, MAXPATH) < 0)
        return -1;

    begin_op();
    if ((parent_dir = nameiparent(path, name)) == 0) {
        end_op();
        return -1;
    }

    ilock(parent_dir);

    // 不允许删除 "." 或 ".."
    if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
        goto bad;

    if ((inode = dirlookup(parent_dir, name, &entry_offset)) == 0)
        goto bad;
    ilock(inode);

    if (inode->nlink < 1)
        panic("unlink: nlink < 1");
    // 不允许删除非空目录
    if (inode->type == T_DIR && !isdirempty(inode)) {
        iunlockput(inode);
        goto bad;
    }

    // 将父目录中的 dirent 清零（标记为删除）
    memset(&entry, 0, sizeof(entry));
    if (writei(parent_dir, 0, (uint64)&entry, entry_offset, sizeof(entry)) != sizeof(entry))
        panic("unlink: writei");
    // 目录的 nlink 包含了 ".." 条目——删除子目录时递减父目录的链接数
    if (inode->type == T_DIR) {
        parent_dir->nlink--;
        iupdate(parent_dir);
    }
    iunlockput(parent_dir);

    inode->nlink--;
    iupdate(inode);
    iunlockput(inode);

    end_op();
    return 0;

bad:
    iunlockput(parent_dir);
    end_op();
    return -1;
}

// 在路径 path 处创建一个类型为 type 的新 inode。
// 如果路径已存在且 type==T_FILE，则返回已有 inode（用于 open(O_CREATE) 的语义）。
// major/minor 仅对 T_DEVICE 类型有意义。
static struct inode *create(char *path, short type, short major, short minor) {
    struct inode *inode, *parent_dir;
    char name[DIRSIZ];

    if ((parent_dir = nameiparent(path, name)) == 0)
        return 0;

    ilock(parent_dir);

    // 路径已存在？若 type==T_FILE 且已有文件为 T_FILE/T_DEVICE，直接返回
    if ((inode = dirlookup(parent_dir, name, 0)) != 0) {
        iunlockput(parent_dir);
        ilock(inode);
        if (type == T_FILE && (inode->type == T_FILE || inode->type == T_DEVICE))
            return inode;
        iunlockput(inode);
        return 0;
    }

    // 分配新 inode
    if ((inode = ialloc(parent_dir->dev, type)) == 0)
        panic("create: ialloc");

    ilock(inode);
    inode->major = major;
    inode->minor = minor;
    inode->nlink = 1;
    iupdate(inode);

    // 目录需要初始化 "." 和 ".." 两个条目
    if (type == T_DIR) {
        parent_dir->nlink++; // 新建的子目录中 ".." 指向父目录，父目录 nlink+1
        iupdate(parent_dir);
        // 注意: inode->nlink 不因 "." 递增——避免循环引用计数
        if (dirlink(inode, ".", inode->inum) < 0 || dirlink(inode, "..", parent_dir->inum) < 0)
            panic("create dots");
    }

    // 在父目录中添加新目录项
    if (dirlink(parent_dir, name, inode->inum) < 0)
        panic("create: dirlink");

    iunlockput(parent_dir);
    return inode;
}

// 打开路径 path，跟随符号链接（最多 10 层）。
// omode 中含 O_NOFOLLOW 时，不跟随符号链接，直接返回符号链接自身的 inode。
static struct inode *open_path(char *path, int omode) {
    char target[MAXPATH];
    struct inode *inode;

    if ((inode = namei(path)) == 0)
        return 0;

    // O_NOFOLLOW: 不跟随符号链接
    if (omode & O_NOFOLLOW) {
        ilock(inode);
        return inode;
    }

    // 逐层跟随符号链接，最多 10 层（防止环路）
    for (int depth = 0; depth < 10; depth++) {
        ilock(inode);
        if (inode->type != T_SYMLINK)
            return inode; // 不是符号链接，直接返回

        // 读取符号链接指向的目标路径
        int nbytes = readi(inode, 0, (uint64)target, 0, inode->size);
        if (nbytes != inode->size || nbytes >= MAXPATH) {
            iunlockput(inode);
            return 0;
        }
        target[nbytes] = '\0';
        iunlockput(inode);

        // 解析目标路径
        if ((inode = namei(target)) == 0)
            return 0;
    }

    // 超过最大层数
    iput(inode);
    return 0;
}

// open(path, flags): 打开（或创建）文件，返回文件描述符。
uint64 sys_open(void) {
    char path[MAXPATH];
    int fd, omode;
    struct file *file;
    struct inode *inode;

    if (argstr(0, path, MAXPATH) < 0 || argint(1, &omode) < 0)
        return -1;

    begin_op();

    if (omode & O_CREATE) {
        // O_CREATE: 文件不存在则创建
        inode = create(path, T_FILE, 0, 0);
        if (inode == 0) {
            end_op();
            return -1;
        }
    } else {
        // 普通打开: 跟随符号链接
        inode = open_path(path, omode);
        if (inode == 0) {
            end_op();
            return -1;
        }
        // 不允许以写入模式打开目录
        if (inode->type == T_DIR && omode != O_RDONLY) {
            iunlockput(inode);
            end_op();
            return -1;
        }
    }

    // 校验设备号范围
    if (inode->type == T_DEVICE && (inode->major < 0 || inode->major >= NDEV)) {
        iunlockput(inode);
        end_op();
        return -1;
    }

    // 分配 file 结构和 fd 编号
    if ((file = filealloc()) == 0 || (fd = fdalloc(file)) < 0) {
        if (file)
            fileclose(file);
        iunlockput(inode);
        end_op();
        return -1;
    }

    // 填充 file 结构
    if (inode->type == T_DEVICE) {
        file->type = FD_DEVICE;
        file->major = inode->major;
    } else {
        file->type = FD_INODE;
        file->off = 0; // 普通文件从偏移 0 开始
    }
    file->ip = inode;
    file->readable = !(omode & O_WRONLY);
    file->writable = (omode & O_WRONLY) || (omode & O_RDWR);

    // O_TRUNC: 截断文件（仅在普通文件有效）
    if ((omode & O_TRUNC) && inode->type == T_FILE)
        itrunc(inode);

    iunlock(inode);
    end_op();

    return fd;
}

// symlink(target, path): 在 path 处创建一个指向 target 的符号链接。
// 符号链接本身是一个 type==T_SYMLINK 的 inode，其数据内容为目标路径字符串。
uint64 sys_symlink(void) {
    char target[MAXPATH], path[MAXPATH];
    struct inode *inode;

    int nbytes = argstr(0, target, MAXPATH);
    if (nbytes < 0 || argstr(1, path, MAXPATH) < 0)
        return -1;

    begin_op();
    if ((inode = create(path, T_SYMLINK, 0, 0)) == 0) {
        end_op();
        return -1;
    }

    // 将目标路径字符串写入符号链接 inode 的数据区
    if (writei(inode, 0, (uint64)target, 0, nbytes) != nbytes) {
        iunlockput(inode);
        end_op();
        return -1;
    }

    iunlockput(inode);
    end_op();
    return 0;
}

// mkdir(path): 创建目录。
uint64 sys_mkdir(void) {
    char path[MAXPATH];
    struct inode *inode;

    begin_op();
    if (argstr(0, path, MAXPATH) < 0 || (inode = create(path, T_DIR, 0, 0)) == 0) {
        end_op();
        return -1;
    }
    iunlockput(inode);
    end_op();
    return 0;
}

// mknod(path, major, minor): 创建设备文件。
uint64 sys_mknod(void) {
    struct inode *inode;
    char path[MAXPATH];
    int major, minor;

    begin_op();
    if (argstr(0, path, MAXPATH) < 0 || argint(1, &major) < 0 || argint(2, &minor) < 0 ||
        (inode = create(path, T_DEVICE, major, minor)) == 0) {
        end_op();
        return -1;
    }
    iunlockput(inode);
    end_op();
    return 0;
}

// chdir(path): 切换当前工作目录。
uint64 sys_chdir(void) {
    char path[MAXPATH];
    struct inode *inode;
    struct proc *proc = myproc();

    begin_op();
    if (argstr(0, path, MAXPATH) < 0 || (inode = namei(path)) == 0) {
        end_op();
        return -1;
    }
    ilock(inode);
    if (inode->type != T_DIR) {
        iunlockput(inode);
        end_op();
        return -1;
    }
    iunlock(inode);
    iput(proc->cwd); // 释放旧 cwd 的引用
    end_op();
    proc->cwd = inode; // 设置新 cwd（已通过 namei 获得引用）
    return 0;
}

// exec(path, argv): 加载并执行新程序。
uint64 sys_exec(void) {
    char path[MAXPATH], *argv[MAXARG];
    uint64 uargv, uarg;

    if (argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0)
        return -1;

    // 从用户态拷贝 argv 数组到内核
    memset(argv, 0, sizeof(argv));
    for (int i = 0;; i++) {
        if (i >= NELEM(argv))
            goto bad;
        // 读取用户态 argv[i] 的指针值
        if (fetchaddr(uargv + sizeof(uint64) * i, (uint64 *)&uarg) < 0)
            goto bad;
        if (uarg == 0) {
            argv[i] = 0;
            break; // argv 数组以 NULL 结尾
        }
        // 分配内核页面，将用户态字符串拷入
        argv[i] = kalloc();
        if (argv[i] == 0)
            goto bad;
        if (fetchstr(uarg, argv[i], PGSIZE) < 0)
            goto bad;
    }

    int ret = exec(path, argv);

    // 释放 argv 中分配的内核页面
    for (int i = 0; i < NELEM(argv) && argv[i] != 0; i++)
        kfree(argv[i]);
    return ret;

bad:
    for (int i = 0; i < NELEM(argv) && argv[i] != 0; i++)
        kfree(argv[i]);
    return -1;
}
