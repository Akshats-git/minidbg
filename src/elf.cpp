// elf.cpp - a minimal ELF64 reader. See elf.hpp.
#include "elf.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

template <typename T>
T ElfFile::read_at(uint64_t offset) const {
    if (offset > data_.size() || data_.size() - offset < sizeof(T)) {
        throw std::runtime_error("ELF file is truncated or corrupt");
    }
    T value;
    memcpy(&value, data_.data() + offset, sizeof(T));  // memcpy avoids alignment problems
    return value;
}

std::string ElfFile::string_at(uint64_t offset) const {
    if (offset >= data_.size()) throw std::runtime_error("ELF string offset out of range");
    const char* start = data_.data() + offset;
    size_t length = strnlen(start, data_.size() - offset);  // stops at the NUL (or the end of file)
    return std::string(start, length);
}

void ElfFile::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open file");
    data_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());

    // --- 1. ELF header, at the very start of the file. ---
    if (data_.size() < SELFMAG || memcmp(data_.data(), ELFMAG, SELFMAG) != 0) {  // magic: 0x7f 'E' 'L' 'F'
        throw std::runtime_error("not an ELF file");
    }
    Elf64_Ehdr header = read_at<Elf64_Ehdr>(0);
    if (header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB ||
        header.e_machine != EM_X86_64) {
        throw std::runtime_error("only 64-bit little-endian x86-64 ELF files are supported");
    }
    type_ = header.e_type;

    // --- 2. Section header table: e_shnum entries starting at e_shoff. ---
    std::vector<Elf64_Shdr> sections;
    for (size_t i = 0; i < header.e_shnum; i++) {
        sections.push_back(read_at<Elf64_Shdr>(header.e_shoff + i * header.e_shentsize));
    }

    // --- 3. Find .symtab (type SHT_SYMTAB). Its sh_link says which section is its string table. ---
    struct Candidate {
        Symbol symbol;
        uint64_t section_end;  // where the section holding the symbol ends
    };
    std::vector<Candidate> found;

    for (const Elf64_Shdr& symtab : sections) {
        if (symtab.sh_type != SHT_SYMTAB) continue;
        if (symtab.sh_link >= sections.size()) throw std::runtime_error("bad .symtab link");
        const Elf64_Shdr& strtab = sections[symtab.sh_link];

        size_t count = symtab.sh_size / sizeof(Elf64_Sym);
        for (size_t i = 0; i < count; i++) {
            Elf64_Sym sym = read_at<Elf64_Sym>(symtab.sh_offset + i * sizeof(Elf64_Sym));

            // Skip undefined symbols (imports like printf) and special section numbers.
            if (sym.st_shndx == SHN_UNDEF || sym.st_shndx >= sections.size()) continue;
            const Elf64_Shdr& section = sections[sym.st_shndx];

            // What counts as a "function"?
            //  - gcc/clang mark functions STT_FUNC.
            //  - NASM labels are STT_NOTYPE (and have size 0) unless the source says
            //    otherwise, so also accept GLOBAL no-type symbols that sit in code.
            unsigned type = ELF64_ST_TYPE(sym.st_info);
            unsigned bind = ELF64_ST_BIND(sym.st_info);
            bool in_code = (section.sh_flags & SHF_EXECINSTR) != 0;
            bool is_function = type == STT_FUNC ||
                               (type == STT_NOTYPE && in_code && bind != STB_LOCAL);
            if (!is_function) continue;

            std::string name = string_at(strtab.sh_offset + sym.st_name);
            if (name.empty()) continue;
            found.push_back({{name, sym.st_value, sym.st_size}, section.sh_addr + section.sh_size});
        }
        break;  // there is only one .symtab
    }

    // --- 4. Sort by address, and give size-less symbols a size. ---
    std::sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) {
        return a.symbol.address < b.symbol.address;
    });
    for (size_t i = 0; i < found.size(); i++) {
        Symbol sym = found[i].symbol;
        if (sym.size == 0) {
            // No size recorded (typical for assembly): assume the function runs until
            // the next symbol, or until the end of its section.
            uint64_t end = found[i].section_end;
            for (size_t j = i + 1; j < found.size(); j++) {
                if (found[j].symbol.address > sym.address) {
                    end = std::min(end, found[j].symbol.address);
                    break;
                }
            }
            sym.size = end > sym.address ? end - sym.address : 0;
        }
        functions_.push_back(sym);
    }
}

const Symbol* ElfFile::find_by_name(const std::string& name) const {
    for (const Symbol& sym : functions_) {
        if (sym.name == name) return &sym;
    }
    return nullptr;
}

const Symbol* ElfFile::find_containing(uint64_t address) const {
    // functions_ is sorted, so binary-search for the first symbol that starts AFTER
    // `address`; the one just before it is the only candidate.
    auto after = std::upper_bound(functions_.begin(), functions_.end(), address,
                                  [](uint64_t addr, const Symbol& sym) { return addr < sym.address; });
    if (after == functions_.begin()) return nullptr;
    const Symbol& candidate = *(after - 1);
    if (address < candidate.address + candidate.size) return &candidate;
    return nullptr;
}
