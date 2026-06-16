#ifndef XV6_SYSCALL_INTERNAL_H
#define XV6_SYSCALL_INTERNAL_H

#include "types.h"

struct syscall_entry {
    const char *name;
    uint64 (*fn)(void);
};

extern const struct syscall_entry syscall_table[];
extern const int syscall_table_size;

#endif
