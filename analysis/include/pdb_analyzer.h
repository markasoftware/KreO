#pragma once

#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <vector>

using type_id_t = uint32_t;
using virtual_address_t = uint64_t;

struct MethodInfo {
  std::string name;
  virtual_address_t virtual_address{};
  type_id_t type_id{};

  // friend bool operator==(const MethodInfo &mi1, const MethodInfo &mi2) {
  //   return mi1.name == mi2.name && mi1.virtual_address == mi2.virtual_address;
  // }

  friend std::ostream &operator<<(std::ostream &os, const MethodInfo &mi) {
    os << "{'" << mi.name << "'"
       << ", virtual address: 0x" << mi.virtual_address << "}";
    return os;
  }
};

struct ClassData {
  std::string mangled_class_name;
  std::string class_name;
  std::vector<MethodInfo> methods{};
  std::vector<std::string> mangled_parent_names{};
};

struct MethodFieldList {
  std::vector<MethodInfo> method_index_list;

  friend std::ostream &operator<<(std::ostream &os, const MethodFieldList fl) {
    os << "{";
    bool first = true;
    for (const auto it : fl.method_index_list) {
      if (!first) {
        os << ", ";
      } else {
        first = false;
      }
      os << it;
    }
    os << "}";
    return os;
  }
};

struct HeaderData {
  std::string name;
  virtual_address_t virtual_size{};
  virtual_address_t virtual_address{};

  friend std::ostream &operator<<(std::ostream &os, const HeaderData &hd) {
    os << "{name: " << hd.name << ", virtual size: 0x" << std::hex
       << hd.virtual_size << ", virtual address: 0x" << hd.virtual_address
       << std::dec << "}";
    return os;
  }
};

class PdbAnalyzer {
 public:
  void AnalyzePdbDump(const std::string &fname);

  std::vector<ClassData> ConstructClassInfo();

 private:
  static constexpr virtual_address_t kDefaultBaseAddress{0x00400000};

  static constexpr std::string_view kTypesSection{"*** TYPES"};
  static constexpr std::string_view kSectionHeadersSection{
      "*** SECTION HEADERS"};
  static constexpr std::string_view kOriginalSectionHeaders{
      "*** ORIGINAL SECTION HEADERS"};
  static constexpr std::string_view kPublicsSection{"*** PUBLICS"};
  static constexpr std::string_view kSymbolsSection{"*** SYMBOLS"};
  static constexpr std::string_view kClassId{"LF_CLASS"};
  static constexpr std::string_view kStructureId{"LF_STRUCTURE"};
  static constexpr std::string_view kGlobals{"*** GLOBALS"};
  static constexpr std::string_view kFieldListId{"LF_FIELDLIST"};
  static constexpr std::string_view kBaseClassId{" LF_BCLASS"};

  static constexpr std::string_view kModuleSection{"** Module: "};

  static constexpr std::string_view kSectionHeaderNum{"SECTION HEADER #"};

  static constexpr std::string_view kForwardRef{", FORWARD REF, "};
  static constexpr std::string_view kStaticId{", STATIC, "};
  static constexpr std::string_view kOneMethod{"= LF_ONEMETHOD, "};

  static constexpr std::string_view kFieldListTypeId{"field list type "};

  static constexpr std::string_view kClassNameId{"class name = "};
  static constexpr std::string_view kFieldIndexId{"index = "};
  static constexpr std::string_view kNameId{"name = "};
  static constexpr std::string_view kUniqueName{"unique name = "};
  static constexpr std::string_view kUdt{", UDT"};
  static constexpr std::string_view kTypeId{"type = "};

  static constexpr std::string_view kBlank{""};

  static constexpr std::string_view kThisCall{"__thiscall"};

  static constexpr std::string_view kGproc32{"S_GPROC32"};

  static constexpr std::string_view kNoType{"T_NOTYPE"};

  /// @brief Identify all types in the pdb file
  /// @param stream
  void FindTypeInfo(std::fstream &fstream);

  /// @brief Identify section headers in the pdb file
  void FindSectionHeaders(std::fstream &fstream);

  /// @brief Identify all publics in the pdb file
  void FindSymbols(std::fstream &fstream);

  /// @brief Identify all inheritance relationships in the pdb file. Must be
  /// called after FindTypeInfo.
  void FindInheritanceRelationships(std::fstream &fstream);

