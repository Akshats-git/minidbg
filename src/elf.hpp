// elf.hpp - a minimal ELF64 reader: just enough to get the function symbols.
//
// We parse the file ourselves using the structs from <elf.h>:
//   ELF header (Elf64_Ehdr) -> section header table (Elf64_Shdr[])
//   -> the .symtab section (Elf64_Sym[]) + its string table (.strtab).
// No libelf or other library is used.
#pragma once

#include <elf.h>

#include <cstdint>
#include <string>
#include <vector>

struct Symbol {
    std::string name;
    uint64_t address;  // as written in the file; add the load bias for PIE programs
    uint64_t size;
};

class ElfFile {
public:
    // Read and parse `path`. Throws std::runtime_error with a readable message if
    // the file is missing, damaged, or not a 64-bit x86-64 ELF file.
    void load(const std::string& path);

    // True for position-independent executables (type ET_DYN): their addresses are
    // offsets that the kernel relocates at load time.
    bool is_pie() const { return type_ == ET_DYN; }

    // All function symbols, sorted by address.
    const std::vector<Symbol>& functions() const { return functions_; }

    const Symbol* find_by_name(const std::string& name) const;

    // The function whose [address, address + size) range contains `address`.
    const Symbol* find_containing(uint64_t address) const;

private:
    // Copy a struct out of the file at `offset` (throws if it would run past the end).
    template <typename T>
    T read_at(uint64_t offset) const;

    // Read a NUL-terminated string at `offset`.
    std::string string_at(uint64_t offset) const;

    std::vector<char> data_;  // the whole file
    uint16_t type_ = 0;       // ET_EXEC or ET_DYN
    std::vector<Symbol> functions_;
};
