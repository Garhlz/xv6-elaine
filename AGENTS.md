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

Follow `.editorconfig`: LF endings, final newline, spaces by default, 4-space indentation for C and headers, 8-space indentation for assembly, and tabs in `Makefile`. Match xv6 C style: small functions, simple control flow, lowercase identifiers, and minimal abstraction. Kernel entry points and helpers belong in matching subsystem files, for example `kernel/sysnet.c` for network syscalls and `kernel/e1000.c` for E1000 driver work. User programs should be named `user/name.c` and added to `UPROGS` as `$U/_name`.

## Testing Guidelines

Use `make grade` before submitting lab work. For focused manual checks, run `make qemu` and execute xv6 commands such as `nettests` at the xv6 shell. The net lab grader starts `make server` and checks output patterns including ping, single-process pings, multi-process pings, and DNS. Keep test output deterministic; graders match exact lines.

## Commit & Pull Request Guidelines

Recent history uses short, imperative or descriptive commit subjects such as `finish lab net` and `fix ld warning: undefined symbol _entry`. Keep commits focused on one lab task or bug fix. Pull requests should state the lab or subsystem changed, summarize behavior, list validation commands run, and mention any known limitations. Include terminal output only when it helps explain a failure or non-obvious result.

## Agent-Specific Instructions

### Branch & Workspace Awareness

- The integration branch is **`dev/all`** (based on `net`). Always verify with `git branch` before editing.
- Never work directly on lab branches (`util`, `syscall`, `pgtbl`, `net`, etc.) — those are reference implementations checked out from upstream.

### Code Hygiene (headers, formatting, diagnostics)

- After editing any header, run `clangd --check=<file>` to catch forward-reference or missing-type errors. Headers must be **self-contained**: include their own `#include` dependencies (e.g. `types.h` for `uint64`).
- Before every commit, run `clang-format -i` on every changed `.c` and `.h` file. The repository `.clang-format` uses 4-space indent, LLVM base style.

### Lab Migration Workflow

When migrating a lab from its reference branch to `dev/all`:

1. `git diff net..<lab> --name-only` to list the functional files for that lab.
2. **Reference the original branch implementation** — use `git show <lab>:<file>` to read the exact working code from the lab branch. Copy its logic faithfully, especially subtle details like macro definitions and conditionals.
3. Migrate only the **lab-specific functional code** — skip formatting noise, toolchain config (`.clang-format`, `.clangd`, `compile_commands.json`), and temporary files.
3. Remove `#ifdef LAB_*` guards for the migrated lab; `dev/all` integrates all labs unconditionally.
4. **Do not modify grader expectations** to match local file differences. Instead, add stable test data (e.g. the original `README` file) so the grader stays unchanged.
5. Run the grader (`make grade-<lab>` or `make grade-all`) and confirm all tests pass.
6. After all changes, sync the migration status in `docs/lab-migration-plan.md` and `README.md`.

### Commit Format

- Format: `feat(<lab>): integrate <lab> lab changes` followed by `- ` bullet points with **no blank lines** between them.
- Do **not** add `Co-Authored-By: Claude`.

### Grading Conventions

- `make grade-all` order: **util → syscall → net → pgtbl → traps → cow → thread → lock → fs → mmap**.
- `conf/lab.mk` keeps `LAB=net` with a comment explaining that `dev/all` integrates multiple labs; this ensures net-specific kernel objects and QEMU flags are always active.
- **Remove `time.txt` checks** from all graders. The `@test(1, "time")` / `check_time()` block in every grader script should be deleted — `time.txt` is a course hand-in artifact that is irrelevant for `dev/all`.
