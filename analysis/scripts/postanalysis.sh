# !/usr/bin/env bash

# Performs evaluation

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

USAGE='usage: ./preanalysis.sh <path/to/gt/json> <path/to/gen/json>'

if [ "$#" != "2" ]; then
    echo $USAGE
    exit 1
fi

cd $SCRIPT_DIR/../build
make
cd -

$SCRIPT_DIR/../build/evaluation $1 $2 2>/dev/null
