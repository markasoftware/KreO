set -e

BASEDIR=$(dirname "$0")
cd ${BASEDIR}

for tool in lego lego+ kreo
do
    for project in libbmp optparse ser4cpp tinyxml2
    do
        rm -rf ${tool}-${project}/base-address
        rm -rf ${tool}-${project}/method-candidates
        rm -rf ${tool}-${project}/static-traces
    done
done
