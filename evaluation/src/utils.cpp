#include "utils.h"

#include <iostream>
#include <sstream>

// ============================================================================
size_t GetFirstNum(const std::string &str,
                   std::function<std::ios_base &(std::ios_base &)> fn) {
  std::stringstream ss(str);
  fn(ss);

  size_t ret_val{};
  if (!(ss >> ret_val)) {
    throw std::runtime_error("failed to get value from str \"" + str + "\"");
  }
  return ret_val;
}

// ============================================================================
size_t GetFirstHex(const std::string &str) {
  return GetFirstNum(str, std::hex);
}

// ============================================================================
size_t GetFirstDec(const std::string &str) {
  return GetFirstNum(str, std::dec);
}

// ============================================================================
size_t GetNumBetween(const std::string str, const std::string &before,
                     const std::string &after,
                     std::function<std::ios_base &(std::ios_base &)> fn) {
  size_t start = str.find(before);

  if (start == std::string::npos) {
    throw std::runtime_error(std::string("GetNumBetween() failed to find \"") +
                             before.data() + "\"" + " in string \"" + str +
                             "\"");
  }

  size_t substr_start = start + before.size();

  std::string str_before_removed = str.substr(substr_start);

  size_t end = str_before_removed.find(after);

  if (end == std::string::npos) {
    throw std::runtime_error(std::string("GetNumBetween() failed to find \"") +
                             after.data() + "\" in string \"" + str + "\"");
  }

  std::string sub_str = str_before_removed.substr(0, end);

  return GetFirstNum(sub_str, fn);
}

// ============================================================================
size_t GetHexBetween(const std::string &str, const std::string &before,
                     const std::string &after) {
  return GetNumBetween(str, before, after, std::hex);
}

// ============================================================================
size_t GetDecBetween(const std::string &str, const std::string &before,
                     const std::string &after) {
  return GetNumBetween(str, before, after, std::dec);
}

// ============================================================================
size_t GetNumAfter(const std::string &str, const std::string &before,
                   std::function<std::ios_base &(std::ios_base &)> fn) {
  size_t start = str.find(before);

  if (start == std::string::npos) {
    throw std::runtime_error(std::string("GetNumAfter() failed to find \"") +
                             before + "\" in string \"" + str + "\"");
  }

  return GetFirstNum(str.substr(start + before.size()), fn);
}

// ============================================================================
size_t GetHexAfter(const std::string &str, const std::string &before) {
  return GetNumAfter(str, before, std::hex);
}

// ============================================================================
size_t GetDecAfter(const std::string &str, const std::string &before) {
  return GetNumAfter(str, before, std::dec);
}

// ============================================================================
std::string GetStrBetween(const std::string &str, const std::string &before,
                          const std::string &after) {
  size_t substr_before = str.find(before);

  if (substr_before == std::string::npos) {
    throw std::runtime_error(
        "when finding substr_before failed to find str \"" + before +
        "\" in str \"" + str + "\"");
  }

  std::string substr_start_removed = str.substr(substr_before + before.size());

  size_t substr_after = substr_start_removed.find(after);

  if (substr_after == std::string::npos) {
    throw std::runtime_error("when finding substr_after failed to find str \"" +
                             after + "\" in str \"" + substr_start_removed +
                             "\"");
  }

  return substr_start_removed.substr(0, substr_after);
}

// ============================================================================
bool StrContains(const std::string &str, const std::string &substr) {
  return str.find(substr) != std::string::npos;
}

// ============================================================================
std::string GetNthStr(const std::string &str, size_t n, char delim) {
  std::stringstream ss(str);
  std::string ret_str;

  for (size_t ii = 0; ii <= n; ii++) {
    if (!std::getline(ss, ret_str, delim)) {
      throw std::runtime_error("could not find " + std::to_string(n) +
                               "th string in str " + str);
    }
  }

  return ret_str;
}

// ============================================================================
std::string GetStrAfter(const std::string &str, const std::string &before) {
  std::stringstream ss(str);

  size_t substr_before = str.find(before);

  if (substr_before == std::string::npos) {
    throw std::runtime_error("could not find str \"" + before +
                             "\" in string \"" + str + "\"");
  }

  return str.substr(substr_before + before.size());
}
