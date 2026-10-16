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

const size_t kMaxFrames = 64;  // bt gives up after this many frames

// "SIGSEGV (Segmentation fault)" for a signal number.
std::string signal_name(int sig) {
    const char* abbrev = sigabbrev_np(sig);  // "SEGV"
    std::string name = abbrev ? std::string("SIG") + abbrev : "signal " + std::to_string(sig);
    return name + " (" + strsignal(sig) + ")";
}

}  // namespace

bool Debugger::start() {
    // Parse the ELF file first: if it is not a usable executable, don't launch it.
    try {
        elf_.load(program_);
    } catch (const std::exception& error) {
        fprintf(stderr, "%s: %s\n", program_.c_str(), error.what());
        return false;
    }

    if (!tracee_.start(program_, args_)) return false;
    printf("Launched %s (pid %d).\n", program_.c_str(), tracee_.pid());

    // Where did the kernel put the program? For a PIE executable every address in
    // the ELF file is an offset from this load address. Other executables are
    // linked for fixed addresses, so no adjustment is needed.
    uint64_t mapped_at = 0;
    if (!tracee_.find_load_address(program_, mapped_at)) {
        fprintf(stderr, "Could not find %s in /proc/%d/maps.\n", program_.c_str(), tracee_.pid());
        tracee_.kill_and_reap();
        return false;
    }
    if (elf_.is_pie()) {
        load_bias_ = mapped_at;
        printf("Position-independent executable loaded at 0x%lx.\n", load_bias_);
    } else {
        printf("Fixed-address executable (not PIE), mapped at 0x%lx.\n", mapped_at);
    }

    if (elf_.functions().empty()) {
        printf("No symbol table found (stripped?). Breaking by function name will not work.\n");
    } else {
        printf("Read %zu function symbols.\n", elf_.functions().size());
    }
    printf("Stopped before the program's first instruction:\n");
    print_stop_location();
    printf("Type 'continue' to run it, 'help' for commands.\n");
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
    } else if (cmd == "stepi" || cmd == "si") {
        cmd_stepi();
    } else if (cmd == "bt") {
        cmd_bt();
    } else if (cmd == "x") {
        cmd_x(words);
    } else if (cmd == "write") {
        cmd_write(words);
    } else if (cmd == "symbols") {
        cmd_symbols();
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
           "  break <address|function>   stop when the program reaches it\n"
           "  delete <n>     remove breakpoint number n\n"
           "  info breakpoints   list breakpoints\n"
           "  stepi (si)     execute one instruction\n"
           "  bt             show the call stack (needs -fno-omit-frame-pointer)\n"
           "  x <address> <count>    dump memory as hex 8-byte words\n"
           "  write <address> <value>    write an 8-byte value to memory\n"
           "  symbols        list function symbols with their addresses\n"
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
        printf("Program received signal %s.\n", signal_name(result.value).c_str());
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
    uint64_t rip = current_pc();
    std::string where = describe_address(rip);
    printf("  rip = 0x%lx <%s>\n", rip, where.empty() ? "unknown" : where.c_str());
}

std::string Debugger::describe_address(uint64_t address) {
    if (address < load_bias_) return "";  // below the program: avoid wrapping around
    const Symbol* sym = elf_.find_containing(address - load_bias_);
    if (!sym) return "";
    return sym->name + "+" + std::to_string(address - load_bias_ - sym->address);
}

void Debugger::cmd_stepi() {
    if (!require_process()) return;
    WaitResult result = step_instruction();
    if (report_unusual_stop(result)) return;
    print_stop_location();  // a SIGTRAP here is just the single-step finishing
}

void Debugger::cmd_x(const std::vector<std::string>& words) {
    if (!require_process()) return;
    uint64_t address, count = 1;
    if (words.size() < 2 || words.size() > 3 || !parse_number(words[1], address) ||
        (words.size() == 3 && !parse_number(words[2], count))) {
        printf("usage: x <address> [count]\n");
        return;
    }
    if (count > 1024) {
        printf("count is too large (maximum 1024).\n");
        return;
    }
    // Print two 8-byte words per line, each line starting with its address.
    for (uint64_t i = 0; i < count; i++) {
        uint64_t word_address = address + 8 * i;
        uint64_t word;
        if (!read_word(word_address, word)) {
            if (i > 0) printf("\n");  // finish the line we were in the middle of
            printf("Cannot access memory at 0x%lx.\n", word_address);
            return;
        }
        if (i % 2 == 0) printf("%s0x%lx:", i == 0 ? "" : "\n", word_address);
        printf("  0x%016lx", word);
    }
    printf("\n");
}

bool Debugger::read_word(uint64_t address, uint64_t& word) {
    if (!tracee_.peek(address, word)) return false;
    for (const Breakpoint& bp : breakpoints_) {
        if (bp.address >= address && bp.address < address + 8) {
            int shift = 8 * (bp.address - address);  // little-endian: byte k sits at bits 8k..8k+7
            word = (word & ~(0xffULL << shift)) | (static_cast<uint64_t>(bp.original_byte) << shift);
        }
    }
    return true;
}

