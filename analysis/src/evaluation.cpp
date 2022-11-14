#include <boost/json.hpp>
#include <iostream>
#include <set>

#include "json_loader.h"

using virtual_address_t = uint64_t;

static constexpr std::string_view kConstructorType{"ctor"};
static constexpr std::string_view kDestructorType{"dtor"};

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
using MethodSet = std::set<MethodInfo>;

// ============================================================================
/// @brief Converts json to vector of method sets whose set elements are method
/// addresses that have been associated with some particular class.
static std::vector<MethodSet> ToMethodSets(const boost::json::value &json);

// ============================================================================
static std::vector<MethodSet> LoadAndConvertJson(const std::string &json_str);

// ============================================================================
/// @brief Computes precision and recall on the generated methods. The ground
/// truth for evaluation is all methods in the ground truth. A true positive is
/// a correctly identified method. A false positive is a method that is not in
/// the ground truth. A false negative is a method that was not identified.
/// @return pair, first element precision second element recall. These elements
/// are guaranteed to be between [0, 1].
static std::pair<float, float> PrecisionAndRecallMethods(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated);

// ============================================================================
static std::pair<float, float> PrecisionAndRecallClasses(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data);

// ============================================================================
static std::pair<float, float> PrecisionAndRecallConstructors(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data);

// ============================================================================
static std::pair<float, float> PrecisionAndRecallDestructors(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data);

// ============================================================================
static std::pair<float, float> PrecisionAndRecallIndividualClasses(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data);

// ============================================================================
int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: ./evaluation <path-to-ground-truth-json> "
                 "<path-to-generated-json>"
              << std::endl;
    return EXIT_FAILURE;
  }

  auto gt_method_sets = LoadAndConvertJson(argv[1]);
  auto gen_method_sets = LoadAndConvertJson(argv[2]);

  auto run_test =
      [=](const std::string &name,
          std::function<std::pair<float, float>(const std::vector<MethodSet> &,
                                                const std::vector<MethodSet> &)>
              test) {
        auto precision_recall = test(gt_method_sets, gen_method_sets);

        std::cout << name << '\t' << precision_recall.first << "\t"
                  << precision_recall.second << std::endl;
      };

  std::cout << "evaluation criteria\tprecision\trecall" << std::endl;
  run_test("methods", &PrecisionAndRecallMethods);
  run_test("classes", &PrecisionAndRecallClasses);
  run_test("constructors", &PrecisionAndRecallConstructors);
  run_test("destructors", &PrecisionAndRecallDestructors);
  run_test("individual_classes", &PrecisionAndRecallIndividualClasses);
}

// ============================================================================
static std::vector<MethodSet> LoadAndConvertJson(const std::string &json_str) {
  auto json = JsonLoader::LoadData(json_str);

  if (json == nullptr) {
    throw std::runtime_error("failed to parse json");
  }

  return ToMethodSets(json);
}

