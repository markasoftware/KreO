#include <boost/json.hpp>
#include <iostream>
#include <map>
#include <set>
#include <string_view>

#include "json_loader.h"

using virtual_address_t = uint64_t;

static constexpr std::string_view kConstructorType{"ctor"};
static constexpr std::string_view kDestructorType{"dtor"};

static constexpr size_t kBaseAddr{0x400000};

// ============================================================================
struct MethodInfo {
  virtual_address_t address;
  std::string type;

  friend bool operator<(const MethodInfo &mi1, const MethodInfo &mi2) {
    if (mi1.address != mi2.address) {
      return mi1.address < mi2.address;
    }

    return mi1.type.compare(mi2.type) < 0;
  }

  friend bool operator==(const MethodInfo &mi1, const MethodInfo &mi2) {
    return mi1.address == mi2.address && mi1.type == mi2.type;
  }
};

// ============================================================================
struct ClassInfo {
  std::string mangled_name;
  std::vector<std::string> parent_mangled_names;
  std::set<MethodInfo> method_set;
};

// ============================================================================
/// @brief Converts json to vector of method sets whose set elements are method
/// addresses that have been associated with some particular class.
static std::vector<ClassInfo> ToClassInfo(const boost::json::value &json);

// ============================================================================
static std::vector<ClassInfo> LoadAndConvertJson(const std::string &json_str);

// ============================================================================
static std::set<MethodInfo> GetTypeSet(const std::vector<ClassInfo> &in,
                                       const std::string_view &type);

// ============================================================================
/// @brief Computes precision and recall on the generated methods. The ground
/// truth for evaluation is all methods in the ground truth. A true positive is
/// a correctly identified method. A false positive is a method that is not in
/// the ground truth. A false negative is a method that was not identified.
/// @return pair, first element precision second element recall. These elements
/// are guaranteed to be between [0, 1].
static std::pair<float, float> PrecisionAndRecallMethods(
    const std::vector<ClassInfo> &ground_truth,
    const std::vector<ClassInfo> &generated);

// ============================================================================
static std::pair<float, float> PrecisionAndRecallClasses(
    const std::vector<ClassInfo> &ground_truth,
    const std::vector<ClassInfo> &generated_data);

// ============================================================================
static std::pair<float, float> PrecisionAndRecallConstructors(
    const std::vector<ClassInfo> &ground_truth,
    const std::vector<ClassInfo> &generated_data);

// ============================================================================
static std::pair<float, float> PrecisionAndRecallDestructors(
    const std::vector<ClassInfo> &ground_truth,
    const std::vector<ClassInfo> &generated_data);

// ============================================================================
static std::pair<float, float> PrecisionAndRecallMethodsAssignedCorrectClass(
    const std::vector<ClassInfo> &ground_truth,
    const std::vector<ClassInfo> &generated_data);

// ============================================================================
static std::pair<float, float> PrecisionAndRecallClassGraphEdges(
    const std::vector<ClassInfo> &ground_truth,
    const std::vector<ClassInfo> &generated_data);

// ============================================================================
static std::pair<float, float> PrecisionAndRecallClassGraphAncestors(
    const std::vector<ClassInfo> &ground_truth,
    const std::vector<ClassInfo> &generated_data);

// ============================================================================
static std::set<std::pair<const ClassInfo *, const ClassInfo *>>
MatchGenToGtClasses(const std::vector<ClassInfo> &ground_truth,
                    const std::vector<ClassInfo> &generated_data);

// ============================================================================
static float ComputeF1(float precision, float recall);

// ============================================================================
static std::set<virtual_address_t> LoadAndRecordGtMethodStats(
    const std::string &gt_methods_instrumented_path,
    const std::vector<ClassInfo> &ground_truth);

// ============================================================================
static std::vector<ClassInfo> GetGtClassInfoInstrumentedList(
    std::set<virtual_address_t> gt_methods_instrumented,
    std::vector<ClassInfo> &gt_class_info_list);

