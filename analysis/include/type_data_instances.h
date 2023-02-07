#pragma once

#include <set>
#include <string>
#include <vector>

#include "type_data.h"

// ============================================================================
class ClassTypeData : public TypeData {
 public:
  void Parse(const std::vector<std::string> &lines) override;

  void to_string(std::ostream &os) const override;

  bool get_forward_ref() const { return forward_ref_; }
  size_t get_members() const { return members_; }
  size_t get_field_list_type() const { return field_list_type_; }
  size_t get_derivation_list_type() const { return derivation_list_type_; }
  size_t get_vt_shape_type() const { return vt_shape_type_; }
  size_t get_size() const { return size_; }
  std::string get_class_name() const { return class_name_; }
  std::string get_unique_name() const { return unique_name_; }
  size_t get_udt() const { return udt_; }

 private:
  bool forward_ref_{false};
  size_t members_{};
  size_t field_list_type_{};
  size_t derivation_list_type_{};
  size_t vt_shape_type_{};
  size_t size_{};
  std::string class_name_;
  std::string unique_name_;
  size_t udt_{};  ///< User defined type ID
};

// ============================================================================
class FieldListTypeData : public TypeData {
 public:
  struct MethodInfo {
    std::string type;
    size_t index;
    std::string name;

    friend bool operator<(const MethodInfo &a, const MethodInfo &b) {
      return a.type < b.type || a.index < b.index || a.name < b.name;
    }

    friend std::ostream &operator<<(std::ostream &os, const MethodInfo &mi) {
      os << "{type: " << mi.type << ", index: 0x" << std::hex << mi.index
         << ", name: " << mi.name << "}" << std::dec;
      return os;
    }
  };

  struct MethodListInfo {
    size_t index;
    std::string name;

    friend bool operator<(const MethodListInfo &a, const MethodListInfo &b) {
      return a.index < b.index || a.name < b.name;
    }

    friend std::ostream &operator<<(std::ostream &os,
                                    const MethodListInfo &mi) {
      os << "{index: " << std::hex << mi.index << ", name: " << mi.name
         << std::dec << "}";
      return os;
    }
  };

  void Parse(const std::vector<std::string> &lines) override;

  void to_string(std::ostream &os) const override;

  const std::set<size_t> &get_base_classes() const { return base_classes_; }

  const std::set<MethodInfo> get_methods() const { return methods_; }

  const std::set<MethodListInfo> get_method_lists() const {
    return method_lists_;
  }

 private:
  std::set<size_t> base_classes_;
  std::set<MethodInfo> methods_;
  std::set<MethodListInfo> method_lists_;
};

// ============================================================================
class MethodListTypeData : public TypeData {
 public:
  void Parse(const std::vector<std::string> &lines) override;

  void to_string(std::ostream &os) const override;

 private:
  std::set<size_t> method_list_;
};

// ============================================================================
class ProcedureTypeData : public TypeData {
 public:
  void Parse(const std::vector<std::string> &lines) override;

  void to_string(std::ostream &os) const override;

  enum class FuncAttr {
    kNone,
    kInstanceConstructor,
    kReturnUdt,
    kUnusedNonzero,
  };

  const std::string &get_return_type() const { return return_type_; }
  size_t get_class_type_ref() const { return class_type_ref_; }
  size_t get_this_type() const { return this_type_; }
  const std::string &get_call_type() const { return call_type_; }
  FuncAttr get_func_attr() const { return func_attr_; }
  size_t get_params() const { return params_; }
  size_t get_arg_list_type() const { return arg_list_type_; }
  size_t get_this_adjust() const { return this_adjust_; }

 private:
  std::string return_type_;
  size_t class_type_ref_;
  size_t this_type_;
  std::string call_type_;
  FuncAttr func_attr_;
  size_t params_;
  size_t arg_list_type_;
  size_t this_adjust_;
};
