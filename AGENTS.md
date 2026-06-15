# Repository Guidelines

## Project Structure & Module Organization

This repository is the xv6 RISC-V teaching OS with the current lab set to `net` in `conf/lab.mk`. Kernel code lives in `kernel/`, user programs and tests live in `user/`, and the host-side filesystem image builder source is in `mkfs/`. Lab configuration is under `conf/`; grader scripts live in `graders/`, while helper scripts such as `gradelib.py`, `server.py`, and `ping.py` remain at the root. Generated artifacts live under `build/` and should not be treated as source changes.

## Build, Test, and Development Commands

- `make qemu`: build xv6, create `build/fs.img`, and boot in QEMU.
- `make qemu-gdb`: boot QEMU paused; run `gdb` in another terminal.
- `make grade`: clean, rebuild, and run the active lab grader, currently `graders/grade-lab-net`.
- `make server`: start the UDP echo server used by the net lab grader.
- `make ping`: send a host-side UDP ping to the forwarded xv6 port.
- `make clean`: remove generated kernel, user, image, and debug artifacts.

Builds expect a RISC-V GCC/binutils toolchain and `qemu-system-riscv64` in `PATH`.

## Coding Style & Naming Conventions

Follow `.editorconfig`: LF endings, final newline, spaces by default, 4-space indentation for C and headers, 8-space indentation for assembly, and tabs in `Makefile`. Match xv6 C style: small functions, simple control flow, lowercase identifiers, and minimal abstraction. Kernel entry points and helpers belong in matching subsystem files, for example `kernel/sysnet.c` for network syscalls and `kernel/e1000.c` for E1000 driver work. User programs should be named `user/name.c` and added to `UPROGS` as `$U/_name`.

## Testing Guidelines

The repository uses layered test targets. Prefer the lightest target that covers your change:

- **Small / build-only changes**: `make build && make image`
- **Before every commit**: `make test-smoke` (`make smoke` is now a compatibility alias)
- **Subsystem changes**: `make test-<lab>` (e.g. `make test-mmap`, `make test-fs`) or `make grade-<lab>` for Python grader comparison
- **Before merging**: `make regression` or `make grade-all-heavy` (full heavy suite)
- **Network changes**: additionally run `make server`, `make qemu-net`, `make ping`, or `nettests` in xv6 shell
- Legacy Python smoke remains available as `make smoke-py`; use `xv6test run --suite smoke --tags <tag>` for tag-filtered Go checks.

All graders live under `graders/`. Keep test output deterministic; graders match exact lines.

## Commit & Pull Request Guidelines

The commit format depends on the type of change:

- **Lab migration commits**: use `feat(<lab>): integrate <lab> lab changes` followed by `- ` bullet points with no blank lines between them.
- **Non-lab maintenance commits**: use short imperative subjects, e.g. `docs: refresh roadmap`, `build: move artifacts under build`, `refactor: modernize bio.c naming`.
- Keep commits focused on one topic. Pull requests should state the subsystem changed, summarize behavior, list validation commands run, and mention any known limitations. Include terminal output only when it helps explain a failure or non-obvious result.
- Do **not** add `Co-Authored-By: Claude`.

## Agent-Specific Instructions

### Branch & Workspace Awareness

- The integration branch is **`dev/all`** (based on `net`). Always verify with `git branch` before editing.
- Never work directly on lab branches (`util`, `syscall`, `pgtbl`, `net`, etc.) — those are reference implementations checked out from upstream.

### Code Hygiene (headers, formatting, diagnostics)

- After editing any header, run `clangd --check=<file>` to catch forward-reference or missing-type errors. Headers must be **self-contained**: include their own `#include` dependencies (e.g. `types.h` for `uint64`).
- Before every commit, run `clang-format -i` on every changed `.c` and `.h` file. The repository `.clang-format` uses 4-space indent, LLVM base style.
- When writing or translating comments, **keep English names** for technical terms, structs, functions, and macros alongside the Chinese explanation — e.g. "超级块 (superblock)"、"inode 表 (itable)"、"空闲位图 (bitmap)"、"间接块 (indirect block)"、"引用计数 (ref)"。

### Lab Migration Workflow

When migrating a lab from its reference branch to `dev/all`:

1. `git diff net..<lab> --name-only` to list the functional files for that lab.
2. **Reference the original branch implementation** — use `git show <lab>:<file>` to read the exact working code from the lab branch. Copy its logic faithfully, especially subtle details like macro definitions and conditionals.
3. Migrate only the **lab-specific functional code** — skip formatting noise, toolchain config (`.clang-format`, `.clangd`, `compile_commands.json`), and temporary files.
4. Remove `#ifdef LAB_*` guards for the migrated lab; `dev/all` integrates all labs unconditionally.
5. **Do not modify grader expectations** to match local file differences. Instead, add stable test data (e.g. the original `README` file) so the grader stays unchanged.
6. Run the grader (`make grade-<lab>` or `make grade-all`) and confirm all tests pass.
7. After all changes, sync the migration status in `docs/lab-migration-plan.md` and `README.md`.

### Grading Conventions

- `make grade-all` order: **util → syscall → net → pgtbl → traps → cow → thread → lock → fs → mmap**.
- `conf/lab.mk` keeps `LAB=net` with a comment explaining that `dev/all` integrates multiple labs; this ensures net-specific kernel objects and QEMU flags are always active.
- **Remove `time.txt` checks** from all graders. The `@test(1, "time")` / `check_time()` block in every grader script should be deleted — `time.txt` is a course hand-in artifact that is irrelevant for `dev/all`.
