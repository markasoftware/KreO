#include "pdb_analyzer.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// ============================================================================
void PdbAnalyzer::AnalyzePdbDump(const std::string &fname) {
  std::fstream fstream(fname);

  if (!fstream.is_open()) {
    throw std::runtime_error("failed to open file named " + fname);
  }

  fstream.seekg(std::fstream::beg);
  FindTypes(fstream);
  fstream.seekg(std::fstream::beg);
  FindInheritanceRelationships(fstream);
  fstream.seekg(std::fstream::beg);
  FindSectionHeaders(fstream);
  fstream.seekg(std::fstream::beg);
  FindSymbols(fstream);
}

// ============================================================================
void PdbAnalyzer::FindTypes(std::fstream &fstream) {
  // Move stream to start of types (past file header)
  SeekToSectionHeader(fstream, kTypesSection);

  ci_ = std::make_shared<std::map<uint32_t, ClassInfo>>();

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
        MustGetLine(fstream, line, "failed to get third class line");
        MustGetLine(fstream, line, "failed to get fourth class line");
        std::string unique_name;
        GetStrValueAfterString(line, kUniqueName, unique_name);
        // trim to remove trailing ,
        forward_ref_type_to_unique_name_[type_index] =
            unique_name.substr(0, unique_name.size() - 1);
        continue;
      }

      // Extract field list type
      GetHexValueAfterString(line, kFieldListTypeId, ci.field_list);

      MustGetLine(fstream, line, "failed to get third class line");
      MustGetLine(fstream, line, "failed to get fourth class line");

      // Extract class name
      GetStrValueAfterString(line, kClassNameId, ci.class_name);
      // trim name to remove trailing ,
      ci.class_name = ci.class_name.substr(0, ci.class_name.size() - 1);

      // Extract unique name
      GetStrValueAfterString(line, kUniqueName, ci.mangled_class_name);
      // trim mangled name to remove trailing ,
      ci.mangled_class_name =
          ci.mangled_class_name.substr(0, ci.mangled_class_name.size() - 1);

      // Insert class info to class info map
      ci_->insert(std::pair(type_index, ci));

      unique_name_to_type_id_[ci.mangled_class_name] = type_index;
      fieldlist_to_class_id_map_[ci.field_list] = type_index;
    } else if (line == kBlank) {
      break;
    }
  }
}

