#pragma once
#include <string>

std::string readTextFile(const std::string &path);
std::string execCommandCapture(const std::string &cmd);
std::string htmlEscape(const std::string &s);
bool endsWith(const std::string &s, const std::string &suffix);