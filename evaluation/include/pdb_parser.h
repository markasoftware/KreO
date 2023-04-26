#pragma once

#include <fstream>
#include <map>
#include <memory>
#include <vector>
#include <optional>

#include "type_data.h"


struct ProcedureSymbolData {
  size_t type_id{};
  size_t addr{};
  std::string name{};
};

/**
 * Parse the given PDB file. Extracts object oriented features from the PDB
 * file and stores them in a data structure contained in this class. Data can
 * be queried via the get_type_to_typedata_map() function.
 */
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

  const std::vector<ProcedureSymbolData> &get_procedure_list() const {
    return procedure_list_;
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
  void SeekToSymbols() { SeekToSectionHeaderStart("*** SYMBOLS"); }
  void SeekToGlobals() { SeekToSectionHeaderStart("*** GLOBALS"); }
  void SeekToSectionHeaders() {
    SeekToSectionHeaderStart("*** SECTION HEADERS");
  }
  void SeekToSectionHeaderStart(const std::string_view &header);

  std::fstream pdb_;

  std::map<size_t, std::shared_ptr<TypeData>> type_to_typedata_map_;

  std::vector<ProcedureSymbolData> procedure_list_;

  std::map<size_t, SectionHeaderInfo> header_info_;
};
