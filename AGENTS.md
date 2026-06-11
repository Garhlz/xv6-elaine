# Repository Guidelines

## Project Structure & Module Organization

This repository is the xv6 RISC-V teaching OS with the current lab set to `net` in `conf/lab.mk`. Kernel code lives in `kernel/`, user programs and tests live in `user/`, and the host-side filesystem image builder is in `mkfs/`. Lab configuration is under `conf/`; grading and helper scripts are at the root, including `grade-lab-net`, `gradelib.py`, `server.py`, and `ping.py`. Generated artifacts such as `fs.img`, `kernel/kernel`, `*.o`, `*.asm`, and `*.sym` should not be treated as source changes.

## Build, Test, and Development Commands

- `make qemu`: build xv6, create `fs.img`, and boot in QEMU.
- `make qemu-gdb`: boot QEMU paused; run `gdb` in another terminal.
- `make grade`: clean, rebuild, and run the active lab grader, currently `./grade-lab-net`.
- `make server`: start the UDP echo server used by the net lab grader.
- `make ping`: send a host-side UDP ping to the forwarded xv6 port.
- `make clean`: remove generated kernel, user, image, and debug artifacts.

Builds expect a RISC-V GCC/binutils toolchain and `qemu-system-riscv64` in `PATH`.

## Coding Style & Naming Conventions

Follow `.editorconfig`: LF endings, final newline, spaces by default, 2-space indentation for C and headers, 8-space indentation for assembly, and tabs in `Makefile`. Match xv6 C style: small functions, simple control flow, lowercase identifiers, and minimal abstraction. Kernel entry points and helpers belong in matching subsystem files, for example `kernel/sysnet.c` for network syscalls and `kernel/e1000.c` for E1000 driver work. User programs should be named `user/name.c` and added to `UPROGS` as `$U/_name`.

## Testing Guidelines

Use `make grade` before submitting lab work. For focused manual checks, run `make qemu` and execute xv6 commands such as `nettests` at the xv6 shell. The net lab grader starts `make server` and checks output patterns including ping, single-process pings, multi-process pings, and DNS. Keep test output deterministic; graders match exact lines.

## Commit & Pull Request Guidelines

Recent history uses short, imperative or descriptive commit subjects such as `finish lab net` and `fix ld warning: undefined symbol _entry`. Keep commits focused on one lab task or bug fix. Pull requests should state the lab or subsystem changed, summarize behavior, list validation commands run, and mention any known limitations. Include terminal output only when it helps explain a failure or non-obvious result.

## Agent-Specific Instructions

Do not overwrite generated lab artifacts unless a build command regenerates them. Prefer small patches that preserve xv6’s teaching-oriented clarity over broad refactors.
