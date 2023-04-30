#pragma once

#include <map>
#include <memory>

#include "pdb_parser.h"
#include "type_data_instances.h"

class PdbParser;

/**
 * Restructures data generated from a PdbParser into a more useful form.
 */
class PdbOrganizer {
 public:
  void Organize(const PdbParser& parser);

  const std::map<size_t, std::shared_ptr<ClassTypeData>>&
  get_type_id_to_cls_data_map() const {
    return type_id_to_cls_data_map_;
  }

  const std::map<size_t, std::shared_ptr<ProcedureTypeData>>&
  get_type_id_to_procedure_data_map() const {
    return type_id_to_procedure_data_map_;
  }

  const std::map<size_t, std::shared_ptr<FieldListTypeData>>&
  get_type_id_to_field_list_data_map() const {
    return type_id_to_field_list_data_map_;
  }

  const std::map<size_t, std::vector<ProcedureSymbolData>>&
  get_class_type_to_procedure_list() const {
    return class_type_to_symbol_proc_list_;
  }

  const std::map<size_t, size_t>& get_ref_cls_to_defined_cls_map() const {
    return ref_cls_to_defined_cls_map_;
  }

 private:
  std::map<size_t, std::shared_ptr<ClassTypeData>> type_id_to_cls_data_map_;

  std::map<size_t, std::shared_ptr<ProcedureTypeData>>
      type_id_to_procedure_data_map_;

  std::map<size_t, std::shared_ptr<FieldListTypeData>>
      type_id_to_field_list_data_map_;

  std::map<size_t, std::vector<ProcedureSymbolData>>
      class_type_to_symbol_proc_list_;

  /// @brief Maps reference class type ID to class definition type ID.
  std::map<size_t, size_t> ref_cls_to_defined_cls_map_;
};