// ============================================================================
int main(int argc, char *argv[]) {
  if (argc != 6) {
    std::cerr << "Usage: ./evaluation <path-to-ground-truth-json> "
                 "<path-to-generated-json> "
                 "<path-to-gt-methods-instrumented> <analysis-gt-out-path> "
                 "<analysis-gt-out-instrumented-path>"
              << std::endl;
    return EXIT_FAILURE;
  }

  auto gt_class_info_list = LoadAndConvertJson(argv[1]);
  auto gen_class_info_list = LoadAndConvertJson(argv[2]);
  std::set<virtual_address_t> gt_methods_instrumented =
      LoadAndRecordGtMethodStats(argv[3], gt_class_info_list);

  std::string gt_out_path(argv[4]);
  std::string gt_out_instrumented_path(argv[5]);

  auto run_all_tests = [&](const std::vector<ClassInfo> &gt_class_info_list,
                           std::ofstream &ostream) {
    ostream << "evaluation criteria\tprecision\trecall\tf-score" << std::endl;

    using PrecisionRecallFn = std::function<std::pair<float, float>(
        const std::vector<ClassInfo> &, const std::vector<ClassInfo> &)>;
    auto run_test = [&](const std::string &name, PrecisionRecallFn test) {
      auto precision_recall = test(gt_class_info_list, gen_class_info_list);

      float f_score =
          ComputeF1(precision_recall.first, precision_recall.second);

      ostream << std::fixed << std::setprecision(2) << name << "&"
              << precision_recall.first << "&" << precision_recall.second << "&"
              << f_score << std::endl;
    };

    run_test("Methods Assigned to Correct Class",
             PrecisionAndRecallMethodsAssignedCorrectClass);
    run_test("Individual Classes", PrecisionAndRecallClasses);
    run_test("Constructors", PrecisionAndRecallConstructors);
    run_test("Destructors", PrecisionAndRecallDestructors);
    run_test("Methods", PrecisionAndRecallMethods);
    // run_test("Class Graph Edges", PrecisionAndRecallClassGraphEdges);
    run_test("Class Graph Ancestors", PrecisionAndRecallClassGraphAncestors);
  };

  std::ofstream gt_out(gt_out_path);
  run_all_tests(gt_class_info_list, gt_out);

  // auto gt_class_info_instrumented_list = GetGtClassInfoInstrumentedList(
  //     gt_methods_instrumented, gt_class_info_list);

  // std::ofstream gt_out_instrumented(gt_out_instrumented_path);
  // run_all_tests(gt_class_info_instrumented_list, gt_out_instrumented);
}

// ============================================================================
static std::vector<ClassInfo> GetGtClassInfoInstrumentedList(
    std::set<virtual_address_t> gt_methods_instrumented,
    std::vector<ClassInfo> &gt_class_info_list) {
  std::vector<ClassInfo> gt_class_info_instrumented;

  for (const ClassInfo &ci : gt_class_info_list) {
    decltype(ci.method_set) new_method_set;
    for (const MethodInfo &mi : ci.method_set) {
      if (gt_methods_instrumented.count(mi.address) != 0) {
        new_method_set.insert(mi);
      }
    }
    if (new_method_set.size() != 0) {
      ClassInfo instrumented_ci = ci;
      instrumented_ci.method_set = new_method_set;
      gt_class_info_instrumented.push_back(instrumented_ci);
    }
  }

  return gt_class_info_instrumented;
}

