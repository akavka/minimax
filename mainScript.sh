#!/bin/bash

make
./mscp $1 < inputBoth.txt > out1.txt

echo "Did first run."

./mscp $1 1 < inputBoth.txt > out2.txt

python analyze.py out1.txt out2.txt