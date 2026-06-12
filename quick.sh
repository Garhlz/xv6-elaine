#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cd "$ROOT_DIR"

step() {
    printf '\n[%s] %s\n' "$1" "$2"
}

step "1/7" "Build kernel and fs image"
make kernel/kernel fs.img

step "2/7" "User-space smoke (util)"
./grade-lab-util --no-make

step "3/7" "Syscall smoke"
./grade-lab-syscall --no-make

step "4/7" "Net smoke"
./grade-lab-net --no-make

step "5/7" "VM/trap smoke"
./grade-lab-pgtbl --no-make
./grade-lab-traps --no-make

step "6/7" "FS symlink smoke"
python3 <<'PY'
import types
import gradelib
from gradelib import *

gradelib.options = types.SimpleNamespace(verbose=False, no_make=True, color="never")

r = Runner()
r.run_qemu(shell_script(["symlinktest"]), timeout=30)
r.match("^test symlinks: ok$")
r.match("^test concurrent symlinks: ok$")
print("fs symlink smoke: OK")
PY

step "7/7" "mmap smoke"
python3 <<'PY'
import types
import gradelib
from gradelib import *

gradelib.options = types.SimpleNamespace(verbose=False, no_make=True, color="never")

r = Runner()
r.run_qemu(shell_script(["mmaptest"]), timeout=60)
r.match("^mmaptest: all tests succeeded$")
print("mmap smoke: OK")
PY

printf '\nquick check passed\n'
