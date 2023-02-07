#include "type_data_instances.h"

#include <cassert>
#include <iostream>

#include "utils.h"

// ============================================================================
void ClassTypeData::Parse(const std::vector<std::string> &lines) {
  if (lines.size() != 4) {
    throw std::runtime_error(
        "when parsing LF_CLASS type, it must have exactly 4 lines");
  }
  ParseFirstLine(lines[0]);

  forward_ref_ = lines[1].find("FORWARD REF") != std::string::npos;

  members_ = GetDecBetween(lines[1], "members = ", ",  ");
  field_list_type_ = GetHexAfter(lines[1], "field list type ");
  derivation_list_type_ = GetHexAfter(lines[2], "Derivation list type ");
  vt_shape_type_ = GetHexAfter(lines[2], "VT shape type ");
  size_ = GetHexAfter(lines[3], "Size = ");
  class_name_ = GetStrBetween(lines[3], "class name = ", ", unique name = ");
  try {
    unique_name_ = GetStrBetween(lines[3], ", unique name = ", ", UDT(");
    udt_ = GetHexBetween(lines[3], "UDT(", ")");
  } catch (std::runtime_error e) {
    // Sometimes UDT not present...not sure if these classes should be
    // included.
    unique_name_ = GetStrAfter(lines[3], ", unique name = ");
  }
}

// ============================================================================
void ClassTypeData::to_string(std::ostream &os) const {
  os << "class: {forward ref: " << forward_ref_ << ", members: " << members_
     << std::hex << ", field list: 0x" << field_list_type_
     << ", derivation list: 0x" << derivation_list_type_ << ", vt shape: 0x"
     << vt_shape_type_ << ", size: " << std::dec << size_
     << ", class name: " << class_name_ << ", unique name: " << unique_name_
     << ", UDT: 0x" << std::hex << udt_ << std::dec << "}";
}

// ============================================================================
void FieldListTypeData::Parse(const std::vector<std::string> &lines) {
  ParseFirstLine(lines[0]);

  for (const std::string &line : lines) {
    std::string identifier = GetNthStr(line, 2);
    identifier = identifier.substr(0, identifier.size() - 1);
    if (identifier == "LF_BCLASS") {
      base_classes_.insert(GetHexAfter(line, "type = "));
    } else if (identifier == "LF_METHOD") {
      MethodListInfo mi;
      mi.index = GetHexBetween(line, "list = ", ",");
      mi.name = GetStrBetween(line, "name = '", "'");

      method_lists_.insert(mi);
    } else if (identifier == "LF_ONEMETHOD") {
      MethodInfo mi;
      mi.type = GetNthStr(line, 2, ',');
      // Remove space in front of type
      mi.type = mi.type.substr(1);
      mi.index = GetHexBetween(line, "index = ", ",");
      mi.name = GetStrBetween(line, "name = '", "'");
      methods_.insert(mi);
    }
  }
}

// ============================================================================
template <typename T>
void SetToString(
    std::ostream &os,
    std::function<void(const T &, std::ostream &)> element_to_string,
    const std::set<T> &s) {
  bool first = true;
  os << "{";
  for (const auto &element : s) {
    if (first) {
      first = false;
    } else {
      os << ", ";
    }

    element_to_string(element, os);
  }
  os << "}";
}

// ============================================================================
void FieldListTypeData::to_string(std::ostream &os) const {
  os << "fieldlist: {base classes: ";
  SetToString<size_t>(
      os,
      [](const size_t &type_id, std::ostream &os) { os << "0x" << type_id; },
      base_classes_);

  os << ", methods: ";

  SetToString<MethodInfo>(
      os, [](const MethodInfo &mi, std::ostream &os) { os << mi; }, methods_);

  os << ", method lists: ";

  SetToString<MethodListInfo>(
      os,
      [](const MethodListInfo &mi, std::ostream &os) { os << mi; },
      method_lists_);

  bool first = true;
  os << "fieldlist: {";
  for (size_t class_type : base_classes_) {
    if (first) {
      first = false;
    } else {
      os << ", ";
    }

    os << "0x" << class_type;
  }
  os << "}";
}

