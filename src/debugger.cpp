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
    } else if (cmd == "break" || cmd == "b") {
        cmd_break(words);
    } else if (cmd == "delete") {
        cmd_delete(words);
    } else if (cmd == "info") {
        cmd_info(words);
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
           "  break <address>    stop when the program reaches this address\n"
           "  delete <n>     remove breakpoint number n\n"
           "  info breakpoints   list breakpoints\n"
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

    // If we are stopped on a breakpoint, its int3 is still in memory. Resuming
    // right away would execute it again and stop at the same place forever, so
    // first step over that one instruction.
    if (find_breakpoint_at(current_pc())) {
        WaitResult stepped = step_instruction();
        if (report_unusual_stop(stepped)) return;  // the step itself crashed or exited
    }

    tracee_.cont(pending_signal_);
    pending_signal_ = 0;
    WaitResult result = tracee_.wait();
    if (report_unusual_stop(result)) return;
    report_trap_after_continue();
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

bool Debugger::report_unusual_stop(const WaitResult& result) {
    switch (result.kind) {
    case StopKind::Exited:
        printf("Process %d exited with code %d.\n", tracee_.pid(), result.value);
        return true;
    case StopKind::Killed:
        printf("Process %d was killed by %s.\n", tracee_.pid(), signal_name(result.value).c_str());
        return true;
    case StopKind::Stopped:
        if (result.value == SIGTRAP) return false;
        // Some other signal. Remember it so the next resume passes it to the program.
        printf("Program received %s.\n", signal_name(result.value).c_str());
        print_stop_location();
        pending_signal_ = result.value;
        return true;
    }
    return true;
}

void Debugger::report_trap_after_continue() {
    user_regs_struct regs;
    tracee_.get_regs(regs);

    // When the CPU executes int3 it has already moved rip PAST the one-byte
    // instruction, so a breakpoint at address A shows up as rip == A + 1.
    Breakpoint* bp = find_breakpoint_at(regs.rip - 1);
    if (!bp) {
        printf("Program received SIGTRAP.\n");
        print_stop_location();
        return;
    }

    // Move rip back so it points at the instruction the int3 replaced. The int3
    // stays in memory; the next resume will step over it (see step_instruction).
    regs.rip = bp->address;
    tracee_.set_regs(regs);
    printf("Breakpoint %d hit.\n", bp->id);
    print_stop_location();
}

void Debugger::print_stop_location() {
    printf("  rip = 0x%lx\n", current_pc());
}

uint64_t Debugger::current_pc() {
    user_regs_struct regs;
    if (!tracee_.get_regs(regs)) return 0;
    return regs.rip;
}

bool Debugger::insert_breakpoint(Breakpoint& bp) {
    uint64_t word;
    if (!tracee_.peek(bp.address, word)) return false;
    bp.original_byte = static_cast<uint8_t>(word & 0xff);  // x86 is little-endian: first byte = low byte
    uint64_t patched = (word & ~0xffULL) | 0xcc;           // 0xCC is the one-byte int3 instruction
    return tracee_.poke(bp.address, patched);
}

void Debugger::remove_breakpoint(const Breakpoint& bp) {
    uint64_t word;
    if (!tracee_.peek(bp.address, word)) return;
    uint64_t restored = (word & ~0xffULL) | bp.original_byte;
    tracee_.poke(bp.address, restored);
}

Breakpoint* Debugger::find_breakpoint_at(uint64_t address) {
    for (Breakpoint& bp : breakpoints_) {
        if (bp.address == address) return &bp;
    }
    return nullptr;
}

WaitResult Debugger::step_instruction() {
    Breakpoint* bp = find_breakpoint_at(current_pc());
    if (bp) remove_breakpoint(*bp);  // put the real instruction back so it can run

    tracee_.single_step(pending_signal_);
    pending_signal_ = 0;
    WaitResult result = tracee_.wait();

    if (bp && tracee_.alive()) insert_breakpoint(*bp);  // re-arm the breakpoint
    return result;
}

void Debugger::cmd_break(const std::vector<std::string>& words) {
    if (!require_process()) return;
    if (words.size() != 2) {
        printf("usage: break <address>\n");
        return;
    }
    uint64_t address;
    if (!parse_number(words[1], address)) {
        printf("'%s' is not an address.\n", words[1].c_str());
        return;
    }
    if (Breakpoint* existing = find_breakpoint_at(address)) {
        printf("Breakpoint %d is already at 0x%lx.\n", existing->id, address);
        return;
    }
    Breakpoint bp{next_breakpoint_id_, address, 0};
    if (!insert_breakpoint(bp)) {
        printf("Cannot access memory at 0x%lx.\n", address);
        return;
    }
    next_breakpoint_id_++;
    breakpoints_.push_back(bp);
    printf("Breakpoint %d at 0x%lx\n", bp.id, address);
}

void Debugger::cmd_delete(const std::vector<std::string>& words) {
    uint64_t id;
    if (words.size() != 2 || !parse_number(words[1], id)) {
        printf("usage: delete <breakpoint number>\n");
        return;
    }
    for (size_t i = 0; i < breakpoints_.size(); i++) {
        if (breakpoints_[i].id == static_cast<int>(id)) {
            if (tracee_.alive()) remove_breakpoint(breakpoints_[i]);
            breakpoints_.erase(breakpoints_.begin() + i);
            printf("Deleted breakpoint %lu.\n", id);
            return;
        }
    }
    printf("No breakpoint number %lu.\n", id);
}

void Debugger::cmd_info(const std::vector<std::string>& words) {
    if (words.size() != 2 || (words[1] != "breakpoints" && words[1] != "b")) {
        printf("usage: info breakpoints\n");
        return;
    }
    if (breakpoints_.empty()) {
        printf("No breakpoints.\n");
        return;
    }
    printf("Num  Address\n");
    for (const Breakpoint& bp : breakpoints_) {
        printf("%-4d 0x%016lx\n", bp.id, bp.address);
    }
}
