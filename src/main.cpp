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

    if (argc < 2) {
        fprintf(stderr, "usage: %s program [args...]\n", argv[0]);
        return 1;
    }

    std::string program = argv[1];
    std::vector<std::string> args(argv + 2, argv + argc);

    Debugger dbg(program, args);
    if (!dbg.start()) return 1;

    // Commands come from the keyboard. When stdin is not a terminal (piped input)
    // we echo them so the output still reads like a session.
    dbg.run_commands(std::cin, !isatty(STDIN_FILENO));

    dbg.shutdown();
    return 0;
}
