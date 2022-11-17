# !/usr/bin/env bash

# Performs evaluation

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

USAGE='usage: ./pregame.sh <path/to/binary>'

if [ "$#" != "1" ]; then
    echo $USAGE
    exit 1
fi

cd $SCRIPT_DIR/pregame
make BINARY=$1
cd -
