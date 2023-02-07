#pragma once

#include <fstream>
#include <map>
#include <memory>
#include <vector>

#include "type_data.h"

class PdbParser {
 public:
  static constexpr size_t kBaseAddr = 0x00400000;

  PdbParser(const std::string &fname);

  void ParseTypeData();

  void ParseSectionHeaders();

  /// @note Must be called after ParseTypeData.
  void ParseSymbols();

  const std::map<size_t, std::shared_ptr<TypeData>> &get_type_to_typedata_map()
      const {
    return type_to_typedata_map_;
  }

 private:
  struct SectionHeaderInfo {
    size_t header_num;
    size_t virtual_size;
    size_t virtual_addr;
  };

  std::optional<std::shared_ptr<TypeData>> GetNextTypeData();

  std::optional<SectionHeaderInfo> GetNextSectionHeader();

  void HandleNextSymbol();

  /// @brief Returns the next line in the PDB dump. Throws runtime_error if
  /// getting line fails.
  std::string GetNextLine();

  /// @brief Peeks at the next line. The state of the stream is unchanged
  /// after peeking (unlike GetNextLine() where the stream pointer is
  /// advanced).
  std::string PeekNextLine();

  void SeekToIds() { SeekToSectionHeaderStart("*** IDs"); }
  void SeekToTypes() { SeekToSectionHeaderStart("*** TYPES"); }
  void SeekToGlobals() { SeekToSectionHeaderStart("*** GLOBALS"); }
  void SeekToSectionHeaders() {
    SeekToSectionHeaderStart("*** SECTION HEADERS");
  }
  void SeekToSectionHeaderStart(const std::string_view &header);

  std::fstream pdb_;

  std::map<size_t, std::shared_ptr<TypeData>> type_to_typedata_map_;

  std::map<size_t, SectionHeaderInfo> header_info_;
};