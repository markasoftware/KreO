@REM Runs evaluation on OOAnalyzer generated results (located in evaluation/results_json_ooa)

for %%p in (libbmp optparse ser4cpp tinyxml2) do (
    python evaluation\independent_evaluation.py -gt-results-json data\kreo-%%p\gt-results.json -results-json evaluation\results_json_ooa\ooa-%%p-results.json -results-path evaluation\results\in\%%p-ooa
    python evaluation\independent_evaluation.py -gt-results-json data\kreo-%%p\gt-results.json -results-json evaluation\results_json_ooa\ooa-%%p-results-no-rtti.json -results-path evaluation\results\in\%%p-ooa_no_rtti
)
