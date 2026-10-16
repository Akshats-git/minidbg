// registers.hpp - names of the x86-64 registers and where they live in
// user_regs_struct (the struct PTRACE_GETREGS fills in).
#pragma once

#include <sys/user.h>

#include <string>
#include <vector>

struct RegisterInfo {
    const char* name;
    // Pointer to a data member: "which field of user_regs_struct is this register".
    // Given a user_regs_struct `r`, `r.*(info.field)` reads or writes that field.
    unsigned long long user_regs_struct::*field;
};

// Every register in user_regs_struct, in the order we print them.
const std::vector<RegisterInfo>& all_registers();

// Look up a register by name ("rax", "rip", ...). Returns nullptr if unknown.
const RegisterInfo* find_register(const std::string& name);