uint64_t Debugger::return_address_slot_without_frame(const user_regs_struct& regs) {
    const Symbol* func = elf_.find_containing(regs.rip - load_bias_);
    if (!func) return 0;
    uint64_t start = load_bias_ + func->address;

    // A gcc/NASM function normally begins:  [endbr64]  push rbp  mov rbp, rsp
    //                                        f3 0f 1e fa   55      48 89 e5
    uint64_t word;
    if (!read_word(start, word)) return 0;
    uint8_t first[8];
    for (int i = 0; i < 8; i++) first[i] = (word >> (8 * i)) & 0xff;
    size_t push_at = (first[0] == 0xf3 && first[1] == 0x0f && first[2] == 0x1e && first[3] == 0xfa) ? 4 : 0;
    if (first[push_at] != 0x55) return 0;  // no "push rbp": nothing special we can say

    uint64_t offset = regs.rip - start;
    if (offset <= push_at) return regs.rsp;       // push rbp has not run: return address is on top of the stack
    if (offset == push_at + 1) return regs.rsp + 8;  // push ran, mov did not: saved rbp is on top, return address above it

    uint64_t at_rip;
    if (read_word(regs.rip, at_rip) && (at_rip & 0xff) == 0xc3) return regs.rsp;  // at "ret": frame already torn down
    return 0;
}

void Debugger::cmd_bt() {
    if (!require_process()) return;
    user_regs_struct regs;
    if (!tracee_.get_regs(regs)) {
        printf("Could not read registers.\n");
        return;
    }

    // Collect the return addresses ("pc" of each frame), innermost first.
    std::vector<uint64_t> pcs = {regs.rip};
    uint64_t word;

    // Frame 0 may be in its prologue or epilogue, where rbp still/already belongs to
    // the caller. Then the caller's pc comes from the stack, and rbp is already the
    // right starting point for the walk below.
    uint64_t slot = return_address_slot_without_frame(regs);
    if (slot != 0 && read_word(slot, word)) pcs.push_back(word);

    // The frame-pointer chain. With "push rbp; mov rbp, rsp" at every function entry:
    //     [rbp]     = the caller's saved rbp   (the next link in the chain)
    //     [rbp + 8] = the return address into the caller
    // so each step up the stack gives us one more caller.
    uint64_t rbp = regs.rbp;
    while (pcs.size() < kMaxFrames && describe_address(pcs.back()) != "" && rbp != 0) {
        uint64_t saved_rbp, return_address;
        if (!read_word(rbp, saved_rbp) || !read_word(rbp + 8, return_address)) break;
        pcs.push_back(return_address);
        if (saved_rbp <= rbp) break;  // the stack grows down, so callers' frames are higher; else corrupt
        rbp = saved_rbp;
    }

    for (size_t i = 0; i < pcs.size(); i++) {
        std::string where = describe_address(pcs[i]);
        printf("#%-2zu 0x%016lx <%s>\n", i, pcs[i], where.empty() ? "unknown" : where.c_str());
    }
    // We stop at the first address outside the program (normally libc's startup code,
    // which has no frame pointers). If frame 0 itself is outside, callers can be missing.
    if (describe_address(pcs[0]).empty()) {
        printf("Note: stopped outside the program; frames below may be incomplete.\n");
    }
}

void Debugger::cmd_write(const std::vector<std::string>& words) {
    if (!require_process()) return;
    uint64_t address, value;
    if (words.size() != 3 || !parse_number(words[1], address) || !parse_number(words[2], value)) {
        printf("usage: write <address> <value>\n");
        return;
    }
    if (!tracee_.poke(address, value)) {
        printf("Cannot access memory at 0x%lx.\n", address);
        return;
    }
    printf("Wrote 0x%lx to 0x%lx.\n", value, address);
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
        printf("usage: break <address|function>\n");
        return;
    }
    uint64_t address;
    if (!resolve_location(words[1], address)) return;
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
    printf("Breakpoint %d at 0x%lx <%s>\n", bp.id, address, describe_address(address).c_str());
}

bool Debugger::resolve_location(const std::string& text, uint64_t& address) {
    if (parse_number(text, address)) return true;  // a plain runtime address
    const Symbol* sym = elf_.find_by_name(text);
    if (!sym) {
        printf("No function named '%s' (see 'symbols').\n", text.c_str());
        return false;
    }
    address = load_bias_ + sym->address;  // file address -> running-process address
    return true;
}

void Debugger::cmd_symbols() {
    printf("%-18s  %-6s  %s\n", "Address", "Size", "Name");
    for (const Symbol& sym : elf_.functions()) {
        printf("0x%016lx  %-6lu  %s\n", load_bias_ + sym.address, sym.size, sym.name.c_str());
    }
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
    printf("Num  Address             What\n");
    for (const Breakpoint& bp : breakpoints_) {
        printf("%-4d 0x%016lx  %s\n", bp.id, bp.address, describe_address(bp.address).c_str());
    }
}
