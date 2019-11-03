#! /bin/bash

valgrind --help >& /dev/null
if [ $? -ne 0 ]; then
  echo "WARNING!"
  echo "valgrind not found!"
  echo "Skipping valgrind test of testlib"
  exit 0
else
  echo dslfj
  if file .libs/lt-testlib | grep ELF > /dev/null; then
    valgrind --error-exitcode=200 --num-callers=150 --leak-check=full --show-reachable=yes .libs/lt-testlib
  elif file ./testlib | grep ELF > /dev/null; then
    valgrind --error-exitcode=200 --num-callers=150 --leak-check=full --show-reachable=yes ./testlib
  elif file ./.libs/testlib | grep ELF > /dev/null; then
    valgrind --error-exitcode=200 --num-callers=150 --leak-check=full --show-reachable=yes ./.libs/testlib
  else
    echo "Unknown install/build stage."
    exit 1
  fi
fi
