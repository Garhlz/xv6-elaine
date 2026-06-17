# Repository Guidelines

## Project Structure & Module Organization

This repository is the xv6 RISC-V teaching OS with the current lab set to `net` in `conf/lab.mk`. All 10 labs (`util` through `mmap`) are integrated into `dev/all`.

### Kernel (`kernel/`)

Kernel sources are organized by subsystem, with the syscall layer split into focused modules:

| Area | Files |
|---|---|
| **process** | `proc.c` / `proc.h`, `exec.c` |
| **VM / memory** | `vm.c`, `kalloc.c` |
| **filesystem** | `fs.c` / `fs.h`, `bio.c`, `log.c`, `file.c` / `file.h` |
| **syscall dispatch** | `syscall.c`, `syscall.h`, `syscall_table.c`, `syscall_internal.h`, `sysarg.c` |
| **syscall handlers** | `sysproc.c`, `sysfd.c`, `sysfile.c`, `sysmmap.c`, `sysnetcall.c` |
| **network** | `net.c` / `net.h`, `e1000.c`, `e1000_dev.h`, `sysnet.c` |
| **drivers** | `virtio_disk.c`, `plic.c`, `uart.c`, `pci.c` |
| **locks** | `spinlock.c` / `spinlock.h`, `sleeplock.c` / `sleeplock.h` |
| **ELF / exec** | `elf.h`, `exec.c` |
| **internal headers** | `fd_internal.h`, `syscall_internal.h` |

### User-space (`user/`)

Two parallel runtime chains coexist:

**Native (xv6 libc) chain** — used by all existing programs and tests:
| File | Purpose |
|---|---|
| `crt0_entry.S` + `crt0.c` | ELF entry point (`_start` → `crt0_main` → `main` → `exit`) |
| `ustring.c` | `strcpy`, `strcmp`, `strlen`, `strchr`, `memmove`, `memcmp`, `memcpy`, `memset` |
| `printf.c` | `printf` / `fprintf` / `vprintf` (subset: `%d%l%x%p%s%c%%`) |
| `umalloc.c` | `malloc` / `free` (K&R implicit free list) |
| `ufile.c` | `stat` via `open` / `fstat` / `close` |
| `ugetpid.c` | `ugetpid` via USYSCALL shared page |
| `usys.py` | generates `usys.S` stubs (`li a7, SYS_*` → `ecall` → `ret`) |
| `usyscall.h` + `ulib.h` | user-side syscall and libc declarations |
| `user.h` | compatibility umbrella header (native chain only) |

**Picolibc experiment chain** (`user/pico/`) — separate, opt-in via `PICOLIBC_EXPERIMENT=1`:
| File | Purpose |
|---|---|
| `crt0_entry.S` + `crt0.c` | picolibc-specific startup |
| `usys_pico.py` | generates `__xv6_*` raw syscall stubs |
| `picolibc_os.c` | OS glue: `_exit`/`_write`/`_read`/`_sbrk`/`_close`/`_fstat`/`_lseek`/`_isatty`/`_open`/`_getpid` |
| `xv6_syscall_raw.h` + `xv6_pico.h` | raw syscall declarations + shared helpers |
| `user_pico.ld` | linker script |
| `picohello.c` | PoC: printf, argc/argv, malloc/free |
| `picoio.c` | verify: open/stat/read/write/close/getpid |
| `picostdio.c` | verify: fopen/fread/fwrite/fclose |
| `picoinit.c` | verify: constructor/destructor |
| `picoecho.c` / `picosleep.c` | program migration PoC |
| `picotime.c` | verify: time/entropy/errno |

### Other directories

| Directory | Purpose |
|---|---|
| `tests/host/` | Go test runner (`xv6test`) — smoke, per-subsystem, heavy suites |
| `graders/` | Legacy Python graders (course reference) |
| `docs/` | `TODO.md` (roadmap), `lab-migration-plan.md`, `test-migrate.md`, `code-refactor.md`, `picolibc.md` |
| `notxv6/` | Host-side pthread exercises (`ph`, `barrier`) |
| `mkfs/` | Filesystem image builder |
| `conf/` | Lab config (`lab.mk`: `LAB=net`) |
| `tools/` | Helper scripts (`gen_compile_commands.sh`) |

