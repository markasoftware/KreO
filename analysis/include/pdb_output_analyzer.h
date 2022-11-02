#pragma once

#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

struct ClassInfo {
  uint32_t field_list{};
  std::string mangled_class_name;
  std::string class_name;

  friend std::ostream &operator<<(std::ostream &os, const ClassInfo &ci) {
    os << "{'" << ci.class_name << "' field_list: " << ci.field_list << "}";
    return os;
  }
};

struct FieldList {
  std::vector<std::pair<std::string, uint32_t>> method_index_list;

  friend std::ostream &operator<<(std::ostream &os, const FieldList fl) {
    os << "{";
    bool first = true;
    for (const auto it : fl.method_index_list) {
      if (!first) {
        os << ", ";
      } else {
        first = false;
      }
      os << "{" << it.first << ", " << it.second << "}";
    }
    os << "}";
    return os;
  }
};

std::pair<std::shared_ptr<std::map<uint32_t, ClassInfo>>,
          std::shared_ptr<std::map<uint32_t, FieldList>>>
AnalyzePdbDump(const std::string &fname);
