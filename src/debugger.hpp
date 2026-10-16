// debugger.hpp - the debugger: command loop and everything the user can ask for.
//
// A Debugger owns one Tracee (the program being debugged) and turns the user's
// text commands ("continue", "break main", ...) into calls on it.
#pragma once

#include <istream>
#include <string>
#include <vector>

#include "tracee.hpp"

class Debugger {
public:
    Debugger(std::string program, std::vector<std::string> args)
        : program_(std::move(program)), args_(std::move(args)) {}

    // Launch the program and stop at its first instruction. False on failure.
    bool start();

    // Read commands from `in` until it ends or the user types quit.
    // `echo` prints each command after the prompt (for scripts and piped input).
    // Returns true if the user asked to quit.
    bool run_commands(std::istream& in, bool echo);

    // Kill the program if it is still running.
    void shutdown();

private:
    // Run one command line. Returns true for quit.
    bool execute(const std::string& line);

    void cmd_continue();
    void cmd_regs();
    void cmd_set(const std::vector<std::string>& words);
    void cmd_help();

    // Print what happened after the tracee stopped, exited or was killed.
    void report(const WaitResult& result);

    // False (with a message) if the program is not running any more.
    bool require_process();

    std::string program_;
    std::vector<std::string> args_;
    Tracee tracee_;

    // A signal (e.g. SIGSEGV) that stopped the program and has not been delivered
    // yet. It is passed to the program on the next resume.
    int pending_signal_ = 0;
};
