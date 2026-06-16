#include "types.h"
#include "param.h"
#include "syscall.h"
#include "syscall_internal.h"
#include "defs.h"

extern uint64 sys_chdir(void);
extern uint64 sys_close(void);
extern uint64 sys_dup(void);
extern uint64 sys_exec(void);
extern uint64 sys_exit(void);
extern uint64 sys_fork(void);
extern uint64 sys_fstat(void);
extern uint64 sys_getpid(void);
extern uint64 sys_kill(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_mknod(void);
extern uint64 sys_open(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_unlink(void);
extern uint64 sys_wait(void);
extern uint64 sys_write(void);
extern uint64 sys_uptime(void);
extern uint64 sys_trace(void);
extern uint64 sys_sysinfo(void);
extern uint64 sys_connect(void);
extern uint64 sys_pgaccess(void);
extern uint64 sys_sigalarm(void);
extern uint64 sys_sigreturn(void);
extern uint64 sys_symlink(void);
extern uint64 sys_mmap(void);
extern uint64 sys_munmap(void);

// 系统调用表：按系统调用编号索引到对应名称和处理函数。
// 未初始化项为 0，表示非法 syscall 编号。
const struct syscall_entry syscall_table[] = {
    [SYS_fork] = {"fork", sys_fork},
    [SYS_exit] = {"exit", sys_exit},
    [SYS_wait] = {"wait", sys_wait},
    [SYS_pipe] = {"pipe", sys_pipe},
    [SYS_read] = {"read", sys_read},
    [SYS_kill] = {"kill", sys_kill},
    [SYS_exec] = {"exec", sys_exec},
    [SYS_fstat] = {"fstat", sys_fstat},
    [SYS_chdir] = {"chdir", sys_chdir},
    [SYS_dup] = {"dup", sys_dup},
    [SYS_getpid] = {"getpid", sys_getpid},
    [SYS_sbrk] = {"sbrk", sys_sbrk},
    [SYS_sleep] = {"sleep", sys_sleep},
    [SYS_uptime] = {"uptime", sys_uptime},
    [SYS_open] = {"open", sys_open},
    [SYS_write] = {"write", sys_write},
    [SYS_mknod] = {"mknod", sys_mknod},
    [SYS_unlink] = {"unlink", sys_unlink},
    [SYS_link] = {"link", sys_link},
    [SYS_mkdir] = {"mkdir", sys_mkdir},
    [SYS_close] = {"close", sys_close},
    [SYS_trace] = {"trace", sys_trace},
    [SYS_sysinfo] = {"sysinfo", sys_sysinfo},
    [SYS_sigalarm] = {"sigalarm", sys_sigalarm},
    [SYS_sigreturn] = {"sigreturn", sys_sigreturn},
    [SYS_symlink] = {"symlink", sys_symlink},
    [SYS_mmap] = {"mmap", sys_mmap},
    [SYS_munmap] = {"munmap", sys_munmap},
    [SYS_connect] = {"connect", sys_connect},
    [SYS_pgaccess] = {"pgaccess", sys_pgaccess},
};

const int syscall_table_size = NELEM(syscall_table);
