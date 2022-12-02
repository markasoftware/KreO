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

  /*
   * 1. Extract all classes and structs from the types section and place in a
   * map mapping at minimum type id -> (field list type id, name). Also extract
   * class forward references, mapping forward ref type id -> unique name. Also
   * extract field list information. Map field list id -> list of class specific
   * members.
   * 2. Extract all field lists from types section and place in a map
   */

  fstream.seekg(std::fstream::beg);
  FindTypeInfo(fstream);
  fstream.seekg(std::fstream::beg);
  FindSectionHeaders(fstream);
  fstream.seekg(std::fstream::beg);
  FindSymbols(fstream);
}

// ============================================================================
void PdbAnalyzer::FindTypeInfo(std::fstream &fstream) {
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
    if (Contains(line, kClassId) || Contains(line, kStructureId)) {
      ClassInfo ci;

      // Extract class type index
      type_id_t type_index{};
      GetHexValueAfterString(line, kBlank, type_index);

      MustGetLine(fstream, line, "failed to get second class line");

      if (Contains(line, kForwardRef)) {
        // Class is a forward reference
        MustGetLine(fstream, line, "failed to get third class line");
        MustGetLine(fstream, line, "failed to get fourth class line");
        std::string unique_name;

        GetStrValueBetweenStrs(line, kUniqueName, kUdt, unique_name);

        forward_ref_type_to_unique_name_map_[type_index] = unique_name;
      } else {
        // Extract field list type
        GetHexValueAfterString(line, kFieldListTypeId, ci.field_list_type_id);

        MustGetLine(fstream, line, "failed to get third class line");
        MustGetLine(fstream, line, "failed to get fourth class line");

        // Extract class name
        GetStrValueBetweenStrs(
            line, kClassNameId, ", unique name", ci.class_name);

        // Extract unique name
        GetStrValueBetweenStrs(line, kUniqueName, kUdt, ci.mangled_class_name);

        // Mangled name must start with "." or else it is not a class and might
        // be a weird thing where msvc duplicates the class but without the "."
        if (ci.mangled_class_name[0] == '.') {
          class_type_id_to_class_info_map_[type_index] = ci;
        }
      }
    } else if (Contains(line, kFieldListId)) {
      // Extract method type index
      type_id_t field_list_index{};
      GetHexValueAfterString(line, kBlank, field_list_index);

      std::vector<std::shared_ptr<FieldListMember>> field_list;

      // Extract list contents
      while (true) {
        MustGetLine(fstream, line, "failed to get line from field list");

        // Peek ahead until we reach a new list item
        int len = fstream.tellg();
        while (line != "") {
          std::string lookahead;
          MustGetLine(fstream, lookahead, "failed to get next line");
          if (lookahead == "" || Contains(lookahead, "list[")) {
            break;
          }
          line += " " + lookahead;
        }
        fstream.seekg(len, std::ios_base::beg);

        if (Contains(line, "= LF_BCLASS, ")) {
          type_id_t parent_type_id{};
          GetHexValueAfterString(line, "type = ", parent_type_id);
          field_list.push_back(
              std::make_shared<ParentClassFieldList>(parent_type_id));
        } else if (Contains(line, "= LF_ONEMETHOD, ")) {
          if (!Contains(line, ", STATIC,") &&
              !Contains(line, ", (compgenx),")) {
            type_id_t method_type_id{};
            GetHexValueAfterString(line, "index = ", method_type_id);
            std::string name;
            GetQuotedStrAfterString(line, "name = ", name);
            field_list.push_back(
                std::make_shared<OneMethodFieldList>(method_type_id, name));
          }
        } else if (Contains(line, "= LF_METHOD, ")) {
          type_id_t list_type_id{};
          GetHexValueAfterString(line, "list = ", list_type_id);
          std::string name;
          GetQuotedStrAfterString(line, "name = ", name);
          field_list.push_back(
              std::make_shared<MethodFieldList>(list_type_id, name));
        } else if (line == kBlank) {
          break;
        }
      }

      field_list_type_id_to_field_list_map_[field_list_index] = field_list;
    } else if (Contains(line, "LF_METHODLIST")) {
      type_id_t method_list_type_id{};
      GetHexValueAfterString(line, kBlank, method_list_type_id);

      std::vector<type_id_t> type_id_list;

      while (true) {
        MustGetLine(fstream, line, "failed to get line from method list");

        if (line == kBlank) {
          break;
        } else if (!Contains(line, ", STATIC,") &&
                   !Contains(line, ", (compgenx),")) {
          std::stringstream ss(line);
          std::string typeidstr;
          getline(ss, typeidstr, ',');
          getline(ss, typeidstr, ',');
          getline(ss, typeidstr, ',');

          type_id_t type_id;
          GetHexValueAfterString(typeidstr, "", type_id);
          type_id_list.push_back(type_id);
        }
      }

      method_list_type_id_to_method_list_map_[method_list_type_id] =
          type_id_list;
    } else if (Contains(line, "LF_MFUNCTION")) {
      type_id_t method_type_id{};
      GetHexValueAfterString(line, kBlank, method_type_id);

      MustGetLine(fstream, line, "failed to get second method line");
      MustGetLine(fstream, line, "failed to get third method line");

      std::string attr;
      GetStrValueAfterString(line, "Func attr = ", attr);
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
    if (Contains(line, "S_GPROC32") || Contains(line, "S_LPROC32")) {
      // Find type id

      auto pos = line.find("Type: ");
      std::stringstream ss(line.substr(pos));
      std::string type_id_str;
      ss >> type_id_str;
      ss >> type_id_str;

      if (type_id_str != "T_NOTYPE(0000),") {
        type_id_t type_id;
        GetHexValueAfterString(type_id_str, "", type_id);

        std::string method_name;
        getline(ss, method_name);
        method_name = method_name.substr(1);

        std::string addr;
        GetStrValueBetweenStrs(line, "[", "]", addr);

        int section_id{};
        {
          std::stringstream ss(addr.substr(0, 4));
          if (!(ss >> section_id)) {
            throw std::runtime_error("failed to get section id");
          }
        }

        virtual_address_t local_address{};
        {
          std::stringstream ss;
          ss << std::hex << addr.substr(5, 8);
          if (!(ss >> local_address)) {
            throw std::runtime_error("failed to get local address");
          }
        }

        virtual_address_t method_address = local_address +
                                           h_data_[section_id].virtual_address +
                                           kDefaultBaseAddress;

        MethodInfo mi;
        mi.virtual_address = method_address;
        mi.name = method_name;
        mi.type_id = type_id;
        method_name_to_method_info_map_.insert(std::pair(method_name, mi));
        type_id_to_method_info_map_.insert(std::pair(type_id, mi));
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
void PdbAnalyzer::GetStrValueBetweenStrs(const std::string &line,
                                         const std::string_view &begin,
                                         const std::string_view &end,
                                         std::string &out_str) {
  auto start = line.find(begin);
  out_str = line.substr(start + begin.size());
  auto stop = out_str.find(end);
  out_str = out_str.substr(0, stop);
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

// ============================================================================
std::vector<ClassData> PdbAnalyzer::ConstructClassInfo() {
  std::vector<ClassData> class_info_list;

  for (const auto &class_it : class_type_id_to_class_info_map_) {
    ClassData data;
    data.class_name = class_it.second.class_name;
    data.mangled_class_name = class_it.second.mangled_class_name;

    if (field_list_type_id_to_field_list_map_.find(
            class_it.second.field_list_type_id) ==
        field_list_type_id_to_field_list_map_.end()) {
      throw std::runtime_error("could not find field list for class " +
                               class_it.second.class_name);
    }

    auto &field_list =
        field_list_type_id_to_field_list_map_[class_it.second
                                                  .field_list_type_id];

    std::map<std::string, std::string> incorrect_to_correct_class_name;

    size_t size = field_list.size();
    for (int ii = 0; ii < size; ii++) {
      const auto &field = field_list[ii];

      if (dynamic_cast<const OneMethodFieldList *>(field.get()) != nullptr) {
        const std::string &method_name =
            dynamic_cast<const OneMethodFieldList *>(field.get())->name;

        std::string full_method_name;
        if (incorrect_to_correct_class_name.count(class_it.second.class_name)) {
          full_method_name =
              incorrect_to_correct_class_name[class_it.second.class_name] +
              "::" + method_name;
        } else {
          full_method_name = class_it.second.class_name + "::" + method_name;
        }

        auto mi_it =
            method_name_to_method_info_map_.equal_range(full_method_name);
        if (mi_it.first != mi_it.second) {
          for (auto mi_it2 = mi_it.first; mi_it2 != mi_it.second; mi_it2++) {
            if (field->type_id == mi_it2->second.type_id) {
              MethodInfo mi;
              mi.name = method_name;
              mi.virtual_address = mi_it2->second.virtual_address;
              data.methods.push_back(mi);
              break;
            }
          }
        } else if (type_id_to_method_info_map_.count(field->type_id)) {
          // Finding by method name did not work...try to find by type id
          auto it = type_id_to_method_info_map_.equal_range(field->type_id);
          auto method_found_it = it.first;

          auto suffix_matches = [&]() {
            int method_name_start =
                method_found_it->second.name.size() - method_name.size();
            return method_name_start >= 2 &&
                   method_found_it->second.name.substr(method_name_start) ==
                       method_name &&
                   method_found_it->second.name.substr(method_name_start - 2,
                                                       2) == "::";
          };

          for (; method_found_it != it.second && suffix_matches();
               method_found_it++) {
          }

          if (suffix_matches()) {
            // we found the method...extract prefix from first element
            // (assume duplicate types indicates all methods from same
            // class)
            std::string correct_name = it.first->second.name;
            correct_name = correct_name.substr(
                0, correct_name.size() - method_name.size() - 2);
            incorrect_to_correct_class_name[class_it.second.class_name] =
                correct_name;

            ii--;  // repeat last step and we will find the method now
          }
        }
      } else if (dynamic_cast<const ParentClassFieldList *>(field.get()) !=
                 nullptr) {
        data.mangled_parent_names.push_back(
            forward_ref_type_to_unique_name_map_[field->type_id]);
      } else if (dynamic_cast<const MethodFieldList *>(field.get()) !=
                 nullptr) {
        const std::string &method_name =
            dynamic_cast<const MethodFieldList *>(field.get())->name;

        const auto &method_list =
            method_list_type_id_to_method_list_map_[field->type_id];
        for (type_id_t method_type_id : method_list) {
          field_list.push_back(std::make_shared<OneMethodFieldList>(
              method_type_id, method_name));
          size++;
        }
      } else {
        throw std::runtime_error("invalid field list type ");
      }
    }

    if (data.methods.size() > 0) {
      class_info_list.push_back(data);
    }
  }

  return class_info_list;
}
