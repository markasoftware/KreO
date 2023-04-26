#include "pdb_results_generator.h"

#include <iostream>
#include <ostream>
#include <sstream>

#include "pdb_organizer.h"

// ============================================================================
PdbResultsGenerator::PdbResultsGenerator(const PdbOrganizer &organizer)
    : organizer_(organizer) {}

// ============================================================================
boost::json::object PdbResultsGenerator::json_generate_base_class_info(
    size_t base_class) const {
  size_t actual_base_class =
      organizer_.get_ref_cls_to_defined_cls_map().find(base_class)->second;

  const auto &base_class_it =
      organizer_.get_type_id_to_cls_data_map().find(actual_base_class);

  if (base_class_it == organizer_.get_type_id_to_cls_data_map().end()) {
    throw std::runtime_error(
        "failed to find base class with reference class id " +
        actual_base_class);
  }

  boost::json::object member;
  member["base"] = false;
  member["name"] = base_class_it->second->get_class_name();
  member["offset"] = "0x0";  // Doesn't matter for our purposes
  member["parent"] = true;
  member["size"] = 0;  // doesn't matter for our purposes
  member["struc"] = base_class_it->second->get_unique_name();
  member["type"] = "struc";
  member["usages"] = boost::json::array();  // doesn't matter
  return member;
}

boost::json::object PdbResultsGenerator::json_generate_method_info(
    const std::string &name, const std::string &addr,
    const std::string &type) const {
  boost::json::object method;
  method["demangled_name"] = name;
  method["ea"] = addr;
  method["import"] = false;
  method["name"] = name;
  method["type"] = type;
  return method;
}

boost::json::value PdbResultsGenerator::ToJson() const {
  boost::json::object obj;

  boost::json::object structures;

  const auto &cls_map = organizer_.get_type_id_to_cls_data_map();
  const auto &cls_method_lists = organizer_.get_class_type_to_procedure_list();
  const auto &field_list_map = organizer_.get_type_id_to_field_list_data_map();

  for (const auto &[type_id, cls] : cls_map) {
    const auto &method_list_it = cls_method_lists.find(type_id);

    if (method_list_it == cls_method_lists.end()) {
      continue;
    }

    const auto &method_list = method_list_it->second;

    // Removing namespace (make sure not to remove any namespace inside
    // template) Do this by first removing trailing namespace after class name,
    // then removing everything before last "::"
    auto remove_trailing_template = [](const std::string &cls_name) {
      std::string class_name_template_removed = cls_name;
      if (class_name_template_removed[class_name_template_removed.size() - 1] ==
          '>') {
        size_t num_brackets{1};
        class_name_template_removed = class_name_template_removed.substr(
            0, class_name_template_removed.size() - 1);

        while (num_brackets > 0) {
          size_t next_close = class_name_template_removed.rfind('>');
          size_t next_open = class_name_template_removed.rfind('<');

          if (next_close == std::string::npos || next_close < next_open) {
            num_brackets--;
            class_name_template_removed =
                class_name_template_removed.substr(0, next_open);
          } else {
            num_brackets++;
            class_name_template_removed =
                class_name_template_removed.substr(0, next_close);
          }
        }
      }
      return class_name_template_removed;
    };

    std::string class_name_template_removed =
        remove_trailing_template(cls->get_class_name());
    size_t last_colons = class_name_template_removed.rfind("::");
    std::string constructor_name =
        last_colons == std::string::npos
            ? cls->get_class_name()
            : cls->get_class_name().substr(last_colons + 2);

    boost::json::object method_objs;
    for (const auto &method : method_list) {
      size_t addr = method.addr;
      std::stringstream ea_ss;
      ea_ss << "0x" << std::hex << addr;

      std::string type;
      std::cout << method.name << std::endl;
      if (method.name.find(cls->get_class_name()) == 0) {
        if (method.name == cls->get_class_name() + "::" + constructor_name) {
          type = "ctor";
        } else if (method.name ==
                   cls->get_class_name() + "::~" + constructor_name) {
          type = "dtor";
        } else {
          type = "meth";
        }
      } else {
        // This might happen if name is a bit mangled
        // For example, `main'::`209'::TestUtil vs main::L209::TestUtil
        // Get method name with leading namespace removed

        std::string method_name_template_removed =
            remove_trailing_template(method.name);
        last_colons = method_name_template_removed.rfind("::");
        std::string method_name = last_colons == std::string::npos
                                      ? method.name
                                      : method.name.substr(last_colons + 2);

        if (method_name == constructor_name) {
          type = "ctor";
        } else if (method_name == "~" + constructor_name) {
          type = "dtor";
        } else {
          type = "meth";
        }
      }

      method_objs[ea_ss.str()] =
          json_generate_method_info(method.name, ea_ss.str(), type);
    }

    // Get field list associated with this class.
    const auto &field_list = field_list_map.find(cls->get_field_list_type());

    if (field_list == field_list_map.end()) {
      throw std::runtime_error(
          "could not find field list associated with class named \"" +
          cls->get_class_name() + "\"");
    }

    const auto &base_classes = field_list->second->get_base_classes();

    boost::json::object members;
    size_t member_idx = 0;
    for (size_t type_id : base_classes) {
      boost::json::object member = json_generate_base_class_info(type_id);
      members[std::to_string(member_idx++)] = member;
    }

    boost::json::object class_info;
    class_info["demangled_name"] = cls->get_class_name();
    class_info["members"] = members;
    class_info["methods"] = method_objs;
    class_info["size"] = 0;  // Doesn't matter for our purposes
    class_info["vftables"] = boost::json::object();
    class_info["name"] = cls->get_class_name();

    structures[cls->get_unique_name()] = class_info;
  }

  obj["structures"] = structures;
  obj["vcalls"] = boost::json::object();
  obj["version"] = kVersion.data();

  return obj;
}
