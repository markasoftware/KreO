#include "pdb_parser.h"

#include <cassert>

#include "type_data_instances.h"
#include "utils.h"

// ============================================================================
enum class TypeId {
  kMemberFunction,
  kClass,
  kFieldList,
  kMethodList,
  kUnknown,
};

// ============================================================================
static TypeId ToTypeId(const std::string &str) {
  if (str == "LF_MFUNCTION") {
    return TypeId::kMemberFunction;
  }
  if (str == "LF_CLASS") {
    return TypeId::kClass;
  }
  if (str == "LF_FIELDLIST") {
    return TypeId::kFieldList;
  }
  if (str == "LF_MFUNCTION") {
    return TypeId::kMemberFunction;
  }
  if (str == "LF_METHODLIST") {
    return TypeId::kMethodList;
  }
  return TypeId::kUnknown;
}

// ============================================================================
PdbParser::PdbParser(const std::string &fname) : pdb_(fname) {
  if (!pdb_.is_open()) {
    throw std::runtime_error("failed to open file named " + fname);
  }
}

// ============================================================================
void PdbParser::ParseTypeData() {
  SeekToTypes();

  std::optional<std::shared_ptr<TypeData>> type_data;
  while ((type_data = GetNextTypeData()) != std::nullopt) {
    if (*type_data != nullptr) {
      auto td = *type_data;
      type_to_typedata_map_[td->get_type()] = td;
    }
  }
}

// ============================================================================
void PdbParser::ParseSectionHeaders() {
  SeekToSectionHeaders();

  std::optional<SectionHeaderInfo> info;
  while ((info = GetNextSectionHeader()) != std::nullopt) {
    header_info_[info->header_num] = *info;
  }
}

// ============================================================================
void PdbParser::ParseSymbols() {
  if (type_to_typedata_map_.empty()) {
    throw std::runtime_error("must call ParseTypeData() before ParseSymbols()");
  }
  while (PeekNextLine() != "*** GLOBALS") {
    HandleNextSymbol();
  }
}

// ============================================================================
std::optional<std::shared_ptr<TypeData>> PdbParser::GetNextTypeData() {
  // Find the next lines associated with the current type
  std::vector<std::string> type_data;
  type_data.push_back(GetNextLine());
  while (!type_data.rbegin()->empty()) {
    std::string line = GetNextLine();
    if (line.find("\t\t") == 0) {
      // If the line is a continuation of the previous line, append it on to
      // the end of the last string in type_data.
      type_data.rbegin()->append(" " + line);
    } else {
      type_data.push_back(line);
    }
  }
  type_data.pop_back();

  if (type_data.empty()) {
    // nothing to parse, likely no more type data left to parse.
    return std::nullopt;
  }

  // type is always the 8th space separated string in the
  // first line of the type information.
  TypeId type = ToTypeId(GetNthStr(type_data[0], 8));

  std::shared_ptr<TypeData> data = nullptr;

  switch (type) {
    case TypeId::kMemberFunction:
      data = std::make_shared<ProcedureTypeData>();
      break;
    case TypeId::kClass:
      data = std::make_shared<ClassTypeData>();
      break;
    case TypeId::kFieldList:
      data = std::make_shared<FieldListTypeData>();
      break;
    case TypeId::kMethodList:
      data = std::make_shared<MethodListTypeData>();
      break;
    case TypeId::kUnknown:
      break;
    default:
      assert(false);
  }

  if (data != nullptr) {
    data->Parse(type_data);
  }

  return data;
}

// ============================================================================
std::optional<PdbParser::SectionHeaderInfo> PdbParser::GetNextSectionHeader() {
  std::vector<std::string> header_strs;
  header_strs.push_back(GetNextLine());
  while (header_strs.rbegin()->empty()) {
    header_strs.push_back(GetNextLine());
  }
  header_strs.pop_back();

  if (header_strs.empty()) {
    return std::nullopt;
  }

  size_t header_num = GetHexAfter(header_strs[0], "SECTION HEADER #");
  size_t virtual_size = GetFirstHex(header_strs[2]);
  size_t virtual_addr = GetFirstHex(header_strs[3]);

  return SectionHeaderInfo{
      .header_num = header_num,
      .virtual_size = virtual_size,
      .virtual_addr = virtual_addr,
  };
}

// ============================================================================
void PdbParser::HandleNextSymbol() {
  std::vector<std::string> cur_symbol;
  cur_symbol.push_back(GetNextLine());
  while (PeekNextLine().at(0) != '(') {
    cur_symbol.push_back(GetNextLine());
  }

  if (StrContains(cur_symbol[0], "S_GPROC32") ||
      StrContains(cur_symbol[0], "S_LPROC32")) {
    // Extract section header and relative address
    std::string addr_str = GetStrBetween(cur_symbol[0], "[", "]");

    size_t section_header = GetFirstHex(addr_str);
    size_t relative_addr = GetHexAfter(addr_str, ":");

    size_t addr = header_info_[section_header].virtual_addr +
                  relative_addr + kBaseAddr;

    size_t type = GetHexAfter(cur_symbol[0], "Type:");

    if (type_to_typedata_map_.count(type) == 0) {
      throw std::runtime_error("found symbol whose associated type does not exist");
    }

    type_to_typedata_map_[type]->set_addr(addr);
  }
}

// ============================================================================
std::string PdbParser::GetNextLine() {
  std::string line;
  if (!std::getline(pdb_, line)) {
    throw std::runtime_error("failed to get next line");
  }
  return line;
}

// ============================================================================
std::string PdbParser::PeekNextLine() {
  std::streampos len = pdb_.tellg();
  std::string line = GetNextLine();
  pdb_.seekg(len, std::ios_base::beg);
  return line;
}

// ============================================================================
void PdbParser::SeekToSectionHeaderStart(const std::string_view &header) {
  pdb_.seekg(std::fstream::beg);
  while (true) {
    std::string next = GetNextLine();
    if (next == header) {
      // Iterate to one line past the header since there is always a blank
      // line following the header.
      GetNextLine();
      return;
    }
  }
}
