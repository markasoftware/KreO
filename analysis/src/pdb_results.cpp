#include "pdb_results.h"

#include <iostream>
#include <ostream>
#include <sstream>

// ============================================================================
PdbResults::PdbResults(const std::vector<ClassData> &class_data)
    : class_data_(class_data) {}

// ============================================================================
boost::json::value PdbResults::ToJson() const {
  boost::json::object obj;

  boost::json::object structures;

  for (const auto &cls : class_data_) {
    boost::json::object class_info;

    class_info["demangled_name"] = cls.class_name;

    std::string constructor_name = cls.class_name;
    size_t loc = 0;
    size_t template_loc = constructor_name.find_first_of('<');
    while ((loc = constructor_name.find("::")) != std::string::npos &&
           (template_loc == std::string::npos || template_loc > loc)) {
      constructor_name = constructor_name.substr(loc + 2);
    }

    boost::json::object members;
    size_t ii = 0;
    for (const auto &parent : cls.mangled_parent_names) {
      boost::json::object member;
      member["base"] = false;
      member["name"] = parent + "_0x0";
      member["offset"] = "0x0";  // Doesn't matter for our purposes
      member["parent"] = true;
      member["size"] = 0;  // doesn't matter for our purposes
      member["struc"] = parent;
      member["type"] = "struc";
      member["usages"] = boost::json::array();  // doesn't matter

      members[std::to_string(ii)] = member;
      ii++;
    }

    class_info["members"] = members;

    boost::json::object methods;
    for (const auto &meth : cls.methods) {
      boost::json::object method;

      method["demangled_name"] = meth.name;

      std::stringstream va_ss;
      va_ss << std::hex << meth.virtual_address;

      method["ea"] = "0x" + va_ss.str();
      method["import"] = false;
      method["name"] = meth.name;

      if (meth.name == constructor_name) {
        method["type"] = "ctor";
      } else if (meth.name == "~" + constructor_name) {
        method["type"] = "dtor";
      } else {
        method["type"] = "meth";
      }

      methods["0x" + va_ss.str()] = method;
    }

    class_info["methods"] = methods;

    class_info["name"] = cls.class_name;
    class_info["size"] = cls.methods.size();
    class_info["vftables"] = boost::json::object();

    structures[cls.mangled_class_name] = class_info;
  }

  obj["structures"] = structures;
  obj["vcalls"] = boost::json::object();
  obj["version"] = kVersion.data();

  return obj;
}