// ============================================================================
void PdbAnalyzer::FindInheritanceRelationships(std::fstream &fstream) {
  // Move stream to start of types (past file header)
  SeekToSectionHeader(fstream, kTypesSection);

  std::string line;

  // Seek past blank line between types section header and first type
  MustGetLine(
      fstream,
      line,
      "failed to seek past blank line between types header and first type");

  // Call IterateToNewType to iterate to the next type and std::getline to get
  // the first line in the type
  while (IterateToNewType(fstream, line) && std::getline(fstream, line)) {
    // associate any LF_BCLASS elements with the class they belong to
    // so we need to know which class the field list is associated with, which
    // we found during FindTypes
    if (Contains(line, kFieldListId)) {
      // Extract method type index
      type_id_t field_list_index{};
      GetHexValueAfterString(line, kBlank, field_list_index);

      if (!fieldlist_to_class_id_map_.count(field_list_index)) {
        continue;
      }

      // Note: entries in the list actual contain type id of the forward
      // reference of the parent class
      std::vector<type_id_t> parent_forward_ref_types_;

      // Extract list contents
      while (true) {
        MustGetLine(fstream, line, "failed to get line from field list");

        if (Contains(line, kBaseClassId)) {
          type_id_t parent_type_id{};
          GetHexValueAfterString(line, kTypeId, parent_type_id);
          parent_forward_ref_types_.push_back(parent_type_id);
        } else if (line == kBlank) {
          break;
        }
      }

      if (parent_forward_ref_types_.size() > 0) {
        for (auto it : parent_forward_ref_types_) {
          type_id_t class_type_id =
              fieldlist_to_class_id_map_[field_list_index];

          if (!ci_->count(class_type_id)) {
            throw std::runtime_error("couldn't find class entry for type ID " +
                                     std::to_string(class_type_id));
          }

          (*ci_)[class_type_id]
              .parent_classes.insert(
                  unique_name_to_type_id_
                      [forward_ref_type_to_unique_name_[it]]);
        }
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
  MustGetLine(
      fstream,
      line,
      "failed to iterate past 1st blank line after section headers section");
  MustGetLine(
      fstream,
      line,
      "failed to iterate past 2nd blank line after section headers section");
  while (IterateToNewType(fstream, line)) {
    int section_id;
    HeaderData hd;

    MustGetLine(fstream, line, "failed to get SECTION HEADER header");
    if (line == kOriginalSectionHeaders) {
      break;
    }

    {
      std::stringstream ss(
          line.substr(line.find(kSectionHeaderNum) + kSectionHeaderNum.size()));
      if (!(ss >> section_id)) {
        throw std::runtime_error("failed to get section id");
      }
    }

    MustGetLine(fstream, line, "failed to get section name");
    {
      std::stringstream ss(line);
      if (!(ss >> hd.name)) {
        throw std::runtime_error("failed to get section name");
      }
    }

    MustGetLine(fstream, line, "failed to get virtual size");
    {
      std::stringstream ss;
      ss << std::hex << line;
      if (!(ss >> hd.virtual_size)) {
        throw std::runtime_error("failed to get virtual size");
      }
    }

    MustGetLine(fstream, line, "failed to get virtual address");
    {
      std::stringstream ss;
      ss << std::hex << line;
      if (!(ss >> hd.virtual_address)) {
        throw std::runtime_error("failed to get virtual address");
      }
    }

    h_data_[section_id] = hd;
  }
}

// ============================================================================
void PdbAnalyzer::FindSymbols(std::fstream &fstream) {
  ml_ = std::make_shared<std::map<std::string, MethodList>>();

  SeekToSectionHeader(fstream, kSymbolsSection);

  std::string line;

  auto SeekToNextEmptyLine = [&]() {
    while (line != kBlank) {
      MustGetLine(fstream,
                  line,
                  "failed to get new line when searching for non blank line");
    }
  };

  auto SeekToNextSymbol = [&]() {
    while (line != kGlobals) {
      MustGetLine(fstream,
                  line,
                  "failed to get new line when searching for new symbol");

      std::stringstream ss(line);
      std::string first_str;
      ss >> first_str;
      if (first_str.size() == 8 && first_str[0] == '(' && first_str[7] == ')') {
        break;
      }
    };
  };

  SeekToNextSymbol();

  while (line != kGlobals) {
    if (line.find(kGproc32) != std::string::npos) {
      MethodInfo mi;

      std::stringstream ss(
          line.substr(line.find(kGproc32) + kGproc32.size() + 1));

      std::string address_info;
      ss >> address_info;

      int section_id{};
      {
        std::stringstream ss(address_info.substr(1, 4));
        if (!(ss >> section_id)) {
          throw std::runtime_error("failed to get section id");
        }
      }

      virtual_address_t local_address{};
      {
        std::stringstream ss;
        ss << std::hex << address_info.substr(6, 8);
        if (!(ss >> local_address)) {
          throw std::runtime_error("failed to get local address");
        }
      }

      std::string type_str;
      if (!(ss >> type_str) || !(ss >> type_str) || !(ss >> type_str) ||
          !(ss >> type_str)) {
        throw std::runtime_error("failed to get type");
      }

      if (type_str.find(kNoType) != std::string::npos) {
        mi.type_id = 0;
      } else {
        std::stringstream ss;
        ss << std::hex << type_str;
        if (!(ss >> mi.type_id)) {
          throw std::runtime_error("failed to get type as int");
        }
      }

      ss >> mi.name;

      virtual_address_t method_address = local_address +
                                         h_data_[section_id].virtual_address +
                                         kDefaultBaseAddress;

      mi.virtual_address = method_address;

      // Find class associated with method info.
      for (const auto &it : *ci_) {
        if (mi.name.find(it.second.class_name) == 0) {
          if (ml_->find(it.second.class_name) == ml_->end()) {
            ml_->insert(std::pair(it.second.class_name, MethodList()));
          }
          (*ml_)[it.second.class_name].method_index_list.push_back(mi);
          break;
        }
      }
    }

    SeekToNextSymbol();
  }
}

// ============================================================================
void PdbAnalyzer::SeekToSectionHeader(std::fstream &fstream,
                                      const std::string_view &header) {
  std::string line;
  while (true) {
    MustGetLine(fstream,
                line,
                "failed to get line when seeking to section header " +
                    std::string(header));
    if (line == header) {
      return;
    }
  }
}

// ============================================================================
void PdbAnalyzer::GetHexValueAfterString(const std::string &line,
                                         const std::string_view &str,
                                         uint32_t &out_value) {
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

// ============================================================================
void PdbAnalyzer::GetStrValueAfterString(const std::string &line,
                                         const std::string_view &str,
                                         std::string &out_str) {
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

void PdbAnalyzer::GetQuotedStrAfterString(const std::string &line,
                                          const std::string_view &str,
                                          std::string &out_str) {
  auto loc = line.find(str);
  if (loc == std::string::npos) {
    throw std::runtime_error(std::string("failed to find ") + str.data());
  }
  std::string subs = line.substr(loc + str.size());
  if (subs[0] != '\'') {
    throw std::runtime_error("trying to get quoted string that isn't quoted");
  }
  subs = subs.substr(1);
  auto end_quote_loc = subs.find_first_of('\'');
  if (end_quote_loc == std::string::npos) {
    throw std::runtime_error("couldn't find end of quoted string");
  }
  out_str = subs.substr(0, end_quote_loc);
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
