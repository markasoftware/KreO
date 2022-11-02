#include "pdb_analyzer.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static constexpr std::string_view kTypesSection{"*** TYPES"};
static constexpr std::string_view kSectionHeadersSection{"*** SECTION HEADERS"};
static constexpr std::string_view kPublicsSection{"*** PUBLICS"};
static constexpr std::string_view kClassId{"LF_CLASS"};
static constexpr std::string_view kFieldListId{"LF_FIELDLIST"};

static constexpr std::string_view kForwardRef{", FORWARD REF, "};
static constexpr std::string_view kStaticId{", STATIC, "};
static constexpr std::string_view kOneMethod{"= LF_ONEMETHOD, "};

static constexpr std::string_view kFieldListTypeId{"field list type "};

static constexpr std::string_view kClassNameId{"class name = "};
static constexpr std::string_view kFieldIndexId{"index = "};
static constexpr std::string_view kNameId{"name = "};
static constexpr std::string_view kUniqueName{"unique name = "};

static constexpr std::string_view kBlank{""};

// ============================================================================
void PdbAnalyzer::AnalyzePdbDump(const std::string &fname) {
  std::fstream fstream(fname);

  if (!fstream.is_open()) {
    throw std::runtime_error("failed to open file named " + fname);
  }

  fstream.seekg(std::fstream::beg);
  FindTypes(fstream);
  // fstream.seekg(std::fstream::beg);
  // FindSectionHeaders(fstream);
  // fstream.seekg(std::fstream::beg);
  // FindPublics(fstream);
}

// ============================================================================
void PdbAnalyzer::FindTypes(std::fstream &fstream) {
  // Move stream to start of types (past file header)
  SeekToSectionHeader(fstream, kTypesSection);

  ci_ = std::make_shared<std::map<uint32_t, ClassInfo>>();
  ml_ = std::make_shared<std::map<uint32_t, MethodList>>();

  std::string line;

  // Seek past blank line between types section header and first type
  MustGetLine(
      fstream,
      line,
      "failed to seek past blank line between types header and first type");

  // Call IterateToNewType to iterate to the next type and std::getline to get
  // the first line in the type
  while (IterateToNewType(fstream, line) && std::getline(fstream, line)) {
    if (Contains(line, kClassId)) {
      ClassInfo ci;

      // Extract class type index
      type_id_t type_index{};
      GetHexValueAfterString(line, kBlank, type_index);

      MustGetLine(fstream, line, "failed to get second class line");
      if (Contains(line, kForwardRef)) {
        // Forward refs are ignored
        continue;
      }

      // Extract field list type
      GetHexValueAfterString(line, kFieldListTypeId, ci.field_list);

      MustGetLine(fstream, line, "failed to get third class line");
      MustGetLine(fstream, line, "failed to get fourth class line");

      // Extract class name
      GetStrValueAfterString(line, kClassNameId, ci.class_name);

      // Extract unique name
      GetStrValueAfterString(line, kUniqueName, ci.mangled_class_name);

      // Insert class info to class info map
      ci_->insert(std::pair(type_index, ci));
    } else if (Contains(line, kFieldListId)) {
      MethodList ml;

      // Extract address of field list
      uint32_t address{0};
      {
        std::stringstream ss;
        ss << std::hex << line;
        if (!(ss >> address)) {
          throw std::runtime_error("getting address failed");
        }
      }

      // Extract list contents
      while (true) {
        MustGetLine(fstream, line, "failed to get line from field list");

        if (Contains(line, kOneMethod)) {
          // Comment at end of line indicates additional content associated with
          // list element on next line
          while (line.find_last_of(',') == line.size() - 2) {
            std::string next_line;
            MustGetLine(fstream, next_line, "failed to get next method line");
            line += next_line;
          }

          // Check if it is a static method, continue if it is
          if (Contains(line, kStaticId)) {
            continue;
          }

          MethodInfo mi;

          GetHexValueAfterString(line, kFieldIndexId, mi.type_id);

          GetStrValueAfterString(line, kNameId, mi.name);

          ml.method_index_list.push_back(mi);
        } else if (line == kBlank) {
          break;
        }
      }

      if (ml.method_index_list.size() > 0) {
        ml_->insert(std::pair(address, ml));
      }
    } else if (line == kBlank) {
      break;
    }
  }
}

// ============================================================================
void PdbAnalyzer::FindSectionHeaders(std::fstream &fstream) {
  SeekToSectionHeader(fstream, kSectionHeadersSection);

  std::string line;
}

// ============================================================================
void PdbAnalyzer::FindPublics(std::fstream &fstream) {
  SeekToSectionHeader(fstream, kPublicsSection);

  std::string line;
}

// ============================================================================
void PdbAnalyzer::SeekToSectionHeader(std::fstream &fstream,
                                      const std::string_view &header) {
  std::string line;
  while (true) {
    MustGetLine(
        fstream, line, "failed to get line when seeking to section header");
    if (line == header) {
      return;
    }
  }
}

// ============================================================================
void PdbAnalyzer::GetHexValueAfterString(const std::string &line,
                                         const std::string_view str,
                                         uint32_t &out_value) {
  // it is a method, get name and index
  {
    std::stringstream ss;
    auto loc = line.find(str);
    if (loc == std::string::npos) {
      throw std::runtime_error(std::string("failed to find ") + str.data());
    }
    ss << std::hex << line.substr(loc + str.size());
    if (!(ss >> out_value)) {
      throw std::runtime_error(std::string("failed to get value trailing ") +
                               str.data());
    }
  }
}

// ============================================================================
void PdbAnalyzer::GetStrValueAfterString(const std::string &line,
                                         const std::string_view str,
                                         std::string &out_str) {
  // it is a method, get name and index
  {
    std::stringstream ss;
    auto loc = line.find(str);
    if (loc == std::string::npos) {
      throw std::runtime_error(std::string("failed to find ") + str.data());
    }
    ss << line.substr(loc + str.size());
    if (!(ss >> out_str)) {
      throw std::runtime_error(std::string("failed to get value trailing ") +
                               str.data());
    }
  }
}

// ============================================================================
bool PdbAnalyzer::Contains(const std::string &str,
                           const std::string_view &substr) {
  return str.find(substr) != std::string::npos;
}

// ============================================================================
bool PdbAnalyzer::IterateToNewType(std::fstream &stream,
                                   const std::string &last_line) {
  std::string line = last_line;

  do {
    if (line == kBlank) {
      return true;
    }
  } while (std::getline(stream, line));

  return false;
}

// ============================================================================
void PdbAnalyzer::MustGetLine(std::fstream &stream, std::string &out_str,
                              const std::string &error_str) {
  if (!std::getline(stream, out_str)) {
    throw std::runtime_error("MustGetLine failed, " + error_str);
  }
}
