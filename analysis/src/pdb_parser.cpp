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
      types_list_.push_back(*type_data);
    }
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
std::string PdbParser::GetNextLine() {
  std::string line;
  if (!std::getline(pdb_, line)) {
    throw std::runtime_error("failed to get next line");
  }
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
