#pragma once

#include <cstdint>
#include <ostream>
#include <string>

#include "utils.h"

class TypeData {
 public:
  virtual void Parse(const std::vector<std::string> &lines) = 0;

  virtual void to_string(std::ostream &os) const = 0;

  friend std::ostream &operator<<(std::ostream &os, const TypeData &td) {
    td.to_string(os);
    return os;
  }

  void set_addr(size_t addr) { addr_ = addr; }
  void set_name(const std::string &name) { name_ = name; }

  size_t get_type() const { return type_; }
  size_t get_length() const { return length_; }
  size_t get_leaf() const { return leaf_; }
  size_t get_addr() const { return addr_; }
  const std::string &get_name() const { return name_; }

 protected:
  void ParseFirstLine(const std::string &str) {
    type_ = GetFirstHex(str);
    length_ = GetDecBetween(str, "Length = ", ", ");
    leaf_ = GetHexAfter(str, "Leaf = ");
  }

  size_t type_{};
  size_t length_{};
  size_t leaf_{};
  size_t addr_{};
  std::string name_{};
};
