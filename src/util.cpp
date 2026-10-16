// util.cpp - tiny string helpers shared by the command parser.
#include "util.hpp"

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
