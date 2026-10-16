// util.cpp - tiny string helpers shared by the command parser.
#include "util.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <sstream>

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> words;
    std::istringstream stream(line);
    std::string word;
    while (stream >> word) {  // operator>> skips whitespace for us
        words.push_back(word);
    }
    return words;
}

bool parse_number(const std::string& text, uint64_t& out) {
    bool negative = !text.empty() && text[0] == '-';
    std::string digits = negative ? text.substr(1) : text;

    int base = 10;
    if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
        base = 16;
        digits = digits.substr(2);
    }
    // strtoull would silently skip spaces and accept a sign; insist on a real digit.
    if (digits.empty() || !isalnum(static_cast<unsigned char>(digits[0]))) return false;

    char* end = nullptr;
    errno = 0;
    unsigned long long value = strtoull(digits.c_str(), &end, base);
    if (*end != '\0' || errno == ERANGE) return false;  // junk after the digits, or too big

    out = negative ? 0 - value : value;
    return true;
}
