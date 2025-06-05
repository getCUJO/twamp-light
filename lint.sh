#!/bin/bash

# List all tracked files using git ls-files
files=$(git ls-files)

# Loop through each file and run clang-tidy if it's a C/C++ source/header file
for file in $files; do
    case "$file" in
    include/system/*) continue ;; # Skip system headers
    *.c | *.cpp | *.cc | *.cxx | *.h | *.hpp | *.hh | *.hxx)
        echo "Running clang-tidy on $file"
        clang-tidy -p /secure-dev/twamp-light/build/ "$file" -- || true
        ;;
    esac
done
