#include "pdb_organizer.h"

#include <iostream>

#include "pdb_parser.h"

void PdbOrganizer::Organize(const PdbParser &parser) {
  const std::map<size_t, std::shared_ptr<TypeData>> &type_to_typedata_map =
      parser.get_type_to_typedata_map();

  std::map<std::string, size_t> unique_class_name_to_type_id;

  // First pass (map unique class names to type IDs)
  for (const auto &[tid, data] : type_to_typedata_map) {
    std::shared_ptr<ClassTypeData> class_data =
        std::dynamic_pointer_cast<ClassTypeData>(data);
    if (class_data != nullptr && !class_data->get_forward_ref()) {
      unique_class_name_to_type_id[class_data->get_unique_name()] =
          class_data->get_type();
    }
  }

  // Second pass: collect type id->class map, type id->procedure map
  for (const auto &[tid, data] : type_to_typedata_map) {
    auto class_data = std::dynamic_pointer_cast<ClassTypeData>(data);
    if (class_data != nullptr) {
      if (!class_data->get_forward_ref()) {
        // Only insert actual classes (not just forward references).
        type_id_to_cls_data_map_[data->get_type()] = class_data;
      } else {
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

  // // Second pass
  // for (const auto &[tid, data] : type_to_typedata_map) {
  //   auto class_data = std::dynamic_pointer_cast<ClassTypeData>(data);
  //   if (class_data != nullptr) {
  //     if (class_data->get_forward_ref()) {
  //       if (unique_class_name_to_type_id.count(class_data->get_unique_name())
  //       !=
  //           0) {
  //         ref_cls_to_defined_cls_map_[data->get_type()] =
  //             unique_class_name_to_type_id[class_data->get_unique_name()];
  //       }
  //     } else {
  //       // Only insert actual classes (not just forward references).
  //       type_id_to_cls_data_map_[data->get_type()] = class_data;
  //     }

  //     continue;
  //   }

  //   auto field_list_data =
  //   std::dynamic_pointer_cast<FieldListTypeData>(data); if (field_list_data
  //   != nullptr) {
  //     type_id_to_field_list_data_map_[data->get_type()] = field_list_data;
  //     continue;
  //   }

  //   auto method_list_data =
  //   std::dynamic_pointer_cast<MethodListTypeData>(data); if (method_list_data
  //   != nullptr) {
  //     type_id_to_method_list_data_map_[data->get_type()] = method_list_data;
  //     continue;
  //   }

  //   auto procedure_data = std::dynamic_pointer_cast<ProcedureTypeData>(data);
  //   if (procedure_data != nullptr) {
  //     type_id_to_procedure_data_map_[data->get_type()] = procedure_data;
  //     continue;
  //   }
  // }

  // Find base classes.
  // for (auto [type_id, class_data] : type_id_to_cls_data_map_) {
  //   if (!class_data->get_forward_ref()) {
  //     std::shared_ptr<FieldListTypeData> field_list =
  //         type_id_to_field_list_data_map_[class_data->get_field_list_type()];

  //     const std::set<size_t> &base_classes = field_list->get_base_classes();

  //     for (size_t base_class_ref : base_classes) {
  //       if (ref_cls_to_defined_cls_map_.count(base_class_ref) == 0) {
  //         throw std::runtime_error("could not find class with ref type ID " +
  //                                  std::to_string(base_class_ref));
  //       }

  //       size_t cls_declaration = ref_cls_to_defined_cls_map_[base_class_ref];
  //       if (type_id_to_cls_data_map_.count(cls_declaration) == 0) {
  //         throw std::runtime_error("could not find class with type ID " +
  //                                  std::to_string(cls_declaration));
  //       }

  //       std::shared_ptr<ClassTypeData> cls = type_id_to_cls_data_map_
  //           [ref_cls_to_defined_cls_map_[base_class_ref]];
  //     }
  //   }
  // }

  for (const auto &procedure : type_id_to_procedure_data_map_) {
    size_t ref_class_type = procedure.second->get_class_type_ref();
    size_t class_type = ref_cls_to_defined_cls_map_[ref_class_type];

    if (!class_type_to_procedure_list_.contains(class_type)) {
      class_type_to_procedure_list_[class_type] =
          std::vector<std::shared_ptr<ProcedureTypeData>>();
    }

    class_type_to_procedure_list_.find(class_type)
        ->second.push_back(procedure.second);
  }

  // for (const auto &it : class_type_to_procedure_list_) {
  //   std::cout << std::hex << it.first << std::endl;

  //   for (const auto &it2 : it.second) {
  //     std::cout << "    " << *it2 << std::endl;
  //   }

  //   std::cout << std::endl;
  // }

  // std::cout << "=============" << std::endl;
}
