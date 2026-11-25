#!/bin/sh
# Q3RF -- GCC-RL78 build (git-bash).
#   sh build.sh                      -> ../lcd_segtest_main.c -> q3rf_lcdtest.mot
#   sh build.sh ../lcd_numtest_main.c q3rf_numtest   -> q3rf_numtest.mot
set -e
SRC="${1:-../lcd_segtest_main.c}"
OUT="${2:-q3rf_lcdtest}"
BIN="/d/!!!!Claude/q3rf/toolchain/extracted/bin"
GCC="$BIN/rl78-unknown-elf-gcc.exe"
OCP="$BIN/rl78-unknown-elf-objcopy.exe"
ODP="$BIN/rl78-unknown-elf-objdump.exe"
SZ="$BIN/rl78-unknown-elf-size.exe"
cd "$(dirname "$0")"
echo "[1/3] Forditas: $SRC"
"$GCC" -mcpu=g13 -Os -Wall -Wextra --param=min-pagesize=0 -ffreestanding -fno-builtin \
  -nostartfiles -nostdlib -nodefaultlibs -g -T rl78.ld -o "$OUT.elf" crt0.S "$SRC" -lgcc
echo "[2/3] S-record (.mot)..."
"$OCP" -O srec "$OUT.elf" "$OUT.mot"
echo "[3/3] Ellenorzes (reset-vektor @0x0, option byte @0xC0):"
"$ODP" -s -j .vec -j .option_bytes "$OUT.elf"
"$ODP" -h "$OUT.elf" | grep -E "text|rodata"
"$SZ" "$OUT.elf"
echo "BUILD OK -> $OUT.mot"
