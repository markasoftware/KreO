// #include "pdb_results.h"

// #include <iostream>
// #include <ostream>
// #include <sstream>

// // ============================================================================
// PdbResults::PdbResults(const std::vector<ClassData> &class_data)
//     : class_data_(class_data) {}

// // ============================================================================
// boost::json::value PdbResults::ToJson() const {
//   boost::json::object obj;

//   boost::json::object structures;

//   for (const auto &cls : class_data_) {
//     boost::json::object class_info;

//     class_info["demangled_name"] = cls.class_name;

//     // Removing namespace (make sure not to remove any namespace inside template)
//     // Do this by first removing trailing namespace after class name, then removing everything before last "::"
//     std::string class_name_template_removed = cls.class_name;
//     if (class_name_template_removed[class_name_template_removed.size() - 1] == '>') {

//       size_t num_brackets{1};
//       class_name_template_removed = class_name_template_removed.substr(0, class_name_template_removed.size() - 1);

//       while (num_brackets > 0) {
//         size_t next_close = class_name_template_removed.rfind('>');
//         size_t next_open = class_name_template_removed.rfind('<');

//         if (next_close == std::string::npos || next_close < next_open) {
//           num_brackets--;
//           class_name_template_removed = class_name_template_removed.substr(0, next_open);
//         } else {
//           num_brackets++;
//           class_name_template_removed = class_name_template_removed.substr(0, next_close);
//         }
//       }
//     }

//     size_t last_colons = class_name_template_removed.rfind("::");
//     std::string constructor_name = last_colons == std::string::npos ? cls.class_name : cls.class_name.substr(last_colons + 2);

//     boost::json::object members;
//     size_t ii = 0;
//     for (const auto &parent : cls.mangled_parent_names) {
//       boost::json::object member;
//       member["base"] = false;
//       member["name"] = parent + "_0x0";
//       member["offset"] = "0x0";  // Doesn't matter for our purposes
//       member["parent"] = true;
//       member["size"] = 0;  // doesn't matter for our purposes
//       member["struc"] = parent;
//       member["type"] = "struc";
//       member["usages"] = boost::json::array();  // doesn't matter

//       members[std::to_string(ii)] = member;
//       ii++;
//     }

//     class_info["members"] = members;

//     boost::json::object methods;
//     for (const auto &meth : cls.methods) {
//       boost::json::object method;

//       method["demangled_name"] = meth.name;

//       std::stringstream va_ss;
//       va_ss << std::hex << meth.virtual_address;

//       method["ea"] = "0x" + va_ss.str();
//       method["import"] = false;
//       method["name"] = meth.name;

//       if (meth.name == constructor_name) {
//         method["type"] = "ctor";
//       } else if (meth.name == "~" + constructor_name) {
//         method["type"] = "dtor";
//       } else {
//         method["type"] = "meth";
//       }

//       methods["0x" + va_ss.str()] = method;
//     }

//     class_info["methods"] = methods;

//     class_info["name"] = cls.class_name;
//     class_info["size"] = cls.methods.size();
//     class_info["vftables"] = boost::json::object();

//     structures[cls.mangled_class_name] = class_info;
//   }

//   obj["structures"] = structures;
//   obj["vcalls"] = boost::json::object();
//   obj["version"] = kVersion.data();

//   return obj;
// }
