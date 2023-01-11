#include "pdb_organizer.h"

#include <iostream>
#include "pdb_parser.h"

void PdbOrganizer::Organize(const PdbParser &parser) {
  std::vector<std::shared_ptr<TypeData>> types_list = parser.get_types_list();

  std::map<std::string, size_t> unique_class_name_to_type_id;

  // First pass (map unique class names to type IDs)
  for (std::shared_ptr<TypeData> data : types_list) {
    std::shared_ptr<ClassTypeData> class_data =
        std::dynamic_pointer_cast<ClassTypeData>(data);
    if (class_data != nullptr && !class_data->get_forward_ref()) {
      unique_class_name_to_type_id[class_data->get_unique_name()] =
          class_data->get_type();
    }
  }

  // Second pass
  for (std::shared_ptr<TypeData> data : types_list) {
    auto class_data = std::dynamic_pointer_cast<ClassTypeData>(data);
    if (class_data != nullptr) {
      if (class_data->get_forward_ref()) {
        if (unique_class_name_to_type_id.count(class_data->get_unique_name()) !=
            0) {
          ref_cls_to_defined_cls_map_[data->get_type()] =
              unique_class_name_to_type_id[class_data->get_unique_name()];
        }
      } else {
        // Only insert actual classes (not just forward references).
        type_id_to_cls_data_map_[data->get_type()] = class_data;
      }
    }

    auto field_list_data = std::dynamic_pointer_cast<FieldListTypeData>(data);
    if (field_list_data != nullptr) {
      field_list_data_set[data->get_type()] = field_list_data;
    }
  }

  for (auto [type_id, class_data] : type_id_to_cls_data_map_) {
    if (!class_data->get_forward_ref()) {
      std::cout << class_data->get_class_name() << std::endl;

      std::shared_ptr<FieldListTypeData> field_list =
          field_list_data_set[class_data->get_field_list_type()];

      const std::set<size_t> &base_classes = field_list->get_base_classes();

      for (size_t base_class_ref : base_classes) {
        if (ref_cls_to_defined_cls_map_.count(base_class_ref) == 0) {
          throw std::runtime_error("could not find class with ref type ID " +
                                   std::to_string(base_class_ref));
        }

        size_t cls_declaration = ref_cls_to_defined_cls_map_[base_class_ref];
        if (type_id_to_cls_data_map_.count(cls_declaration) == 0) {
          throw std::runtime_error("could not find class with type ID " +
                                   std::to_string(cls_declaration));
        }

        std::shared_ptr<ClassTypeData> cls = type_id_to_cls_data_map_
            [ref_cls_to_defined_cls_map_[base_class_ref]];

        std::cout << "    " << cls->get_class_name() << std::endl;
      }
    }
  }
}