// ============================================================================
static std::set<virtual_address_t> LoadAndRecordGtMethodStats(
    const std::string &gt_methods_instrumented_path,
    const std::vector<ClassInfo> &ground_truth) {
  std::ifstream gt_methods_instrumented(gt_methods_instrumented_path);

  std::set<virtual_address_t> gt_methods_instrumented_set;

  int addr{};
  while (gt_methods_instrumented >> addr) {
    gt_methods_instrumented_set.insert(addr + kBaseAddr);
  }

  // Find all constructors/destructors in ground truth
  std::set<MethodInfo> ctor_set = GetTypeSet(ground_truth, "ctor");
  std::set<MethodInfo> dtor_set = GetTypeSet(ground_truth, "dtor");

  size_t ctor_instrumented{0};
  size_t dtor_instrumented{0};

  for (const MethodInfo &mi : ctor_set) {
    if (gt_methods_instrumented_set.count(mi.address) != 0) {
      ctor_instrumented++;
    }
  }

  for (const MethodInfo &mi : dtor_set) {
    if (gt_methods_instrumented_set.count(mi.address) != 0) {
      dtor_instrumented++;
    }
  }

  size_t gt_methods{0};
  for (const ClassInfo &ci : ground_truth) {
    for (const MethodInfo &mi : ci.method_set) {
      gt_methods++;
    }
  }

  std::ofstream gt_method_info(gt_methods_instrumented_path + ".stats");

  float gt_coverage_all_methods =
      static_cast<float>(gt_methods_instrumented_set.size()) /
      static_cast<float>(gt_methods);

  float gt_coverage_ctor = static_cast<float>(ctor_instrumented) /
                           static_cast<float>(ctor_set.size());

  float gt_coverage_dtor = static_cast<float>(dtor_instrumented) /
                           static_cast<float>(dtor_set.size());

  gt_method_info << "all method coverage: " << gt_coverage_all_methods
                 << ", ctor coverage: " << gt_coverage_ctor
                 << ", dtor coverage: " << gt_coverage_dtor;

  return gt_methods_instrumented_set;
}

// ============================================================================
static std::vector<ClassInfo> LoadAndConvertJson(const std::string &json_str) {
  auto json = JsonLoader::LoadData(json_str);

  if (json == nullptr) {
    throw std::runtime_error("failed to parse json");
  }

  return ToClassInfo(json);
}

// ============================================================================
static std::vector<ClassInfo> ToClassInfo(const boost::json::value &json) {
  std::vector<ClassInfo> class_info_list;
  for (const auto &class_it :
       json.as_object().find("structures")->value().as_object()) {
    try {
      ClassInfo class_info;

      // Search through class methods
      const auto &class_methods =
          class_it.value().as_object().find("methods")->value().as_object();

      for (const auto &method_it : class_methods) {
        const auto &method_obj = method_it.value().as_object();
        std::stringstream method_ea_ss;
        std::string type = method_obj.find("type")->value().as_string().c_str();
        method_ea_ss << std::hex
                     << method_obj.find("ea")->value().as_string().c_str();
        virtual_address_t ea{};
        method_ea_ss >> ea;
        class_info.method_set.insert(MethodInfo{.address = ea, .type = type});
      }

      // Search through class members
      const auto &class_members =
          class_it.value().as_object().find("members")->value().as_object();

      for (const auto &member_it : class_members) {
        const auto &member_obj = member_it.value().as_object();
        bool is_member_parent = member_obj.find("parent")->value().as_bool();

        if (is_member_parent) {
          class_info.parent_mangled_names.push_back(
              member_obj.find("struc")->value().as_string().c_str());
        }
      }

      class_info.mangled_name = class_it.key_c_str();

      class_info_list.push_back(class_info);
    } catch (std::invalid_argument e) {
      std::stringstream ss;
      ss << "when trying to create method sets: " << e.what() << " for class "
         << class_it.key() << std::endl;
      throw std::invalid_argument(ss.str());
    }
  }

  return class_info_list;
}

// ============================================================================
static float ComputePrecision(int32_t true_positives, int32_t false_positives) {
  if (false_positives + true_positives == 0) {
    return 0.0f;
  }
  return static_cast<float>(true_positives) /
         static_cast<float>(true_positives + false_positives);
}

// ============================================================================
static float ComputeRecall(int32_t true_positives, int32_t false_negatives) {
  if (true_positives + false_negatives == 0) {
    return 0.0f;
  }
  return static_cast<float>(true_positives) /
         static_cast<float>(true_positives + false_negatives);
}

// ============================================================================
static float ComputeF1(float precision, float recall) {
  if (precision + recall == 0.0f) {
    return 0.0f;
  }
  return (2.0f * precision * recall) / (precision + recall);
}

// ============================================================================
template <typename T>
static int32_t FalseNegatives(const T &gt, int32_t true_positives) {
  return gt.size() - true_positives;
}

// ============================================================================
template <typename T>
static int32_t FalsePositives(const T &gen_data, int32_t true_positives) {
  return gen_data.size() - true_positives;
}

