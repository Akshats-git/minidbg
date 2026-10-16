// registers.cpp - register name table. See registers.hpp.
#include "registers.hpp"

const std::vector<RegisterInfo>& all_registers() {
    static const std::vector<RegisterInfo> table = {
        {"rax", &user_regs_struct::rax},
        {"rbx", &user_regs_struct::rbx},
        {"rcx", &user_regs_struct::rcx},
        {"rdx", &user_regs_struct::rdx},
        {"rsi", &user_regs_struct::rsi},
        {"rdi", &user_regs_struct::rdi},
        {"rbp", &user_regs_struct::rbp},
        {"rsp", &user_regs_struct::rsp},
        {"r8", &user_regs_struct::r8},
        {"r9", &user_regs_struct::r9},
        {"r10", &user_regs_struct::r10},
        {"r11", &user_regs_struct::r11},
        {"r12", &user_regs_struct::r12},
        {"r13", &user_regs_struct::r13},
        {"r14", &user_regs_struct::r14},
        {"r15", &user_regs_struct::r15},
        {"rip", &user_regs_struct::rip},
        {"eflags", &user_regs_struct::eflags},
        {"cs", &user_regs_struct::cs},
        {"ss", &user_regs_struct::ss},
        {"ds", &user_regs_struct::ds},
        {"es", &user_regs_struct::es},
        {"fs", &user_regs_struct::fs},
        {"gs", &user_regs_struct::gs},
        {"fs_base", &user_regs_struct::fs_base},
        {"gs_base", &user_regs_struct::gs_base},
        {"orig_rax", &user_regs_struct::orig_rax},
    };
    return table;
}

const RegisterInfo* find_register(const std::string& name) {
    for (const RegisterInfo& info : all_registers()) {
        if (name == info.name) return &info;
    }
    return nullptr;
}
