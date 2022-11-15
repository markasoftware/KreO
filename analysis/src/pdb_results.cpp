#include "pdb_results.h"

#include <iostream>
#include <ostream>
#include <sstream>

// ============================================================================
PdbResults::PdbResults(std::shared_ptr<std::map<uint32_t, ClassInfo>> ci,
                       std::shared_ptr<std::map<std::string, MethodList>> fl)
    : ci_(ci),
      ml_(fl) {}

// ============================================================================
std::optional<uint32_t> PdbResults::FindClassIndex(
    const std::string &classname) {
  for (const auto &it : *ci_) {
    if (classname == it.second.class_name) {
      return it.first;
    }
  }
  return std::nullopt;
}

// ============================================================================
void PdbResults::CombineClasses() {
  std::set<std::string> classes;
  for (auto it = ci_->begin(); it != ci_->end();) {
    auto existing_ci_it = classes.find(it->second.class_name);

    if (existing_ci_it != classes.end()) {
      // Get associated field list
      std::string removed_fl_cn = it->second.class_name;

      auto removed_fl_it = ml_->find(removed_fl_cn);

      if (removed_fl_it != ml_->end()) {
        if (ml_->count(*existing_ci_it)) {
          auto &index_list = (*ml_)[*existing_ci_it].method_index_list;

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
          (*ml_)[*existing_ci_it] = removed_fl_it->second;
        }

        ml_->erase(removed_fl_it);
      }

      it = ci_->erase(it);
    } else {
      classes.insert(it->second.class_name);
      it++;
    }
  }
}

// ============================================================================
std::ostream &operator<<(std::ostream &os, const PdbResults &results) {
  os << "{" << std::endl;
  for (const auto &it : *results.ci_) {
    os << '\t' << it.second.class_name;

    const auto &fl = results.ml_->find(it.second.class_name);
    if (fl != results.ml_->end()) {
      os << ": " << fl->second << "}";
    }

    os << std::endl;
  }
  os << "}";
  return os;
}

// ============================================================================
void PdbResults::RemoveAllBut(
    const std::map<std::string, std::set<std::string>> &child_to_parent_map) {
  for (auto it = ci_->begin(); it != ci_->end();) {
    if (!child_to_parent_map.count(it->second.class_name)) {
      std::cerr << "erasing " << it->second.class_name
                << " because not found in doxygen output" << std::endl;
      it = ci_->erase(it);
    } else {
      it++;
    }
  }
}

// ============================================================================
boost::json::value PdbResults::ToJson() const {
  boost::json::object obj;

  boost::json::object structures;

  for (const auto &cls : *ci_) {
    boost::json::object class_info;

    class_info["demangled_name"] = cls.second.class_name;
    std::stringstream type_id_ss;
    type_id_ss << std::hex << cls.first;
    class_info["type_id"] = "0x" + type_id_ss.str();

    const auto &fl_it = ml_->find(cls.second.class_name);
    int size = 0;
    std::string constructor_name = cls.second.class_name;
    size_t loc = 0;
    while ((loc = constructor_name.find("::")) != std::string::npos) {
      constructor_name = constructor_name.substr(loc + 2);
    }

    boost::json::object members;
    size_t ii = 0;
    for (const auto &parent : cls.second.parent_classes) {
      ClassInfo &ci = (*ci_)[parent];

      boost::json::object member;
      member["base"] = false;
      member["name"] = ci.class_name;
      member["offset"] = "0x0";  // Doesn't matter for our purposes
      member["parent"] = true;
      member["size"] = 0;  // doesn't matter for our purposes
      member["struc"] = ci.mangled_class_name;
      member["type"] = "struc";
      member["usages"] = boost::json::array();  // doesn't matter

      members[std::to_string(ii)] = member;
      ii++;
    }

    class_info["members"] = members;

    boost::json::object methods;
    if (fl_it != ml_->end()) {
      for (const auto &field_it : fl_it->second.method_index_list) {
        boost::json::object method;

        method["demangled_name"] = field_it.name;

        std::stringstream va_ss;
        va_ss << std::hex << field_it.virtual_address;

        method["ea"] = "0x" + va_ss.str();
        method["import"] = false;
        method["name"] = field_it.name;

        if (field_it.name == cls.second.class_name + "::" + constructor_name) {
          method["type"] = "ctor";
        } else if (field_it.name ==
                   cls.second.class_name + "::~" + constructor_name) {
          method["type"] = "dtor";
        } else {
          method["type"] = "meth";
        }

        std::stringstream type_id_ss;
        type_id_ss << std::hex << field_it.type_id;
        method["type_id"] = "0x" + type_id_ss.str();

        methods["0x" + va_ss.str()] = method;
      }

      size = fl_it->second.method_index_list.size();
    }

    class_info["methods"] = methods;

    class_info["name"] = cls.second.class_name;
    class_info["size"] = size;
    class_info["vftables"] = boost::json::object();

    structures[cls.second.mangled_class_name] = class_info;
  }

  obj["structures"] = structures;
  obj["vcalls"] = boost::json::object();
  obj["version"] = kVersion.data();

  return obj;
}