## Build, Test, and Development Commands

### Build

- `make build`: compile kernel, user programs (native + optionally picolibc), host tools, and filesystem tools.
- `make image`: build `build/fs.img` only.
- `make clean`: remove `build/` and stale legacy artifacts.

### QEMU

- `make qemu`: boot with normal networking (`-netdev user`).
- `make qemu-net`: boot with UDP host forwarding (`hostfwd=udp::$(FWDPORT)-:2000`).
- `make qemu-gdb` / `make qemu-gdb-net`: boot paused for GDB.

### Testing — Go runner (primary daily workflow)

- `make test-quick`: fastest subset (~8 cases, no DNS / heavy). For rapid iteration.
- `make test-smoke`: default pre-commit check. 40 light cases across all subsystems, per-case QEMU isolation, no DNS or heavy tests.
- `make test-<lab>` (e.g. `make test-thread`, `make test-mmap`): full per-subsystem suite with default heavy exclusion.
- `make test-usertests`: 19 light usertests subtests.
- `make test-heavy`: only heavy-tagged cases (`bigfile`, `sbrkmuch`, `usertests-full`).
- `make test-all`: all suites.
- `make test-picolibc`: picolibc PoC verification (9 programs: picohello, picoio, picostdio, picoinit, picoecho, picosleep, picotime, pico_echo, pico_sleep).

Go runner CLI reference:
```bash
xv6test list [--suite NAME] [--tags TAG,...]
xv6test run --suite NAME [--case NAME] [--tags TAG,...] [--log-dir DIR] [--timeout DURATION]
```

Logs: `build/test-logs/<suite>/<case>.log`.

### Testing — Python grader (legacy reference)

- `make smoke-py`: legacy Python smoke (may not pass on echo/xargs instability).
- `make grade-<lab>`: run one lab's Python grader.
- `make grade-all` / `make grade-all-heavy`: full course regression.

### Networking

- `make server`: start UDP echo server.
- `make ping`: send UDP ping to forwarded xv6 port.

### Picolibc experiment

```bash
# 构建并安装 picolibc (one-time)
make picolibc-configure picolibc-build picolibc-install

# Build with picolibc experiment enabled
make build PICOLIBC_EXPERIMENT=1

# Run picolibc tests
make test-picolibc
```

Required: `meson`, `ninja`, `riscv64-unknown-elf-gcc`. CI note: picolibc experiment is not in default `make build`; gated behind `PICOLIBC_EXPERIMENT=1`.

## Code Organization

### Syscall split conventions

The original monolithic `syscall.c` / `sysfile.c` / `sysproc.c` have been split:

| Module | Contains |
|---|---|
| `syscall_table.c` | `syscall_table[]` 分发表（`struct syscall_entry` 数组，含 `.fn` / `.name`） |
| `sysarg.c` | `argraw`, `argint`, `argaddr`, `argstr`, `fetchaddr`, `fetchstr` |
| `syscall.c` | `syscall()` entry point (thin dispatcher) |
| `sysproc.c` | `exit`, `getpid`, `fork`, `wait`, `sbrk`, `sleep`, `kill`, `uptime`, `trace`, `sysinfo`, `pgaccess`, `sigalarm`, `sigreturn` |
| `sysfd.c` | `argfd`, `fdalloc`, `dup`, `read`, `write`, `close`, `fstat`, `pipe` |
| `sysfile.c` | `link`, `unlink`, `open`, `symlink`, `mkdir`, `mknod`, `chdir`, `exec` |
| `sysmmap.c` | `mmap`, `munmap` |
| `sysnetcall.c` | `connect`, `sockalloc`, `sockclose`, `sockread`, `sockwrite`, `sockrecvudp` |

New kernel `.c` files must be added to `KOBJS` in the `Makefile`.

### Native ulibc split

