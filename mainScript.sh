#!/bin/bash
source /afs/cs/academic/class/15210-f14/cilk/gccvars_bash.sh

if [ $# -ge 2 ]
  then
    export CILK_NWORKERS=$2
    echo "Setting to $2 workers."
else
    export CILK_NWORKERS=4
    echo "Defaulting to four workers"
fi


gcc -fcilkplus -lcilkrts  -o  mscp mscp.c
#make
./mscp $1  < inputBoth.txt > out1.txt

echo "Did first run."

./mscp $1 1  < inputBoth.txt > out2.txt

python analyze.py out1.txt out2.txt