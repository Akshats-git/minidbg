#!/usr/bin/env bash
# run_demo.sh - plays the minidbg demo: two debugger sessions driven by -x scripts.
#
# Build first (cmake --build build). Needs objdump (comes with gcc's binutils).
#
# Record, convert to a GIF, and upload:
#   asciinema rec --cols 96 --rows 46 -c demo/run_demo.sh docs/demo.cast
#   agg --font-size 16 --last-frame-duration 4 docs/demo.cast docs/demo.gif
#   asciinema upload docs/demo.cast        # prints the shareable link
#
# Tools: sudo apt install asciinema; agg is a single binary from
#   https://github.com/asciinema/agg/releases (or: cargo install --git https://github.com/asciinema/agg)
set -euo pipefail
cd "$(dirname "$0")/.."

DBG=./build/minidbg
FACT=./build/examples/factorial
SEGV=./build/examples/segfault

# Print stdin one line at a time with a pause after each, so a recording is easy
# to follow. minidbg reads its -x file line by line, so it just waits for the next one.
paced() {
    while IFS= read -r line; do
        printf '%s\n' "$line"
        case $line in
            '#'*)   sleep 1.5 ;;
            regs*)  sleep 3 ;;
            stepi*) sleep 0.6 ;;
            *)      sleep 1.1 ;;
        esac
    done
}

# --- Fill in the two machine-specific parts of demo/factorial.cmds. -------------
# Where does gcc keep the argument `n`, and how many instructions does it take to
# store it? Read both from the disassembly of the function's prologue.
disasm=$(objdump -d -M intel --no-show-raw-insn "$FACT" | sed -n '/<factorial>:/,/^$/p' | tail -n +2)
steps=$(echo "$disasm" | grep -n -m1 'DWORD PTR \[rbp-0x[0-9a-f]*\],edi' | cut -d: -f1)
offset=$(echo "$disasm" | grep -m1 'DWORD PTR \[rbp-0x[0-9a-f]*\],edi' | sed 's/.*\[rbp-\(0x[0-9a-f]*\)\].*/\1/')

# The stack address depends on the environment, so do a silent trial run (same
# program, same environment) to see what rbp is after the prologue.
rbp=$({ echo "break factorial"; echo continue
        for ((i = 0; i < steps; i++)); do echo stepi; done
        echo regs; echo quit; } | "$DBG" "$FACT" 2>&1 | awk '$1 == "rbp" { print $2 }')
n_addr=$(printf '0x%x' $((rbp - offset)))

fill_template() {
    while IFS= read -r line; do
        case $line in
            '##'*)   ;;  # a note for readers of the file: not part of the show
            @STEPI@) for ((i = 0; i < steps; i++)); do echo stepi; done ;;
            *)       printf '%s\n' "${line//@N_ADDR@/$n_addr}" ;;
        esac
    done < demo/factorial.cmds
}

shell_line() { printf '\033[1;32m$\033[0m %s\n' "$*"; sleep 1; }

clear
shell_line ./build/minidbg -x demo/factorial.cmds ./build/examples/factorial
"$DBG" -x <(fill_template | paced) "$FACT"

sleep 2
clear
shell_line ./build/minidbg -x demo/segfault.cmds ./build/examples/segfault
"$DBG" -x <(grep -v '^##' demo/segfault.cmds | paced) "$SEGV"
sleep 2