/// @return The classes in the given data set that have a nonempty method set.
static std::vector<const ClassInfo *> NonemptyClasses(
    const std::vector<ClassInfo> &classes) {
  std::vector<const ClassInfo *> nonempty_classes;
  for (const auto &cls : classes) {
    if (!cls.method_set.empty()) {
      nonempty_classes.push_back(&cls);
    }
  }
  return nonempty_classes;
}

// ============================================================================
static std::pair<float, float> PrecisionAndRecallClasses(
    const std::vector<ClassInfo> &ground_truth,
    const std::vector<ClassInfo> &generated_data) {
  // Two classes are equal if the intersection of their method sets is not
  // empty. Exclude classes without methods. Ground truth classes can't be
  // double counted as a match for multiple generated classes.

  std::vector<const ClassInfo *> ground_truth_excluding_empty_cls =
      NonemptyClasses(ground_truth);
  std::vector<const ClassInfo *> generated_data_excluding_empty_cls =
      NonemptyClasses(generated_data);

  std::set<std::pair<const ClassInfo *, const ClassInfo *>> matched_classes =
      MatchGenToGtClasses(ground_truth, generated_data);
  int32_t true_positives = matched_classes.size();

  int32_t false_negatives =
      FalseNegatives(ground_truth_excluding_empty_cls, true_positives);
  int32_t false_positives =
      FalsePositives(generated_data_excluding_empty_cls, true_positives);

  return std::pair(ComputePrecision(true_positives, false_positives),
                   ComputeRecall(true_positives, false_negatives));
}

// ============================================================================
static std::pair<float, float> PrecisionAndRecallMethods(
    const std::vector<ClassInfo> &ground_truth,
    const std::vector<ClassInfo> &generated_data) {
  // Collect all methods in a single set (their address only)
  auto to_method_set = [](const std::vector<ClassInfo> &in) {
    std::set<virtual_address_t> method_set;
    for (const auto &it : in) {
      for (const auto &method : it.method_set) {
        method_set.insert(method.address);
      }
    }
    return method_set;
  };

  std::set<virtual_address_t> ground_truth_methods =
      to_method_set(ground_truth);
  std::set<virtual_address_t> generated_data_methods =
      to_method_set(generated_data);

  int32_t true_positives{};

  for (const auto &method : generated_data_methods) {
    if (ground_truth_methods.count(method)) {
      true_positives++;
    }
  }

  int32_t false_negatives =
      FalseNegatives(ground_truth_methods, true_positives);
  int32_t false_positives =
      FalsePositives(generated_data_methods, true_positives);

  return std::pair(ComputePrecision(true_positives, false_positives),
                   ComputeRecall(true_positives, false_negatives));
}

// ============================================================================
static std::set<MethodInfo> GetTypeSet(const std::vector<ClassInfo> &in,
                                       const std::string_view &type) {
  std::set<MethodInfo> type_set;
  for (const auto &it : in) {
    for (const auto &method : it.method_set) {
      if (method.type == type) {
        type_set.insert(method);
      }
    }
  }
  return type_set;
}

// ============================================================================
static std::pair<float, float> PrecisionAndRecallSpecificType(
    const std::vector<ClassInfo> &ground_truth,
    const std::vector<ClassInfo> &generated_data,
    const std::string_view &type) {
  std::set<MethodInfo> ground_truth_type = GetTypeSet(ground_truth, type);
  std::set<MethodInfo> generated_data_type = GetTypeSet(generated_data, type);

  int32_t true_positives{};

  for (const auto &method : generated_data_type) {
    if (ground_truth_type.count(method)) {
      true_positives++;
    }
  }

  int32_t false_negatives = FalseNegatives(ground_truth_type, true_positives);
  int32_t false_positives = FalsePositives(generated_data_type, true_positives);

  return std::pair(ComputePrecision(true_positives, false_positives),
                   ComputeRecall(true_positives, false_negatives));
}

// ============================================================================
static std::pair<float, float> PrecisionAndRecallConstructors(
    const std::vector<ClassInfo> &ground_truth,
    const std::vector<ClassInfo> &generated_data) {
  return PrecisionAndRecallSpecificType(
      ground_truth, generated_data, kConstructorType);
}

