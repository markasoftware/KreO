#include <boost/json.hpp>
#include <iostream>
#include <set>

#include "json_loader.h"

using virtual_address_t = uint64_t;

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
  return (2.0f * precision * recall) / (precision + recall);
}

// ============================================================================
static std::pair<float, float> PrecisionAndRecallClasses(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data) {
  // Creates a set of all detected classes. We identify classes by associating
  // constructor methods with the class. Two classes are equal the intersection
  // of their constructor sets is not empty.
  auto to_class_set = [](const std::vector<MethodSet> &in) {
    std::set<std::set<virtual_address_t>> class_set;
    for (const auto &c : in) {
      std::set<virtual_address_t> constructors;
      for (const auto &method : c) {
        if (method.type == "ctor") {
          constructors.insert(method.address);
        }
      }
      class_set.insert(constructors);
    }
    return class_set;
  };

  std::set<std::set<virtual_address_t>> ground_truth_class_set =
      to_class_set(ground_truth);
  std::set<std::set<virtual_address_t>> generated_data_class_set =
      to_class_set(generated_data);

  int32_t true_positives{};

  for (const auto &c : generated_data_class_set) {
    if (std::find_if(ground_truth_class_set.begin(),
                     ground_truth_class_set.end(),
                     [c](const std::set<virtual_address_t> &constructors) {
                       for (virtual_address_t va : constructors) {
                         if (c.count(va)) {
                           return true;
                         }
                       }
                       return false;
                     }) != ground_truth_class_set.end()) {
      true_positives++;
    }
  }

  int32_t false_negatives = ground_truth_class_set.size() - true_positives;
  int32_t false_positives = generated_data_class_set.size() - true_positives;

  return std::pair(ComputePrecision(true_positives, false_positives),
                   ComputeRecall(true_positives, false_negatives));
}

// ============================================================================
static std::pair<float, float> PrecisionAndRecallMethods(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data) {
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

  int32_t false_negatives = ground_truth_methods.size() - true_positives;
  int32_t false_positives = generated_data_methods.size() - true_positives;

  return std::pair(ComputePrecision(true_positives, false_positives),
                   ComputeRecall(true_positives, false_negatives));
}

// ============================================================================
static std::pair<float, float> PrecisionAndRecallSpecificType(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data, const std::string &type) {
  auto to_constructor_set = [type](const std::vector<MethodSet> &in) {
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

  std::set<MethodInfo> ground_truth_constructors =
      to_constructor_set(ground_truth);
  std::set<MethodInfo> generated_data_constructors =
      to_constructor_set(generated_data);

  int32_t true_positives{};

  for (const auto &method : generated_data_constructors) {
    if (ground_truth_constructors.count(method)) {
      true_positives++;
    }
  }

  int32_t false_negatives = ground_truth_constructors.size() - true_positives;
  int32_t false_positives = generated_data_constructors.size() - true_positives;

  return std::pair(ComputePrecision(true_positives, false_positives),
                   ComputeRecall(true_positives, false_negatives));
}

// ============================================================================
static std::pair<float, float> PrecisionAndRecallConstructors(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data) {
  return PrecisionAndRecallSpecificType(ground_truth, generated_data, "ctor");
}

// ============================================================================
static std::pair<float, float> PrecisionAndRecallDestructors(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data) {
  return PrecisionAndRecallSpecificType(ground_truth, generated_data, "dtor");
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

    for (const auto &ground_truth_class : ground_truth) {
      int32_t true_positives{};

      for (const auto &method : generated_class) {
        if (ground_truth_class.count(method)) {
          true_positives++;
        }
      }

      int32_t false_positives = generated_class.size() - true_positives;
      int32_t false_negatives = ground_truth_class.size() - true_positives;

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

    const PrecisionRecallF1 &highest_f1 = *precision_recall_f1_scores.begin();

    results.push_back(EvaluationResults{
        .precision = highest_f1.precision,
        .recall = highest_f1.recall,
        .ground_truth_class_size = highest_f1.ground_truth_class->size(),
    });
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
