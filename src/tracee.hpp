// tracee.hpp - the process being debugged (the "tracee").
//
// This class is the only place that talks to the kernel about the child
// process: fork/exec, waitpid and every ptrace() call live in tracee.cpp.
// It never prints anything; the Debugger class decides what to tell the user.
#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

// What happened when we waited for the tracee.
enum class StopKind {
    Exited,   // the program called exit(); value = exit code
    Killed,   // the program was terminated by a signal; value = signal number
    Stopped,  // the program is alive but stopped; value = the signal that stopped it
};

struct WaitResult {
    StopKind kind;
    int value;
};

class Tracee {
public:
    // Fork, make the child traceable, exec `program`, and wait for the first stop.
    // Returns false (after printing why) if anything goes wrong.
    bool start(const std::string& program, const std::vector<std::string>& args);

    bool alive() const { return alive_; }
    pid_t pid() const { return pid_; }

    // Let the tracee run until the next stop. `signal` is delivered to it (0 = none).
    void cont(int signal);

    // Block until the tracee stops, exits or is killed.
    WaitResult wait();

    // Forcibly end the tracee (used when the user quits early).
    void kill_and_reap();

private:
    pid_t pid_ = 0;
    bool alive_ = false;
};
