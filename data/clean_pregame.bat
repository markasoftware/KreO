@ECHO OFF

for %%t in (lego lego+ kreo) do (
    for %%p in (libbmp optparse ser4cpp tinyxml2) do (
        rm -rf %%t-%%p/base-address
        rm -rf %%t-%%p/method-candidates
        rm -rf %%t-%%p/static-traces
    )
)
