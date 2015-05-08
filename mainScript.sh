#!/bin/bash
source /afs/cs/academic/class/15210-f14/cilk/gccvars_bash.sh

latepath=/afs/andrew.cmu.edu/usr21/akavka/private/minimax/
echo Arguments were $1 $2 $3 $4 $5
if [ $# -ge 2 ]
  then
    export CILK_NWORKERS=$2
    echo "Setting to $2 workers."
else
    export CILK_NWORKERS=4
    echo "Defaulting to four workers"
fi


gcc -fcilkplus -lcilkrts  -o  "$latepath""$5"mscp "$latepath"hscp.c
#make
#"$latepath""$5"mscp $1 0 $3 $4 $latepath $5 < "$latepath"inputBoth.txt > "$latepath""$5"out1.txt

echo "Did first run."

"$latepath""$5"mscp $1 1 $3 $4 $latepath $5 < "$latepath"inputBoth.txt > "$latepath""$5"out2.txt

python "$latepath"analyze.py "$latepath""$5"out1.txt "$latepath""$5"out2.txt $2 $latepath $5