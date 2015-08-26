#!/bin/bash
#This is the primary script for the project.
# It sets the environment variables, builds the c program, runs the program
# and runs the analysis scripts.

#We need some of the class' variables to get Cilk to work.
source /afs/cs/academic/class/15210-f14/cilk/gccvars_bash.sh

#path to our directory on this machine
latepath=/home/akavka/minimax/
echo Arguments were $1 $2 $3 $4 $5

#set the number of cilk workers
if [ $# -ge 2 ]
  then
    export CILK_NWORKERS=$2
    echo "Setting to $2 workers."
else
    export CILK_NWORKERS=4
    echo "Defaulting to four workers"
fi

#build our c program
gcc -fcilkplus -lcilkrts  -o  "$latepath""$5"mscp "$latepath"hscp.c

echo "Did first run."

# run a game of the chess program
"$latepath""$5"mscp $1 1 $3 $4 $latepath $5 < "$latepath"inputBoth.txt > "$latepath""$5"out2.txt

#run our analysis script
python "$latepath"analyze.py "$latepath""$5"out1.txt "$latepath""$5"out2.txt $2 $latepath $5
