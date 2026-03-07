#!/bin/sh
filesdir="$1"
searchstr="$2"

if [ -z "$1" ] || [ -z "$2" ]; then
    echo "Error: Both arguments are required."
    exit 1
fi

if [ ! -d "$filesdir" ]; then
    echo "Error: The specified directory does not exist."
    exit 1
fi

file_count=$(find "$filesdir" -type f | wc -l)
line_count=$(grep -r "$searchstr" "$filesdir" | wc -l)
echo "The number of files are $file_count and the number of matching lines are $line_count"