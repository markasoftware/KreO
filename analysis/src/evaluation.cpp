#include <boost/json.hpp>
#include <iostream>
#include <set>

#include "json_loader.h"

using virtual_address_t = uint64_t;

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

using MethodSet = std::set<MethodInfo>;

/// @brief Converts json to vector of method sets whose set elements are method
/// addresses that have been associated with some particular class.
static std::vector<MethodSet> ToMethodSets(const boost::json::value &json);

static std::vector<MethodSet> LoadAndConvertJson(const std::string &json_str);

/// @brief Computes precision and recall on the generated methods. The ground
/// truth for evaluation is all methods in the ground truth. A true positive is
/// a correctly identified method. A false positive is a method that is not in
/// the ground truth. A false negative is a method that was not identified.
/// @return pair, first element precision second element recall. These elements
/// are guaranteed to be between [0, 1].
static std::pair<float, float> PrecisionAndRecallMethods(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated);

static std::pair<float, float> PrecisionAndRecallClasses(
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

        std::cout << "name\t" << precision_recall.first << "\t"
                  << precision_recall.second << std::endl;
      };

  run_test("methods", &PrecisionAndRecallMethods);
  run_test("classes", &PrecisionAndRecallClasses);
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

static float ComputePrecision(int32_t true_positives, int32_t false_positives) {
  return static_cast<float>(true_positives) /
         static_cast<float>(true_positives + false_positives);
}

static float ComputeRecall(int32_t true_positives, int32_t false_negatives) {
  return static_cast<float>(true_positives) /
         static_cast<float>(true_positives + false_negatives);
}

static std::pair<float, float> PrecisionAndRecallClasses(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data) {
  // Creates a set of all detected classes. We identify classes by associating
  // constructor methods with the class. Two classes are equal the intersection
  // of their constructor sets is not empty.
  auto ToClassSet = [](const std::vector<MethodSet> &in) {
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
      ToClassSet(ground_truth);
  std::set<std::set<virtual_address_t>> generated_data_class_set =
      ToClassSet(generated_data);

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

static std::pair<float, float> PrecisionAndRecallMethods(
    const std::vector<MethodSet> &ground_truth,
    const std::vector<MethodSet> &generated_data) {
  auto ToMethodSet = [](const std::vector<MethodSet> &in) {
    std::set<virtual_address_t> method_set;
    for (const auto &it : in) {
      for (const auto &method : it) {
        method_set.insert(method.address);
      }
    }
    return method_set;
  };

  std::set<virtual_address_t> ground_truth_methods = ToMethodSet(ground_truth);
  std::set<virtual_address_t> generated_data_methods =
      ToMethodSet(generated_data);

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