// ============================================================================
static std::vector<MethodSet> ToMethodSets(const boost::json::value &json) {
  std::vector<MethodSet> method_sets;
  for (const auto &class_it :
       json.as_object().find("structures")->value().as_object()) {
    try {
      const auto &class_methods =
          class_it.value().as_object().find("methods")->value().as_object();

      MethodSet method_set;
      for (const auto &method_it : class_methods) {
        const auto &method_obj = method_it.value().as_object();
        std::stringstream method_ea_ss;
        std::string type = method_obj.find("type")->value().as_string().c_str();
        method_ea_ss << std::hex
                     << method_obj.find("ea")->value().as_string().c_str();
        virtual_address_t ea{};
        method_ea_ss >> ea;
        method_set.insert(MethodInfo{.address = ea, .type = type});
      }
      method_sets.push_back(method_set);
    } catch (std::invalid_argument e) {
      std::stringstream ss;
      ss << "when trying to create method sets: " << e.what() << " for class "
         << class_it.key() << std::endl;
      throw std::invalid_argument(ss.str());
    }
  }
  return method_sets;
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

// ============================================================================
static std::pair<float, float> PrecisionAndRecallClasses(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data) {
  // Two classes are equal if the intersection of their method sets is not
  // empty. Exclude classes without methods. Ground truth classes can't be
  // double counted as a match for multiple generated classes.

  auto remove_empty_cls = [](const std::vector<MethodSet> &classes) {
    std::vector<MethodSet> classesNonempty;
    for (const auto &cls : classes) {
      if (!cls.empty()) {
        classesNonempty.push_back(cls);
      }
    }
    return classesNonempty;
  };

  std::vector<MethodSet> ground_truth_excluding_empty_cls =
      remove_empty_cls(ground_truth);
  std::vector<MethodSet> generated_data_excluding_empty_cls =
      remove_empty_cls(generated_data);

  int32_t true_positives{};

  std::set<MethodSet> gt_classes_referenced;

  for (const auto &cls : generated_data_excluding_empty_cls) {
    for (const auto &gt_cls : ground_truth_excluding_empty_cls) {
      std::vector<MethodInfo> intersected_method_set;

      std::set_intersection(cls.begin(),
                            cls.end(),
                            gt_cls.begin(),
                            gt_cls.end(),
                            std::back_inserter(intersected_method_set));

      if (!intersected_method_set.empty() &&
          gt_classes_referenced.find(gt_cls) == gt_classes_referenced.end()) {
        gt_classes_referenced.insert(gt_cls);

        true_positives++;
        break;
      }
    }
  }

  int32_t false_negatives =
      FalseNegatives(ground_truth_excluding_empty_cls, true_positives);
  int32_t false_positives =
      FalsePositives(generated_data_excluding_empty_cls, true_positives);

  return std::pair(ComputePrecision(true_positives, false_positives),
                   ComputeRecall(true_positives, false_negatives));
}

// ============================================================================
static std::pair<float, float> PrecisionAndRecallMethods(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data) {
  // Collect all methods in a single set (their address only)
  auto to_method_set = [](const std::vector<MethodSet> &in) {
    std::set<virtual_address_t> method_set;
    for (const auto &it : in) {
      for (const auto &method : it) {
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
static std::pair<float, float> PrecisionAndRecallSpecificType(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data,
    const std::string_view &type) {
  auto to_type_set = [type](const std::vector<MethodSet> &in) {
    std::set<MethodInfo> constructors;
    for (const auto &it : in) {
      for (const auto &method : it) {
        if (method.type == type) {
          constructors.insert(method);
        }
      }
    }
    return constructors;
  };

  std::set<MethodInfo> ground_truth_constructors = to_type_set(ground_truth);
  std::set<MethodInfo> generated_data_constructors =
      to_type_set(generated_data);

  int32_t true_positives{};

  for (const auto &method : generated_data_constructors) {
    if (ground_truth_constructors.count(method)) {
      true_positives++;
    }
  }

  int32_t false_negatives =
      FalseNegatives(ground_truth_constructors, true_positives);
  int32_t false_positives =
      FalsePositives(generated_data_constructors, true_positives);

  return std::pair(ComputePrecision(true_positives, false_positives),
                   ComputeRecall(true_positives, false_negatives));
}

// ============================================================================
static std::pair<float, float> PrecisionAndRecallConstructors(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data) {
  return PrecisionAndRecallSpecificType(
      ground_truth, generated_data, kConstructorType);
}

// ============================================================================
static std::pair<float, float> PrecisionAndRecallDestructors(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data) {
  return PrecisionAndRecallSpecificType(
      ground_truth, generated_data, kDestructorType);
}

// ============================================================================
struct PrecisionRecallF1 {
  float precision{};
  float recall{};
  float f1{};
  const MethodSet *ground_truth_class;

  friend bool operator<(const PrecisionRecallF1 &o1,
                        const PrecisionRecallF1 &o2) {
    return o1.f1 < o2.f1;
  }
};

static std::pair<float, float> PrecisionAndRecallIndividualClasses(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data) {
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

      for (const auto &method : generated_class) {
        if (ground_truth_class.count(method)) {
          true_positives++;
        }
      }

      // If no true positives, the precision and recall are 0 or undefined
      if (true_positives == 0) {
        continue;
      }

      int32_t false_negatives = FalseNegatives(generated_class, true_positives);
      int32_t false_positives =
          FalsePositives(ground_truth_class, true_positives);

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
                  : highest_f1.ground_truth_class->size(),
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