  static void SeekToSectionHeader(std::fstream &fstream,
                                  const std::string_view &header);

  static void GetHexValueAfterString(const std::string &line,
                                     const std::string_view &str,
                                     uint32_t &out_value);

  static void GetStrValueAfterString(const std::string &line,
                                     const std::string_view &str,
                                     std::string &out_str);

  static void GetStrValueBetweenStrs(const std::string &line,
                                     const std::string_view &begin,
                                     const std::string_view &end,
                                     std::string &out_str);

  static void GetQuotedStrAfterString(const std::string &line,
                                      const std::string_view &str,
                                      std::string &out_str);

  /// @brief checks to see if string contains substr, returning true if str
  /// contains substr, false otherwise.
  static bool Contains(const std::string &str, const std::string_view &substr);

  /// @brief Iterates the stream to the next type. Assumed that the stream is at
  /// some previous type or between types. For example, given the stream pointer
  /// is pointing to the start of the following example:
  ///
  /// ```
  ///   # members = 1,  field list type 0x10e6,
  ///   Derivation list type 0x0000, VT shape type 0x0000
  ///   Size = 1, class name = __scrt_no_argv_policy, unique name =
  ///   ?AU__scrt_no_argv_policy@@, UDT(0x000010e7)
  ///
  /// ```
  ///
  /// Then the stream will be moved past the lines above to the last empty
  /// line.
  ///
  /// @param stream The stream to iterate through
  /// @param last_line The last line that was read from the stream
  /// @return True if a new type was found, false if not (the fstream reached
  /// the end of the file or some other issue with the file stream).
  static bool IterateToNewType(std::fstream &stream,
                               const std::string &last_line);

  /// @brief Strict checking of std::getline. Calls std::getline and throws
  /// runtime_error if std::getline failed.
  static void MustGetLine(std::fstream &stream, std::string &out_str,
                          const std::string &error_str);

  struct ClassInfo {
    std::string mangled_class_name;
    std::string class_name;
    type_id_t field_list_type_id{};
    std::set<type_id_t> parent_class_type_id_set{};

    virtual_address_t virtual_address{};

    friend std::ostream &operator<<(std::ostream &os, const ClassInfo &ci) {
      os << "{'" << ci.mangled_class_name << "' class name: " << ci.class_name
         << ", virtual address: " << ci.virtual_address << ", field list: 0x"
         << std::hex << ci.field_list_type_id << std::dec << "}";
      return os;
    }
  };

  struct FieldListMember {
    FieldListMember(type_id_t a_type_id) : type_id(a_type_id) {}

    virtual ~FieldListMember() = default;

    type_id_t type_id{};
  };

  struct OneMethodFieldList : public FieldListMember {
    OneMethodFieldList(type_id_t a_type_id, const std::string &a_name)
        : FieldListMember(a_type_id),
          name(a_name) {}

    std::string name;
  };

  struct ParentClassFieldList : public FieldListMember {
    using FieldListMember::FieldListMember;
  };

  struct MethodFieldList : public FieldListMember {
    MethodFieldList(type_id_t a_type_id, const std::string &a_name)
        : FieldListMember(a_type_id),
          name(a_name) {}

    std::string name;
  };

  std::map<type_id_t, ClassInfo> class_type_id_to_class_info_map_;
  std::map<type_id_t, std::string> forward_ref_type_to_unique_name_map_;
  std::map<type_id_t, std::vector<std::shared_ptr<FieldListMember>>>
      field_list_type_id_to_field_list_map_;
  std::map<type_id_t, std::vector<type_id_t>>
      method_list_type_id_to_method_list_map_;
  std::multimap<std::string, MethodInfo> method_name_to_method_info_map_;
  std::multimap<type_id_t, MethodInfo> type_id_to_method_info_map_;





  // ------------

  std::shared_ptr<std::map<type_id_t, ClassInfo>> ci_;
  std::shared_ptr<std::map<std::string, MethodFieldList>> ml_;

  std::map<type_id_t, type_id_t> fieldlist_to_class_id_map_;
  std::map<std::string, type_id_t> unique_name_to_type_id_;

  std::map<int, HeaderData> h_data_;

  std::map<type_id_t, virtual_address_t> type_id_to_virtual_address_map_;
};
