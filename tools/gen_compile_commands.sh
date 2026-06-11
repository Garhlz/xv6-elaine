#!/usr/bin/env sh

set -eu

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out_file="$root_dir/compile_commands.json"

lab=$(sed -n 's/^LAB=//p' "$root_dir/conf/lab.mk")
lab_upper=$(printf '%s' "$lab" | tr '[:lower:]' '[:upper:]')
server_port=$(expr "$(id -u)" % 5000 + 25099)

lab_flags="-DSOL_${lab_upper} -DLAB_${lab_upper}"
if [ "$lab" = "net" ]; then
  lab_flags="$lab_flags -DNET_TESTS_PORT=${server_port}"
fi

kernel_c_flags="-Wall -Werror -O -fno-omit-frame-pointer -ggdb $lab_flags -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -mno-relax -I. -fno-stack-protector -fno-pie"
kernel_s_flags="--target=riscv64-unknown-elf -x assembler-with-cpp -I. -Ikernel"
user_c_flags="$kernel_c_flags"
user_initcode_flags="$kernel_c_flags -march=rv64g -nostdinc -Ikernel"
mkfs_c_flags="-Werror -Wall -I. $lab_flags"

escape_json() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

write_entry() {
  file_path=$1
  command=$2
  if [ "$first" = 0 ]; then
    printf ',\n' >> "$out_file"
  fi
  first=0
  printf '  {\n' >> "$out_file"
  printf '    "directory": "%s",\n' "$(escape_json "$root_dir")" >> "$out_file"
  printf '    "file": "%s",\n' "$(escape_json "$file_path")" >> "$out_file"
  printf '    "command": "%s"\n' "$(escape_json "$command")" >> "$out_file"
  printf '  }' >> "$out_file"
}

printf '[\n' > "$out_file"
first=1

for file in "$root_dir"/kernel/*.c; do
  rel=${file#"$root_dir"/}
  write_entry "$rel" "clang --target=riscv64-unknown-elf $kernel_c_flags -c $rel"
done

for file in "$root_dir"/kernel/*.S; do
  rel=${file#"$root_dir"/}
  write_entry "$rel" "clang $kernel_s_flags -c $rel"
done

for file in "$root_dir"/user/*.c; do
  rel=${file#"$root_dir"/}
  write_entry "$rel" "clang --target=riscv64-unknown-elf $user_c_flags -c $rel"
done

for file in "$root_dir"/user/*.S; do
  rel=${file#"$root_dir"/}
  if [ "$rel" = "user/initcode.S" ]; then
    write_entry "$rel" "clang --target=riscv64-unknown-elf -x assembler-with-cpp $user_initcode_flags -c $rel"
  else
    write_entry "$rel" "clang --target=riscv64-unknown-elf -x assembler-with-cpp -I. -Ikernel -c $rel"
  fi
done

for file in "$root_dir"/mkfs/*.c; do
  rel=${file#"$root_dir"/}
  write_entry "$rel" "clang $mkfs_c_flags -c $rel"
done

printf '\n]\n' >> "$out_file"
