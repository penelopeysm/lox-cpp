#!/usr/bin/env bash

# This script runs end-to-end tests for the project.
DIR=$(dirname "$0")
printf "\e[1;34mRunning end-to-end tests...\e[0m\n"

# Read all *.out files into a Bash array
FILES=($(find "${DIR}/e2e" -name "*.out"))
i=1
nfiles=${#FILES[@]}
nfails=0

# Look for all *.out files in the e2e directory
for output_file in ${FILES[@]}; do
    # Get the lox file.
    loxfile="${output_file%.out}.lox"
    # Run the lox interpreter with the lox file and capture the output
    actual_output=$("$DIR/../loxc" "$loxfile")
    # Check exit code.
    if [ $? -ne 0 ]; then
        printf "\e[31m ($i/$nfiles) Test failed (interpreter error):\e[0m %s\n" "$loxfile"
        i=$((i + 1))
        nfails=$((nfails + 1))
        continue
    fi
    # Read the expected output from the .out file
    expected_output=$(<"$output_file")
    # Compare the actual output with the expected output
    if [ "$actual_output" == "$expected_output" ]; then
        printf "\e[32m ($i/$nfiles) Test passed:\e[0m %s\n" "$loxfile"
    else
        printf "\e[31m ($i/$nfiles) Test failed:\e[0m %s\n" "$loxfile"
        printf "\e[33m--------------------\e[0m\n"
        printf "\e[33mExpected output:\e[0m\n%s\n" "$expected_output"
        printf "\e[33mActual output:\e[0m\n%s\n" "$actual_output"
        printf "\e[33m--------------------\e[0m\n"
        nfails=$((nfails + 1))
    fi
    i=$((i + 1))
done

if [ $nfails -eq 0 ]; then
    printf "\e[1;32mAll %d tests passed!\e[0m\n" "$nfiles"
    exit 0
else
    printf "\e[1;31m%d out of %d tests failed.\e[0m\n" "$nfails" "$nfiles"
    exit 1
fi
