#pragma once

#include <map>
#include <memory>

#include "type_data_instances.h"

class PdbParser;

class PdbOrganizer {
 public:
  void Organize(const PdbParser& parser);

  const std::map<size_t, std::shared_ptr<ClassTypeData>>&
  get_type_id_to_cls_data_map() const {
    return type_id_to_cls_data_map_;
  }
  const std::map<size_t, std::shared_ptr<FieldListTypeData>>&
  get_type_id_to_field_list_data_map() const {
    return type_id_to_field_list_data_map_;
  }
  const std::map<size_t, std::shared_ptr<MethodListTypeData>>&
  get_type_id_to_method_list_data_map() const {
    return type_id_to_method_list_data_map_;
  }
  const std::map<size_t, std::shared_ptr<ProcedureTypeData>>&
  get_type_id_to_procedure_data_map() const {
    return type_id_to_procedure_data_map_;
  }

 private:
  std::map<size_t, std::shared_ptr<ClassTypeData>> type_id_to_cls_data_map_;
  std::map<size_t, std::shared_ptr<FieldListTypeData>>
      type_id_to_field_list_data_map_;
  std::map<size_t, std::shared_ptr<MethodListTypeData>>
      type_id_to_method_list_data_map_;
  std::map<size_t, std::shared_ptr<ProcedureTypeData>>
      type_id_to_procedure_data_map_;

  /// @brief Maps reference class type ID to class definition type ID.
  std::map<size_t, size_t> ref_cls_to_defined_cls_map_;
};
