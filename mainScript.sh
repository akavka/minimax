#!/bin/bash
source /afs/cs/academic/class/15210-f14/cilk/gccvars_bash.sh

latepath=/home/akavka/minimax/
echo Arguments were $1 $2 $3 $4
if [ $# -ge 2 ]
  then
    export CILK_NWORKERS=$2
    echo "Setting to $2 workers."
else
    export CILK_NWORKERS=4
    echo "Defaulting to four workers"
fi


#gcc -fcilkplus -lcilkrts  -o  "$latepath"mscp "$latepath"mscp.c
#make
"$latepath"mscp $1 0 $3 $4 $latepath < "$latepath"inputBoth.txt > "$latepath"out1.txt

echo "Did first run."

"$latepath"mscp $1 1 $3 $4 $latepath < "$latepath"inputBoth.txt > "$latepath"out2.txt

python "$latepath"analyze.py "$latepath"out1.txt "$latepath"out2.txt $2 $latepath