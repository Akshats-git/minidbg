// main.cpp - entry point of minidbg.
//
// Usage: minidbg [-x script] program [program args...]
// Parses the command line, launches the program under the debugger, then runs
// the command loop: first the -x script (if any), then the keyboard.
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

#include "debugger.hpp"

int main(int argc, char** argv) {
    // Make stdout unbuffered so our messages and the program's own output come
    // out in the order they happen.
    setvbuf(stdout, nullptr, _IONBF, 0);

    // Optional "-x <file>": run the debugger commands in <file> first, like gdb -x.
    // That makes a session repeatable, e.g. for a demo.
    int first = 1;
    const char* script_path = nullptr;
    if (argc >= 3 && strcmp(argv[1], "-x") == 0) {
        script_path = argv[2];
        first = 3;
    }

    if (first >= argc) {
        fprintf(stderr, "usage: %s [-x script] program [args...]\n", argv[0]);
        return 1;
    }

    std::string program = argv[first];
    std::vector<std::string> args(argv + first + 1, argv + argc);

    // Open the script before launching anything, so a typo fails fast.
    std::ifstream script;
    if (script_path) {
        script.open(script_path);
        if (!script) {
            perror(script_path);
            return 1;
        }
    }

    Debugger dbg(program, args);
    if (!dbg.start()) return 1;

    // Run the script first (echoing each command so the output reads like a
    // session). If it does not end with quit, carry on with the keyboard. When stdin
    // is not a terminal (piped input) we echo as well.
    bool quit = false;
    if (script_path) quit = dbg.run_commands(script, true);
    if (!quit) dbg.run_commands(std::cin, !isatty(STDIN_FILENO));

    dbg.shutdown();
    return 0;
}