// ============================================================================
static std::pair<float, float> PrecisionAndRecallDestructors(
    const std::vector<ClassInfo> &ground_truth,
    const std::vector<ClassInfo> &generated_data) {
  return PrecisionAndRecallSpecificType(
      ground_truth, generated_data, kDestructorType);
}

// ============================================================================
struct PrecisionRecallF1 {
  float precision{};
  float recall{};
  float f1{};
  const ClassInfo *ground_truth_class;

  friend bool operator<(const PrecisionRecallF1 &o1,
                        const PrecisionRecallF1 &o2) {
    return o1.f1 < o2.f1;
  }
};

static std::pair<float, float> PrecisionAndRecallMethodsAssignedCorrectClass(
    const std::vector<ClassInfo> &ground_truth,
    const std::vector<ClassInfo> &generated_data) {
  struct EvaluationResults {
    float precision;
    float recall;
    size_t ground_truth_class_size;
  };

  std::vector<EvaluationResults> results;

  // For each generated method set
  for (const auto &generated_class : generated_data) {
    // compute precision and recall against every ground truth method set
    std::set<PrecisionRecallF1> precision_recall_f1_scores;

    // Insert 0 precision, recall, and F-score struct into
    // precision_recall_f1_scores in case none of the ground truth sets don't
    // have any methods shared with the generated class.
    precision_recall_f1_scores.insert(PrecisionRecallF1{
        .precision = 0,
        .recall = 0,
        .f1 = 0,
        .ground_truth_class = nullptr,
    });

    for (const auto &ground_truth_class : ground_truth) {
      int32_t true_positives{};

      for (const auto &method : generated_class.method_set) {
        if (ground_truth_class.method_set.count(method)) {
          true_positives++;
        }
      }

      // If no true positives, the precision and recall are 0 or undefined
      if (true_positives == 0) {
        continue;
      }

      int32_t false_negatives =
          FalseNegatives(generated_class.method_set, true_positives);
      int32_t false_positives =
          FalsePositives(ground_truth_class.method_set, true_positives);

      float precision = ComputePrecision(true_positives, false_positives);
      float recall = ComputeRecall(true_positives, false_negatives);
      float f1 = ComputeF1(precision, recall);

      precision_recall_f1_scores.insert(PrecisionRecallF1{
          .precision = ComputePrecision(true_positives, false_positives),
          .recall = ComputeRecall(true_positives, false_negatives),
          .f1 = ComputeF1(precision, recall),
          .ground_truth_class = &ground_truth_class,
      });
    }

    const auto &highest_f1_it = precision_recall_f1_scores.rbegin();
    if (highest_f1_it != precision_recall_f1_scores.rend()) {
      const PrecisionRecallF1 &highest_f1 = *highest_f1_it;

      // If the ground truth class is undefined, the ground truth class size is
      // 0. While this means this won't be taken into account in this metric,
      // other metrics that measure overall class precision/recall are useful.
      results.push_back(EvaluationResults{
          .precision = highest_f1.precision,
          .recall = highest_f1.recall,
          .ground_truth_class_size =
              highest_f1.ground_truth_class == nullptr
                  ? 0
                  : highest_f1.ground_truth_class->method_set.size(),
      });
    } else {
      results.push_back(EvaluationResults{
          .precision = 0,
          .recall = 0,
          .ground_truth_class_size = 0,
      });
    }
  }

  // Consume results
  float precision{};
  float recall{};
  size_t total_methods{};
  for (const auto &result : results) {
    precision += result.precision * result.ground_truth_class_size;
    recall += result.recall * result.ground_truth_class_size;
    total_methods += result.ground_truth_class_size;
  }

  return std::pair(precision / static_cast<float>(total_methods),
                   recall / static_cast<float>(total_methods));
}

