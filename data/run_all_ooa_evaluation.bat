@REM Runs evaluation on OOAnalyzer generated results (located in evaluation/results_json_ooa)

python evaluation\independent_evaluation.py -gt-results-json data\kreo-libbmp\gt-results.json -results-json evaluation\results_json_ooa\ooa-libbmp-results.json -results-path evaluation\results\in\libbmp-ooa
python evaluation\independent_evaluation.py -gt-results-json data\kreo-optparse\gt-results.json -results-json evaluation\results_json_ooa\ooa-optparse-results.json -results-path evaluation\results\in\optparse-ooa
python evaluation\independent_evaluation.py -gt-results-json data\kreo-ser4cpp\gt-results.json -results-json evaluation\results_json_ooa\ooa-ser4cpp-results.json -results-path evaluation\results\in\ser4cpp-ooa
python evaluation\independent_evaluation.py -gt-results-json data\kreo-tinyxml2\gt-results.json -results-json evaluation\results_json_ooa\ooa-tinyxml2-results.json -results-path evaluation\results\in\tinyxml2-ooa
