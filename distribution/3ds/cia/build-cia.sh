#!/bin/sh
# Package the 3DS ELF as a CIA (installable) and CXI (for the emulator).
set -e
cd "$(dirname "$0")"
ELF=${1:-/home/claude/openrct2/build-3ds/openrct2.elf}
M=/home/claude/3ds-deps/Project_CTR/makerom/bin/makerom
$M -f cia -o openrct2.cia -elf "$ELF" -rsf openrct2.rsf -icon icon.icn -banner banner.bnr -exefslogo -target t
$M -f cxi -o openrct2.cxi -elf "$ELF" -rsf openrct2.rsf -icon icon.icn -banner banner.bnr -exefslogo -target t
ls -la openrct2.cia openrct2.cxi
