#include "pdb_analyzer.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "llvm/Demangle/Demangle.h"

static constexpr std::string_view kTypesSection{"*** TYPES"};
static constexpr std::string_view kSectionHeadersSection{"*** SECTION HEADERS"};
static constexpr std::string_view kOriginalSectionHeaders{
    "*** ORIGINAL SECTION HEADERS"};
static constexpr std::string_view kPublicsSection{"*** PUBLICS"};
static constexpr std::string_view kClassId{"LF_CLASS"};
static constexpr std::string_view kFieldListId{"LF_FIELDLIST"};

static constexpr std::string_view kSectionHeaderNum{"SECTION HEADER #"};

static constexpr std::string_view kForwardRef{", FORWARD REF, "};
static constexpr std::string_view kStaticId{", STATIC, "};
static constexpr std::string_view kOneMethod{"= LF_ONEMETHOD, "};

static constexpr std::string_view kFieldListTypeId{"field list type "};

static constexpr std::string_view kClassNameId{"class name = "};
static constexpr std::string_view kFieldIndexId{"index = "};
static constexpr std::string_view kNameId{"name = "};
static constexpr std::string_view kUniqueName{"unique name = "};

static constexpr std::string_view kBlank{""};

static constexpr std::string_view kThisCall{"__thiscall"};

// ============================================================================
void PdbAnalyzer::AnalyzePdbDump(const std::string &fname) {
  std::fstream fstream(fname);

  if (!fstream.is_open()) {
    throw std::runtime_error("failed to open file named " + fname);
  }

  fstream.seekg(std::fstream::beg);
  FindTypes(fstream);
  fstream.seekg(std::fstream::beg);
  FindSectionHeaders(fstream);
  fstream.seekg(std::fstream::beg);
  FindPublics(fstream);
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
      // trim name to remove trailing ,
      ci.class_name = ci.class_name.substr(0, ci.class_name.size() - 1);

      // Extract unique name
      GetStrValueAfterString(line, kUniqueName, ci.mangled_class_name);
      // trim mangled name to remove trailing ,
      ci.mangled_class_name =
          ci.mangled_class_name.substr(0, ci.mangled_class_name.size() - 1);

      // Insert class info to class info map
      ci_->insert(std::pair(type_index, ci));
    } else if (Contains(line, kFieldListId)) {
      MethodList ml;

      // Extract method type index
      type_id_t method_type_index{};
      GetHexValueAfterString(line, kBlank, method_type_index);

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

          GetQuotedStrAfterString(line, kNameId, mi.name);

          ml.method_index_list.push_back(mi);
        } else if (line == kBlank) {
          break;
        }
      }

      if (ml.method_index_list.size() > 0) {
        ml_->insert(std::pair(method_type_index, ml));
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
void PdbAnalyzer::FindPublics(std::fstream &fstream) {
  SeekToSectionHeader(fstream, kPublicsSection);

  std::string line;
  MustGetLine(
      fstream, line, "failed to seek past blank line between header and data");
  MustGetLine(
      fstream, line, "failed to seek past blank line between header and data");

  while (line != kBlank) {
    std::vector<std::string> strs;
    {
      std::stringstream ss(line);
      std::string next_str;
      while (ss >> next_str) {
        strs.push_back(next_str);
      }
    }
    // Extract section id and address within section
    int section_id{};
    {
      std::stringstream ss(strs[1].substr(1, 4));
      if (!(ss >> section_id)) {
        throw std::runtime_error("failed to get section id");
      }
    }

    virtual_address_t local_address{};
    {
      std::stringstream ss;
      ss << std::hex << strs[1].substr(6, 8);
      if (!(ss >> local_address)) {
        throw std::runtime_error("failed to get local address");
      }
    }

    // Compute offset virtual address
    virtual_address_t offset_address = local_address +
                                       h_data_[section_id].virtual_address +
                                       kDefaultBaseAddress;

    // Extract demangled name (if any)
    // TODO make not path dependent
    // std::string demumble_cmd = "./demumble " + strs[4];
    // FILE *res_fd = popen(demumble_cmd.c_str(), "r");
    // if (!res_fd) {
    //   throw std::runtime_error("popen failed");
    // }
    // char buffer[1024];
    // char *line_p = fgets(buffer, sizeof(buffer), res_fd);
    // pclose(res_fd);

    // Associated name with method
    // std::cout << buffer << ", " << std::hex << offset_address << std::dec
    //           << std::endl;

    // Find class name in name
    // std::string dmangled_name(buffer);

    std::string mangled_name = strs[4];

    size_t n_mangled{};
    char *unmangled_name = llvm::microsoftDemangle(
        mangled_name.c_str(),
        &n_mangled,
        NULL,
        NULL,
        NULL,
        (llvm::MSDemangleFlags)(
            // (int)llvm::MSDF_NoCallingConvention |
            (int)llvm::MSDF_NoAccessSpecifier | (int)llvm::MSDF_NoReturnType |
            (int)llvm::MSDF_NoMemberType | (int)llvm::MSDF_NoVariableType));
    if (unmangled_name != nullptr) {
      std::stringstream ss(unmangled_name);
      std::cout << "^^^" << unmangled_name << std::endl;
      free(unmangled_name);
      // Get class and method name
      std::string calling_convention;
      std::string class_name;
      std::string method_name;

      ss >> calling_convention;

      if (calling_convention != kThisCall || !(ss >> class_name) ||
          !(ss >> method_name)) {
        MustGetLine(fstream, line, "failed to get public element");
        continue;
      }

      std::cout << mangled_name << ", " << class_name << ", " << method_name
                << std::endl;
    }

    // auto pos = mangled_name.find_first_of('?');
    // if (pos != 0) {
    //   // not a method
    //   MustGetLine(fstream, line, "failed to get public element");
    //   continue;
    // }
    // mangled_name = mangled_name.substr(pos + 1);

    // pos = mangled_name.find_first_of('@');
    // if (pos == std::string::npos) {
    //   throw std::runtime_error("demangling failed for mangled name " +
    //   strs[4]);
    // }
    // std::string method_name = mangled_name.substr(0, pos);

    // mangled_name = mangled_name.substr(pos);
    // pos = mangled_name.find_first_of('@');
    // if (pos != 0) {
    //   throw std::runtime_error("demangling failed for mangled name " +
    //   strs[4]);
    // }
    // mangled_name = mangled_name.substr(pos + 1);

    // pos = mangled_name.find_first_of("@@");
    // if (pos == std::string::npos) {
    //   throw std::runtime_error("demangling failed for mangled name " +
    //   strs[4]);
    // }
    // std::string class_name = mangled_name.substr(0, pos);

    // std::cout << strs[4] << ", " << method_name << ", " << class_name
    //           << std::endl;

    MustGetLine(fstream, line, "failed to get public element");
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
