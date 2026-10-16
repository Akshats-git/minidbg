// util.hpp - tiny string helpers shared by the command parser.
#pragma once

#include <string>
#include <vector>

// Split a line into words separated by spaces/tabs: "break  main" -> {"break", "main"}.
std::vector<std::string> split(const std::string& line);
