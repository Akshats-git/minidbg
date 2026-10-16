// debugger.cpp - the debugger: command loop and everything the user can ask for.
//
// Each cmd_* function implements one user command. Anything that needs the
// kernel (ptrace, waitpid) goes through the Tracee class in tracee.cpp.
#include "debugger.hpp"

#include <signal.h>
#include <string.h>

#include <cstdio>

#include "registers.hpp"
#include "util.hpp"

namespace {

// "SIGSEGV (Segmentation fault)" for a signal number.
std::string signal_name(int sig) {
    const char* abbrev = sigabbrev_np(sig);  // "SEGV"
    std::string name = abbrev ? std::string("SIG") + abbrev : "signal " + std::to_string(sig);
    return name + " (" + strsignal(sig) + ")";
}

}  // namespace

bool Debugger::start() {
    if (!tracee_.start(program_, args_)) return false;
    printf("Launched %s (pid %d). Type 'continue' to run it, 'help' for commands.\n",
           program_.c_str(), tracee_.pid());
    return true;
}

bool Debugger::run_commands(std::istream& in, bool echo) {
    std::string line;
    while (true) {
        printf("(minidbg) ");
        if (!std::getline(in, line)) {  // end of input (Ctrl-D or end of script)
            printf("\n");
            return false;
        }
        if (echo) printf("%s\n", line.c_str());
        if (execute(line)) return true;
    }
}

void Debugger::shutdown() {
    tracee_.kill_and_reap();
}

bool Debugger::execute(const std::string& line) {
    std::vector<std::string> words = split(line);
    if (words.empty()) return false;
    const std::string& cmd = words[0];

    if (cmd == "quit" || cmd == "q") {
        return true;
    } else if (cmd == "continue" || cmd == "c") {
        cmd_continue();
    } else if (cmd == "regs") {
        cmd_regs();
    } else if (cmd == "set") {
        cmd_set(words);
    } else if (cmd == "help") {
        cmd_help();
    } else {
        printf("Unknown command '%s'. Type 'help'.\n", cmd.c_str());
    }
    return false;
}

void Debugger::cmd_help() {
    printf("Commands:\n"
           "  continue (c)   run until the next stop\n"
           "  regs           print all registers\n"
           "  set <reg> <value>  change a register, e.g. set rax 5\n"
           "  help           show this list\n"
           "  quit (q)       kill the program and exit\n");
}

bool Debugger::require_process() {
    if (!tracee_.alive()) {
        printf("The program is not running.\n");
        return false;
    }
    return true;
}

void Debugger::cmd_continue() {
    if (!require_process()) return;
    tracee_.cont(pending_signal_);
    pending_signal_ = 0;
    report(tracee_.wait());
}

void Debugger::cmd_regs() {
    if (!require_process()) return;
    user_regs_struct regs;
    if (!tracee_.get_regs(regs)) {
        printf("Could not read registers.\n");
        return;
    }
    for (const RegisterInfo& info : all_registers()) {
        // uint64_t is "unsigned long" on x86-64 Linux, which is what %lx / %ld expect.
        uint64_t value = regs.*(info.field);
        printf("%-9s 0x%016lx  %ld\n", info.name, value, static_cast<int64_t>(value));
    }
}

void Debugger::cmd_set(const std::vector<std::string>& words) {
    if (!require_process()) return;
    if (words.size() != 3) {
        printf("usage: set <register> <value>\n");
        return;
    }
    const RegisterInfo* info = find_register(words[1]);
    if (!info) {
        printf("Unknown register '%s'.\n", words[1].c_str());
        return;
    }
    uint64_t value;
    if (!parse_number(words[2], value)) {
        printf("'%s' is not a number.\n", words[2].c_str());
        return;
    }
    // Read-modify-write: fetch all registers, change one field, write them all back.
    user_regs_struct regs;
    if (!tracee_.get_regs(regs)) {
        printf("Could not read registers.\n");
        return;
    }
    regs.*(info->field) = value;
    if (!tracee_.set_regs(regs)) {
        printf("Could not write registers.\n");
        return;
    }
    printf("%s = 0x%lx\n", info->name, value);
}

void Debugger::report(const WaitResult& result) {
    switch (result.kind) {
    case StopKind::Exited:
        printf("Process %d exited with code %d.\n", tracee_.pid(), result.value);
        break;
    case StopKind::Killed:
        printf("Process %d was killed by %s.\n", tracee_.pid(), signal_name(result.value).c_str());
        break;
    case StopKind::Stopped:
        // Not a SIGTRAP we caused ourselves: remember the signal so that the next
        // continue passes it on to the program.
        printf("Program stopped by %s.\n", signal_name(result.value).c_str());
        pending_signal_ = result.value;
        break;
    }
}
