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
# find - finds files in the FILESDIR that are files (ignores directories); innately recursive
# pipe to wc -l to count the number of lines (i.e., matches)
# executes command in quotes and saves output to FILECOUNT
FILECOUNT="$(find ${FILESDIR} -type f | wc -l)"

# Save the number of matches into local variable
# grep - searches for SEARCHSTR recursively in FILESDIR for the whole search term
# pipe to wc -l to count the number of lines (i.e., matches)
# executes command in quotes and saves output to MATCHCOUNT
MATCHCOUNT="$(grep -rw ${FILESDIR} -e ${SEARCHSTR} | wc -l)"

# echo the results to the terminal
echo "The number of files are ${FILECOUNT} and the number of matching lines are ${MATCHCOUNT}"