// ============================================================================
static std::pair<float, float> PrecisionAndRecallClassGraphAncestors(
    const std::vector<ClassInfo> &ground_truth,
    const std::vector<ClassInfo> &generated_data) {
  std::set<std::pair<const ClassInfo *, const ClassInfo *>> matched_classes =
      MatchGenToGtClasses(ground_truth, generated_data);

  auto get_gen_gt_cls_pair =
      [matched_classes](const std::string &cls_mangled_name) {
        using ClassPair = const std::pair<const ClassInfo *, const ClassInfo *>;
        for (const auto &it : matched_classes) {
          if (it.first->mangled_name == cls_mangled_name) {
            return std::optional<ClassPair>{it};
          }
        }
        return std::optional<ClassPair>{std::nullopt};
      };

  auto get_gen_cls =
      [&](const std::string &cls_mangled_name) -> const ClassInfo * {
    for (const auto &cls : generated_data) {
      if (cls.mangled_name == cls_mangled_name) {
        return &cls;
      }
    }
    std::cerr << "could not find cls in generated data by name " << cls_mangled_name
              << std::endl;
    return nullptr;
  };

  auto get_gt_cls =
      [&](const std::string &cls_mangled_name) -> const ClassInfo * {
    for (const auto &cls : ground_truth) {
      if (cls.mangled_name == cls_mangled_name) {
        return &cls;
      }
    }
    std::cerr << "could not find cls in ground truth " << cls_mangled_name
              << std::endl;
    return nullptr;
  };

  int32_t true_positives{0};
  int32_t gt_size{0};
  int32_t gen_size{0};

  for (const auto &[gen_cls, gt_cls] : matched_classes) {
    if (gen_cls->parent_mangled_names.size() == 0 &&
        gt_cls->parent_mangled_names.size() == 0) {
      // Both gen and ground truth share the "root" i.e. there are no
      // inheritance relationships and that has been correctly identified.
      true_positives++;

      gen_size++;
      gt_size++;
    } else {
      auto find_ancestors =
          [](const ClassInfo *cls,
             std::function<const ClassInfo *(const std::string &)>
                 get_cls) {
            std::set<const ClassInfo *> ancestors;

            // List of ancestors that will be systematically removed from this
            // worklist and whose associated ClassInfo will be added to the set
            // of ancestors.
            std::vector<std::string> worklist = cls->parent_mangled_names;

            // Find all ancestors of gen class
            while (worklist.size() != 0) {
              std::string cls_name = worklist.back();
              worklist.pop_back();

              const ClassInfo *ci = get_cls(cls_name);
              if (ci != nullptr && ancestors.count(ci) == 0) {
                ancestors.insert(ci);
                for (const auto &parent : ci->parent_mangled_names) {
                  worklist.push_back(parent);
                }
              }
            }
            return ancestors;
          };

      auto gen_ancestors = find_ancestors(gen_cls, get_gen_cls);
      // std::set<const ClassInfo *> gen_ancestors;
      // for (const auto &cls_name : gen_cls->parent_mangled_names) {
      //   const ClassInfo *parent_gt_cls = get_gen_cls(cls_name);
      //   gen_ancestors.insert(parent_gt_cls);
      // }

      // Find all parents given the list of parents
      std::set<const ClassInfo *> gt_parents;
      for (const auto &cls_name : gt_cls->parent_mangled_names) {
        const ClassInfo *parent_gt_cls = get_gt_cls(cls_name);
        if (parent_gt_cls != nullptr) {
          gt_parents.insert(parent_gt_cls);
        }
      }

      // Check if any of the ancestors match the gt
      for (const ClassInfo *ancestor : gen_ancestors) {
        // Find the ground truth class associated with the ancestor.
        auto gen_gt_pair = get_gen_gt_cls_pair(ancestor->mangled_name);

        if (gen_gt_pair.has_value() &&
            gt_parents.find(gen_gt_pair->second) != gt_parents.end()) {
          true_positives++;
        }
      }

      gen_size += gen_ancestors.size();
      gt_size += gt_parents.size();
    }
  }

  int32_t false_negatives = gt_size - true_positives;
  int32_t false_positives = gen_size - true_positives;

  return std::pair(ComputePrecision(true_positives, false_positives),
                   ComputeRecall(true_positives, false_negatives));
}

