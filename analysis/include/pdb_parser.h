#pragma once

#include <map>
#include <fstream>
#include <vector>
#include "type_data.h"
#include <memory>

class PdbParser {
 public:
  using TypeIdToDataMap = std::map<size_t, std::shared_ptr<TypeData>>;

  PdbParser(const std::string &fname);

  void ParseTypeData();

  const std::vector<std::shared_ptr<TypeData>> &get_types_list() const {
    return types_list_;
  }

 private:
  std::optional<std::shared_ptr<TypeData>> GetNextTypeData();

  /// @brief Returns the next line in the PDB dump. Throws runtime_error if
  /// getting line fails.
  std::string GetNextLine();

  void SeekToIds() { SeekToSectionHeaderStart("*** IDs"); }
  void SeekToTypes() { SeekToSectionHeaderStart("*** TYPES"); }
  void SeekToGlobals() { SeekToSectionHeaderStart("*** GLOBALS"); }
  void SeekToSectionHeaderStart(const std::string_view &header);

  std::fstream pdb_;

  TypeIdToDataMap type_id_to_data_map_{};

  std::vector<std::shared_ptr<TypeData>> types_list_;
};
