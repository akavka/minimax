#!/bin/bash
source /afs/cs/academic/class/15210-f14/cilk/gccvars_bash.sh

gcc -fcilkplus -lcilkrts -o mscp mscp.c
#make
./mscp $1  < inputBoth.txt > out1.txt

echo "Did first run."

./mscp $1 1  < inputBoth.txt > out2.txt 2>error.txt

python analyze.py out1.txt out2.txt