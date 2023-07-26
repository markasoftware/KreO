#include "pdb_organizer.h"

#include <iostream>

#include "pdb_parser.h"

void PdbOrganizer::Organize(const PdbParser &parser) {
  const std::map<size_t, std::shared_ptr<TypeData>> &type_to_typedata_map =
      parser.get_type_to_typedata_map();

  const std::vector<ProcedureSymbolData> &procedure_list =
      parser.get_procedure_list();

  std::map<std::string, size_t> unique_class_name_to_type_id;

  // First pass (map unique class names to type IDs)
  for (const auto &[tid, data] : type_to_typedata_map) {
    // If class data, add to unique_class_name_to_type_id and type_id_to_cls_data_map_
    std::shared_ptr<ClassTypeData> class_data =
        std::dynamic_pointer_cast<ClassTypeData>(data);

    if (class_data != nullptr && !class_data->get_forward_ref()) {
      unique_class_name_to_type_id[class_data->get_unique_name()] =
          class_data->get_type();

      type_id_to_cls_data_map_[data->get_type()] = class_data;
    }
  }

  // Second pass: ref->actual class, type id->procedure map
  for (const auto &[tid, data] : type_to_typedata_map) {
    auto class_data = std::dynamic_pointer_cast<ClassTypeData>(data);
    if (class_data != nullptr) {
      if (class_data->get_forward_ref()) {
        // Map reference class to actual class.
        ref_cls_to_defined_cls_map_[class_data->get_type()] =
            unique_class_name_to_type_id[class_data->get_unique_name()];
      }
      continue;
    }

    auto field_list_data = std::dynamic_pointer_cast<FieldListTypeData>(data);
    if (field_list_data != nullptr) {
      type_id_to_field_list_data_map_[data->get_type()] = field_list_data;
      continue;
    }

    auto procedure_data = std::dynamic_pointer_cast<ProcedureTypeData>(data);
    if (procedure_data != nullptr) {
      type_id_to_procedure_data_map_[data->get_type()] = procedure_data;
      continue;
    }
  }

  for (const auto &procedure : procedure_list) {
    const auto &proc_data = type_id_to_procedure_data_map_[procedure.type_id];

    if (!ref_cls_to_defined_cls_map_.contains(
            proc_data->get_class_type_ref())) {
      std::cout << "could not find ref class with type 0x" << std::hex
                << proc_data->get_class_type_ref() << std::endl;
    }

    size_t class_type =
        ref_cls_to_defined_cls_map_[proc_data->get_class_type_ref()];

    if (!class_type_to_symbol_proc_list_.contains(class_type)) {
      if (proc_data->get_call_type() == "ThisCall") {
        class_type_to_symbol_proc_list_[class_type] =
            std::vector<ProcedureSymbolData>();
      }
    }

    if (proc_data->get_call_type() == "ThisCall") {
      class_type_to_symbol_proc_list_[class_type].push_back(procedure);
    }
  }
}
