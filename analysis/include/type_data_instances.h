#pragma once

#include "type_data.h"
#include <vector>
#include <string>
#include <set>

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
  void Parse(const std::vector<std::string> &lines) override;

  void to_string(std::ostream &os) const override;

  const std::set<size_t> &get_base_classes() const { return base_classes_; }

  class MethodInfo {
    bool public_;
    std::string type_;
    size_t index_;
    std::string name_;
  };

 private:
  std::set<size_t> base_classes_;
  std::set<MethodInfo> methods_;
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
