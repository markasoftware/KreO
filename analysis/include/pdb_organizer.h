#pragma once

#include <map>
#include <memory>

#include "type_data_instances.h"

class PdbParser;

class PdbOrganizer {
 public:
  void Organize(const PdbParser &parser);

  std::map<size_t, std::shared_ptr<ClassTypeData>> type_id_to_cls_data_map_;

  std::map<size_t, std::shared_ptr<FieldListTypeData>> field_list_data_set;

  std::map<size_t, size_t> ref_cls_to_defined_cls_map_;
};
