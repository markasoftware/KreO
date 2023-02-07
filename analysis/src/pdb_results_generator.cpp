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
  const auto &base_class_it =
      organizer_.get_type_id_to_cls_data_map().find(base_class);

  if (base_class_it == organizer_.get_type_id_to_cls_data_map().end()) {
    throw std::runtime_error(
        "failed to find base class with reference class id " + base_class);
  }

  boost::json::object member;
  member["base"] = false;
  member["name"] = base_class_it->second->get_class_name();
  member["offset"] = "0x0";  // Doesn't matter for our purposes
  member["parent"] = true;
  member["size"] = 0;  // doesn't matter for our purposes
  member["struc"] = "TODO";
  member["type"] = "struc";
  member["usages"] = boost::json::array();  // doesn't matter
  return member;
}

boost::json::object PdbResultsGenerator::json_generate_method_info(
    const std::string &name, size_t method_type) const {
  auto it = organizer_.get_type_id_to_procedure_data_map().find(method_type);
  std::shared_ptr<ProcedureTypeData> procedure_info = it->second;

  boost::json::object method;
  method["demangled_name"] = name;
  method["ea"] = procedure_info->get_addr();
  method["import"] = false;
  method["name"] = name;
  method["type"] = "meth";  // TODO
  return method;
}

boost::json::value PdbResultsGenerator::ToJson() const {
  boost::json::object obj;

  boost::json::object structures;

  const auto &cls_map = organizer_.get_type_id_to_cls_data_map();
  const auto &field_list_map = organizer_.get_type_id_to_field_list_data_map();

  for (const auto &[type_id, cls] : cls_map) {
    // Get field list associated with this class.
    const auto &field_list = field_list_map.find(cls->get_field_list_type());

    if (field_list == field_list_map.end()) {
      throw std::runtime_error(
          "could not find field list associated with class named \"" +
          cls->get_class_name() + "\"");
    }

    const auto &base_classes = field_list->second->get_base_classes();
    const auto &methods = field_list->second->get_methods();
    const auto &method_lists = field_list->second->get_method_lists();

    boost::json::object members;
    size_t ii = 0;
    for (size_t type_id : base_classes) {
      boost::json::object member = json_generate_base_class_info(type_id);
      members[std::to_string(ii++)] = member;
    }

    std::set<FieldListTypeData::MethodInfo> method_info = methods;
    // TODO add in method lists as well

    boost::json::object method_objs;
    for (FieldListTypeData::MethodInfo mi : methods) {
      boost::json::object method_info = json_generate_method_info(mi.name, mi.index);
      std::string ea = method_info.at("ea").as_string().c_str();
      method_objs[ea] = method_info;
    }

    boost::json::object class_info;
    class_info["demangled_name"] = cls->get_class_name();
    class_info["members"] = members;
    class_info["methods"] = method_objs;
    class_info["size"] = 0;  // Doesn't matter for our purposes
    class_info["vftables"] = boost::json::object();
    class_info["name"] = cls->get_class_name();

    structures[cls->get_class_name()] = class_info;
  }

  obj["structures"] = structures;
  obj["vcalls"] = boost::json::object();
  obj["version"] = kVersion.data();

  return obj;
}