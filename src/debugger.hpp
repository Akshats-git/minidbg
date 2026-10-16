// debugger.hpp - the debugger: command loop and everything the user can ask for.
//
// A Debugger owns one Tracee (the program being debugged) and turns the user's
// text commands ("continue", "break main", ...) into calls on it.
#pragma once

#include <istream>
#include <string>
#include <vector>

#include "elf.hpp"
#include "tracee.hpp"

// A software breakpoint: the byte at `address` is replaced by 0xCC (int3).
struct Breakpoint {
    int id;
    uint64_t address;
    uint8_t original_byte;  // the byte that int3 replaced
};

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

    void cmd_break(const std::vector<std::string>& words);
    void cmd_delete(const std::vector<std::string>& words);
    void cmd_info(const std::vector<std::string>& words);
    void cmd_symbols();
    void cmd_continue();
    void cmd_stepi();
    void cmd_bt();
    void cmd_x(const std::vector<std::string>& words);
    void cmd_write(const std::vector<std::string>& words);
    void cmd_regs();
    void cmd_set(const std::vector<std::string>& words);
    void cmd_help();

    // Turn "main" or "0x555555555149" into a runtime address. Prints why on failure.
    bool resolve_location(const std::string& text, uint64_t& address);

    // Breakpoint bookkeeping. insert writes int3 into the tracee (and remembers the
    // byte it replaced); remove puts the original byte back.
    bool insert_breakpoint(Breakpoint& bp);
    void remove_breakpoint(const Breakpoint& bp);
    Breakpoint* find_breakpoint_at(uint64_t address);

    // Execute one instruction. If a breakpoint sits at the current rip, step over it
    // correctly (remove int3, step, put int3 back).
    WaitResult step_instruction();

    uint64_t current_pc();

    // Like Tracee::peek, but shows the original bytes where we have patched in an
    // int3, so the user (and bt) never see our own 0xCC bytes.
    bool read_word(uint64_t address, uint64_t& word);

    // For bt: if rbp does not describe the current function's frame (we are before
    // its "push rbp; mov rbp, rsp" or at its "ret"), return the stack address that
    // holds its return address. Returns 0 when rbp is fine to use.
    uint64_t return_address_slot_without_frame(const user_regs_struct& regs);

    // "bump+4" for an address inside a known function, "" if we don't know it.
    std::string describe_address(uint64_t address);

    // Report exit / kill / any signal other than SIGTRAP. Returns true if it did,
    // false if the stop was a SIGTRAP the caller must interpret.
    bool report_unusual_stop(const WaitResult& result);

    // After PTRACE_CONT stopped with SIGTRAP: was it one of our breakpoints?
    void report_trap_after_continue();

    void print_stop_location();

    // False (with a message) if the program is not running any more.
    bool require_process();

    std::string program_;
    std::vector<std::string> args_;
    Tracee tracee_;
    ElfFile elf_;

    // Add this to an address from the ELF file to get the address in the running
    // process. Zero for normal executables; the load address for PIE ones.
    uint64_t load_bias_ = 0;

    // A signal (e.g. SIGSEGV) that stopped the program and has not been delivered
    // yet. It is passed to the program on the next resume.
    int pending_signal_ = 0;

    std::vector<Breakpoint> breakpoints_;
    int next_breakpoint_id_ = 1;
};
