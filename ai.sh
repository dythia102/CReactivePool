#!/bin/bash

# Usage: ./script.sh [directory1] [directory2] ... [file1] [file2] ...

BASE_DIR="$PWD"

for arg in "$@"; do
    if [ -d "$arg" ]; then
        find "$arg" -type f | while read -r file; do
            rel_path=$(realpath --relative-to="$BASE_DIR" "$file")
            echo "===== $rel_path ====="
            nl -ba "$file"
            echo -e "\n"
        done
    elif [ -f "$arg" ]; then
        rel_path=$(realpath --relative-to="$BASE_DIR" "$arg")
        echo "===== $rel_path ====="
        nl -ba "$arg"
        echo -e "\n"
    else
        echo "Warning: '$arg' is not a valid file or directory." >&2
    fi
done
