// tracee.cpp - the process being debugged (the "tracee").
//
// All fork/exec/waitpid/ptrace calls are in this file. See tracee.hpp.
#include "tracee.hpp"

#include <signal.h>
#include <sys/personality.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

bool Tracee::start(const std::string& program, const std::vector<std::string>& args) {
    // execv wants a char* array: program name, then the arguments, then nullptr.
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(program.c_str()));
    for (const std::string& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return false;
    }

    if (child == 0) {
        // ---- This code runs in the CHILD (the future tracee). ----

        // PTRACE_TRACEME: "my parent is my tracer". From now on the kernel stops
        // this process and tells the parent whenever it gets a signal. The exec
        // below counts too: after a successful execve the kernel sends us a
        // SIGTRAP, so we freeze at the very first instruction of the new program.
        ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);

        // Turn off address space layout randomization for this process (and the
        // program we exec). Stack, heap, libraries and PIE executables then load at
        // the same addresses on every run, which makes sessions repeatable.
        // This must happen before execve, because exec is when the layout is chosen.
        personality(ADDR_NO_RANDOMIZE);

        execv(program.c_str(), argv.data());

        // execv only returns if it failed (e.g. file not found).
        perror(program.c_str());
        _exit(127);
    }

    // ---- This code runs in the PARENT (the debugger). ----
    pid_ = child;
    alive_ = true;

    // Wait for the child's first stop (the SIGTRAP caused by exec).
    WaitResult first = wait();
    if (first.kind != StopKind::Stopped) {
        fprintf(stderr, "could not start %s\n", program.c_str());
        return false;
    }

    // PTRACE_SETOPTIONS + PTRACE_O_EXITKILL: if the debugger dies for any reason
    // (crash, Ctrl-C), the kernel kills the tracee too, so we never leave a
    // stopped orphan process behind.
    ptrace(PTRACE_SETOPTIONS, pid_, nullptr, reinterpret_cast<void*>(PTRACE_O_EXITKILL));
    return true;
}

void Tracee::cont(int signal) {
    // PTRACE_CONT: resume the stopped tracee and let it run freely until the next
    // stop. The last argument is a signal to hand to the program as it resumes;
    // 0 means "no signal". We use it to pass along signals like SIGSEGV that
    // stopped the program, so it behaves the way it would without a debugger.
    ptrace(PTRACE_CONT, pid_, nullptr, reinterpret_cast<void*>(static_cast<long>(signal)));
}

WaitResult Tracee::wait() {
    int status = 0;
    // waitpid blocks until the tracee changes state. With ptrace, "changes state"
    // includes being stopped by a signal, not just exiting.
    if (waitpid(pid_, &status, 0) < 0) {
        perror("waitpid");
        alive_ = false;
        return {StopKind::Exited, -1};
    }
    if (WIFEXITED(status)) {
        alive_ = false;
        return {StopKind::Exited, WEXITSTATUS(status)};
    }
    if (WIFSIGNALED(status)) {
        alive_ = false;
        return {StopKind::Killed, WTERMSIG(status)};
    }
    return {StopKind::Stopped, WSTOPSIG(status)};
}

void Tracee::kill_and_reap() {
    if (!alive_) return;
    // SIGKILL cannot be caught or blocked, so this always ends the tracee.
    // waitpid then collects it so it does not stay around as a zombie.
    kill(pid_, SIGKILL);
    waitpid(pid_, nullptr, 0);
    alive_ = false;
}
