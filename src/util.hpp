// util.hpp - tiny string helpers shared by the command parser.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Split a line into words separated by spaces/tabs: "break  main" -> {"break", "main"}.
std::vector<std::string> split(const std::string& line);

// Parse "123", "-5" or "0x1f" into a 64-bit value (negative numbers wrap around,
// so -1 becomes 0xffffffffffffffff). Returns false if `text` is not a number.
bool parse_number(const std::string& text, uint64_t& out);
