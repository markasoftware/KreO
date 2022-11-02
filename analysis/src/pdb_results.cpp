#include "pdb_results.h"

#include <ostream>
#include <sstream>

PdbResults::PdbResults(std::shared_ptr<std::map<uint32_t, ClassInfo>> ci,
                       std::shared_ptr<std::map<uint32_t, FieldList>> fl)
    : ci_(ci),
      fl_(fl) {}

std::optional<uint32_t> PdbResults::FindClassIndex(
    const std::string &classname) {
  for (const auto &it : *ci_) {
    if (classname == it.second.class_name) {
      return it.first;
    }
  }
  return std::nullopt;
}

/// @brief Combines elements in the class info and field list maps that have
/// the same class name (dbc file sometimes produces identical class
/// structures that share field list elements for some reason). This should
/// really be investigated. For example, std::exception is listed twice as a
/// type.
void PdbResults::CombineClasses() {
  std::map<std::string, uint32_t> classes;
  for (auto it = ci_->begin(); it != ci_->end();) {
    auto existing_ci_it = classes.find(it->second.class_name);

    if (existing_ci_it != classes.end()) {
      // Get associated field list
      uint32_t removed_fl_index = it->second.field_list;

      auto removed_fl_it = fl_->find(removed_fl_index);

      if (removed_fl_it != fl_->end()) {
        if (fl_->count(existing_ci_it->second)) {
          auto &index_list = (*fl_)[existing_ci_it->second].method_index_list;

          for (const auto &removed_fl_it_element :
               removed_fl_it->second.method_index_list) {
            auto Find = [=]() {
              for (const auto &it : index_list) {
                if (it == removed_fl_it_element) {
                  return true;
                }
              }
              return false;
            };

            if (!Find()) {
              index_list.push_back(removed_fl_it_element);
            }
          }
        } else {
          (*fl_)[existing_ci_it->second] = removed_fl_it->second;
        }

        fl_->erase(removed_fl_it);
      }

      it = ci_->erase(it);
    } else {
      classes.insert(std::pair(it->second.class_name, it->second.field_list));
      it++;
    }
  }
}

std::ostream &operator<<(std::ostream &os, const PdbResults &results) {
  os << "{" << std::endl;
  for (const auto &it : *results.ci_) {
    os << '\t' << it.second.class_name;

    const auto &fl = results.fl_->find(it.second.field_list);
    if (fl != results.fl_->end()) {
      os << ": " << fl->second << "}";
    }

    os << std::endl;
  }
  os << "}";
  return os;
}

void PdbResults::RemoveAllBut(const std::set<std::string> &classes) {
  for (auto it = ci_->begin(); it != ci_->end();) {
    if (!classes.count(it->second.class_name)) {
      it = ci_->erase(it);
    } else {
      it++;
    }
  }
}

boost::json::value PdbResults::ToJson() const {
  boost::json::object obj;
  // obj["structures"]

  boost::json::object structures;

  // structures["classname"] = class structure

  for (const auto &it : *ci_) {
    boost::json::object class_info;

    class_info["demangled_name"] = it.second.class_name;

    const auto &fl_it = fl_->find(it.second.field_list);
    if (fl_it != fl_->end()) {
      boost::json::object methods;
      for (const auto &field_it : fl_it->second.method_index_list) {
        boost::json::object method;
        method["demangled_name"] = field_it.first;
        std::stringstream ss;
        ss << std::hex << field_it.second;
        methods["0x" + ss.str()] = method;
      }

      class_info["methods"] = methods;
    }

    structures[it.second.mangled_class_name] = class_info;
  }

  obj["structures"] = structures;

  return obj;
}
