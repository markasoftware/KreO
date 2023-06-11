#pragma once

#include <cstdint>
#include <functional>
#include <ios>
#include <string>

// trim from start (in place)
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s) {
    rtrim(s);
    ltrim(s);
}

// trim from start (copying)
static inline std::string ltrim_copy(std::string s) {
    ltrim(s);
    return s;
}

// trim from end (copying)
static inline std::string rtrim_copy(std::string s) {
    rtrim(s);
    return s;
}

// trim from both ends (copying)
static inline std::string trim_copy(std::string s) {
    trim(s);
    return s;
}

size_t GetFirstNum(const std::string &str,
                   std::function<std::ios_base &(std::ios_base &)> fn);

size_t GetFirstHex(const std::string &str);

size_t GetFirstDec(const std::string &str);

size_t GetNumBetween(const std::string str, const std::string &before,
                     const std::string &after,
                     std::function<std::ios_base &(std::ios_base &)> fn);

size_t GetHexBetween(const std::string &str, const std::string &before,
                     const std::string &after);

size_t GetDecBetween(const std::string &str, const std::string &before,
                     const std::string &after);

size_t GetNumAfter(const std::string &str, const std::string &before,
                   std::function<std::ios_base &(std::ios_base &)> fn);
size_t GetHexAfter(const std::string &str, const std::string &before);

size_t GetDecAfter(const std::string &str, const std::string &before);

std::string GetStrBetween(const std::string &str, const std::string &before,
                          const std::string &after);

bool StrContains(const std::string &str, const std::string &substr);

std::string GetNthStr(const std::string &str, size_t n, char delim = ' ');

std::string GetStrAfter(const std::string &str, const std::string &before);