// ============================================================================
void MethodListTypeData::Parse(const std::vector<std::string> &lines) {
  for (size_t ii = 1; ii < lines.size(); ii++) {
    std::string line = lines[ii];

    std::vector<std::string> method_parameters;

    try {
      for (size_t jj = 1;; jj++) {
        std::string param = GetNthStr(line, jj, ',');
        method_parameters.push_back(param);
      }
    } catch (std::runtime_error) {
    }

    std::string method_type = method_parameters[0];
    if (method_type == "VANILLA" || method_type == "VIRTUAL" ||
        method_type == "INTRODUCING VIRTUAL") {
    }

    // The hex string is going to be either the 1st or 2nd method parameter (0
    // indexed).
    size_t method_type_id{};
    try {
      method_type_id = GetFirstHex(method_parameters[1]);
    } catch (std::runtime_error) {
      method_type_id = GetFirstHex(method_parameters[2]);
    }
    method_list_.insert(method_type_id);
  }
}

// ============================================================================
void MethodListTypeData::to_string(std::ostream &os) const {
  os << "method list: {";
  if (method_list_.size() > 1) {
    auto it = method_list_.begin();
    os << std::hex << *it;
    for (; it != method_list_.end(); it++) {
      os << ", 0x" << *it;
    }
    os << std::dec;
  }
  os << "}";
}

// ============================================================================
static ProcedureTypeData::FuncAttr StrToFuncAttr(const std::string &str) {
  std::string str_trim = trim_copy(str);

  if (str_trim == "none") {
    return ProcedureTypeData::FuncAttr::kNone;
  } else if (str_trim == "return UDT (C++ style)") {
    return ProcedureTypeData::FuncAttr::kReturnUdt;
  } else if (str_trim == "instance constructor") {
    return ProcedureTypeData::FuncAttr::kInstanceConstructor;
  } else if (str_trim == "****Warning**** unused field non-zero!") {
    return ProcedureTypeData::FuncAttr::kUnusedNonzero;
  }

  assert(false);
}

// ============================================================================
void ProcedureTypeData::Parse(const std::vector<std::string> &lines) {
  ParseFirstLine(lines[0]);

  return_type_ = GetStrBetween(lines[1], "Return type = ", ", ");

  class_type_ref_ = GetHexAfter(lines[1], "Class type = ");

  try {
    this_type_ = GetHexAfter(lines[1], "This type = ");
  } catch (std::runtime_error) {
    if (GetStrAfter(lines[1], "This type = ") != "T_NOTYPE(0000), ") {
      throw std::runtime_error(
          "failed to find this type and not T_NOTYPE, from line \"" + lines[1] +
          "\"");
    }
  }

  call_type_ = GetStrAfter(lines[2], "Call type = ");
  call_type_ = call_type_.substr(0, call_type_.size());

  func_attr_ = StrToFuncAttr(GetNthStr(GetNthStr(lines[2], 1, ','), 1, '='));

  params_ = GetDecAfter(lines[3], "Parms = ");
  arg_list_type_ = GetHexAfter(lines[3], "Arg list type = ");
  this_adjust_ = GetDecAfter(lines[3], "This adjust = ");
}

// ============================================================================
#define CASE(attr)                        \
  case ProcedureTypeData::FuncAttr::attr: \
    return #attr;

static const char *FuncAttrToStr(ProcedureTypeData::FuncAttr attr) {
  switch (attr) {
    CASE(kNone)
    CASE(kInstanceConstructor)
    CASE(kReturnUdt)
    CASE(kUnusedNonzero)
  }
  assert(false);
}

// ============================================================================
void ProcedureTypeData::to_string(std::ostream &os) const {
  os << "procedure: {return type: " << return_type_ << std::hex
     << ", class type: 0x" << class_type_ref_ << ", this type: 0x" << this_type_
     << ", call type: " << call_type_
     << ", func attr: " << FuncAttrToStr(func_attr_) << ", params: " << std::dec
     << params_ << std::hex << ", arg list type: 0x" << arg_list_type_
     << std::dec << ", this adjust: " << this_adjust_ << "}";
}
