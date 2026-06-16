# Picolibc experiment ELF entry.
# exec() passes argc in a0 and argv in a1.

.section .text
.globl _start
_start:
    call pico_crt0_main
1:  j 1b
