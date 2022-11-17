# !/usr/bin/env bash

# Performs preanalysis for a project given its generated PDB file

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

USAGE='usage: ./preanalysis.sh <path/to/pdb/dump/file> <path/to/output/json>'

if [ "$#" != "2" ]; then
    echo $USAGE
    exit 1
fi

PDB_FILE=$1
JSON_FILE=$2

# Make sure build is up to date
cd $SCRIPT_DIR/../build
make
cd -

# Analyze PDB file
$SCRIPT_DIR/../build/analyze_pdb_dump $PDB_FILE > $JSON_FILE

# Extract ground truth methods from the generated json
$SCRIPT_DIR/../build/extract_gt_methods $JSON_FILE > "$SCRIPT_DIR/../../out/gt-methods"