| File | Contents |
|---|---|
| `ustring.c` | string + memory operations (ex `ulib.c`) |
| `ufile.c` | file stat helper |
| `ugetpid.c` | USYSCALL-based `ugetpid` |
| `ulib.h` | user libc declarations |
| `usyscall.h` | syscall declarations (generated stubs match these) |

New user `.c` files must be added to `ULIB` in the `Makefile`.

## Coding Style & Naming Conventions

Follow `.editorconfig`: LF endings, final newline, spaces by default, 4-space indentation for C and headers, 8-space indentation for assembly, tabs in `Makefile`.

Variable naming: prefer clear, domain-specific names over single-letter abbreviations for non-trivial locals. Acceptable short names: `i`/`j` loop indices, `p` as temporary pointer, `sz` for size, `fd` for file descriptor.

Comments: use Chinese for explanations with English identifiers inline — e.g. "超级块 (superblock)"、"写时复制 (copy-on-write, COW)"、"页表项 (PTE)". Keep code identifiers, struct names, function names, and macros in English.

## Comment Translation & Readability Workflow

Two skills are available for iterative source improvement:

- `/chinese-comment-localizer`: translate existing English comments to Chinese and add learning-oriented density. Use for subsystem files where you want to build understanding (e.g. `kernel/exec.c`, `kernel/elf.h`, `user/ulib.c`).
- `/readability-refactor`: conservative readability pass — rename unclear locals, simplify conditions, add small clarifying comments, without changing behavior. Use after comments are localized.

Conventions:
- Run `clang-format -i` on changed `.c` / `.h` before committing.
- Run `clangd --check=<file>` on changed headers to verify self-containment.
- Keep translation commits separate from logic changes; amend readability tweaks into the same logical commit.

## Testing Guidelines

Layered test targets. Prefer the lightest target that covers your change:

- **Build-only**: `make build && make image`
- **Fast iteration**: `make test-quick`
- **Before every commit**: `make test-smoke`
- **Subsystem changes**: `make test-<lab>` (e.g. `make test-mmap`, `make test-fs`) or `make grade-<lab>` for Python comparison
- **Stage/merge regression**: `make test-heavy` (only heavy cases) or `make grade-all-heavy` (full)

Heavy tests (`bigfile`, `sbrkmuch`, `usertests-full`) are excluded by default in all suites; run them explicitly with `--tags heavy`.

Legacy Python smoke: `make smoke-py`. All graders live under `graders/`.

## Commit & Pull Request Guidelines

- **Lab migration commits**: `feat(<lab>): integrate <lab> lab changes` with `- ` bullets, no blank lines between bullets.
- **Non-lab maintenance commits**: short imperative subjects, e.g. `docs: refresh roadmap`, `build: move artifacts under build`, `refactor: modernize bio.c naming`.

Body format for all commits:

```
type(scope): short subject

- first bullet describing one logical piece
- second bullet describing another logical piece
- no blank lines between bullets; each - line is a self-contained sentence
```

- Keep commits focused on one topic.
- Do **not** add `Co-Authored-By: Claude`.

## Agent-Specific Instructions

### Branch & Workspace Awareness

- Integration branch: **`dev/all`** (based on `net`). Verify with `git branch` before editing.
- Never work directly on lab reference branches (`util`, `syscall`, `pgtbl`, `net`, etc.).

### Kernel Module Hygiene

- New kernel `.c` → add to `KOBJS` in `Makefile`.
- New user `.c` → add to `ULIB` (if library) or `UPROGS` (if program) in `Makefile`.
- After editing any header, run `clangd --check=<file>`.
- Before every commit, run `clang-format -i` on changed `.c` and `.h` files.

### Lab Migration Workflow (historical — all labs migrated)

For reference only; all 10 labs are integrated. See `docs/lab-migration-plan.md`.

### Grading Conventions

- `make grade-all` order: **util → syscall → net → pgtbl → traps → cow → thread → lock → fs → mmap**.
- `conf/lab.mk` keeps `LAB=net` for net-specific kernel objects and QEMU flags.
- Remove `time.txt` checks from all graders.
