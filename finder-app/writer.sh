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

# Add string to file
mkdir -p "${WRITEFILE%/*}" && touch $WRITEFILE
echo "${WRITESTR}" > $WRITEFILE