// ============================================================================
static std::pair<float, float> PrecisionAndRecallClassGraphEdges(
    const std::vector<ClassInfo> &ground_truth,
    const std::vector<ClassInfo> &generated_data) {
  std::set<std::pair<const ClassInfo *, const ClassInfo *>> matched_classes =
      MatchGenToGtClasses(ground_truth, generated_data);

  // TODO this is inefficient and can be sped up but for now we aren't worried
  // about evaluation efficiency.
  auto get_gen_gt_cls_pair =
      [matched_classes](const std::string &cls_mangled_name) {
        using ClassPair = const std::pair<const ClassInfo *, const ClassInfo *>;
        for (const auto &it : matched_classes) {
          if (it.first->mangled_name == cls_mangled_name) {
            return std::optional<ClassPair>{it};
          }
        }
        return std::optional<ClassPair>{std::nullopt};
      };

  int32_t true_positives{0};
  int32_t gt_size{0};
  int32_t gen_size{0};

  for (const auto &[gen_cls, gt_cls] : matched_classes) {
    // Count number of parents that the two classes share. The "ground truth"
    // for this measure is the total number of inheritance relationships. A true
    // positive would be when the generated and ground truth class share the
    // same inheritance relationship

    // Note: we don't expect parent names to be the same - instead we expect the
    // paired classes to be the same.

    if (gen_cls->parent_mangled_names.size() == 0 &&
        gt_cls->parent_mangled_names.size() == 0) {
      // Both gen and ground truth share the "root" i.e. there are no
      // inheritance relationships and that has been correctly identified.
      true_positives++;

      gen_size++;
      gt_size++;
    } else {
      for (const std::string &parent_name : gen_cls->parent_mangled_names) {
        auto gt = get_gen_gt_cls_pair(parent_name);
        if (gt.has_value()) {
          if (std::find(gt_cls->parent_mangled_names.begin(),
                        gt_cls->parent_mangled_names.end(),
                        gt->second->mangled_name) !=
              gt_cls->parent_mangled_names.end()) {
            true_positives++;
          }
        } else {
          std::cerr << "failed to find parent by the name of " << parent_name
                    << " for child " << gen_cls->mangled_name
                    << " because no gt class matches this parent" << std::endl;
        }
      }

      gen_size += gen_cls->parent_mangled_names.size();
      gt_size += gt_cls->parent_mangled_names.size();
    }
  }

  int32_t false_negatives = gt_size - true_positives;
  int32_t false_positives = gen_size - true_positives;

  return std::pair(ComputePrecision(true_positives, false_positives),
                   ComputeRecall(true_positives, false_negatives));
}

// ============================================================================
static std::set<std::pair<const ClassInfo *, const ClassInfo *>>
MatchGenToGtClasses(const std::vector<ClassInfo> &ground_truth,
                    const std::vector<ClassInfo> &generated_data) {
  std::vector<const ClassInfo *> ground_truth_excluding_empty_cls =
      NonemptyClasses(ground_truth);
  std::vector<const ClassInfo *> generated_data_excluding_empty_cls =
      NonemptyClasses(generated_data);

  std::set<std::pair<const ClassInfo *, const ClassInfo *>> matched_classes;

  std::set<const ClassInfo *> gt_classes_referenced;

  for (const auto &cls : generated_data_excluding_empty_cls) {
    std::multimap<size_t, const ClassInfo *> gen_gt_intersection_sizes;
    for (const auto &gt_cls : ground_truth_excluding_empty_cls) {
      std::vector<MethodInfo> intersected_method_set;

      std::set_intersection(cls->method_set.begin(),
                            cls->method_set.end(),
                            gt_cls->method_set.begin(),
                            gt_cls->method_set.end(),
                            std::back_inserter(intersected_method_set));

      if (!intersected_method_set.empty()) {
        gen_gt_intersection_sizes.insert(
            std::pair(intersected_method_set.size(), gt_cls));
      }
    }

    // Get largest method set intersection that isn't already in the referenced
    // class set
    bool found = false;
    for (auto it = gen_gt_intersection_sizes.rbegin();
         it != gen_gt_intersection_sizes.rend();
         it++) {
      if (!gt_classes_referenced.count(it->second)) {
        gt_classes_referenced.insert(it->second);
        matched_classes.insert(std::pair(cls, it->second));
        found = true;
        break;
      }
    }
  }

  return matched_classes;
}
