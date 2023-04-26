'''
Perform evaluation without config file (allows you to evaluate OOAnalyzer).
'''

import evaluation
import argparse

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-gt-results-json', required=True)
    parser.add_argument('-results-json', required=True)
    parser.add_argument('-results-path', required=True)
    args = parser.parse_args()

    evaluation.run_evaluation(args.gt_results_json, args.results_json,
                              args.results_path, None, None)

if __name__ == '__main__':
    main()
