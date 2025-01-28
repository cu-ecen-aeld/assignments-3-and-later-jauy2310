#!/bin/sh
# Assignment 1 Finder App script

# Pre-checks to validate command structure
if [ $# -ne 2 ]
then
    echo "Error; please specify exactly 2 arguments."
    exit 1
else
    FILESDIR=$1
    SEARCHSTR=$2
fi

# Check if directory is valid
if [ ! -d ${FILESDIR} ]
then
    echo "Error; please ensure ${FILESDIR} is a valid path."
    exit 1
fi

# Save the number of files into local variable
FILECOUNT="$(find ${FILESDIR} -type f | wc -l)"

# Save the number of matches into local variable
MATCHCOUNT="$(grep -rnw ${FILESDIR} -e ${SEARCHSTR} | wc -l)"

echo "The number of files are ${FILECOUNT} and the number of matching lines are ${MATCHCOUNT}"