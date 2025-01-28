#!/bin/sh
# Assignment 1 writer script

# Pre-check for valid command arguments
if [ $# -ne 2 ]
then
    echo "Error; please specify exactly 2 arguments."
    exit 1
else
    WRITEFILE=$1
    WRITESTR=$2
fi

# create necessary parent directories using -p flag in mkdir, trimming out the filename
mkdir -p "${WRITEFILE%/*}" && touch $WRITEFILE

# redirect output of echo from stdout (terminal) to the file path specified
echo "${WRITESTR}" > $WRITEFILE