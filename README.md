# minimax
This is Adam's project for Parallel Computing 15-418 with Dr. Kayvon.

Detailed analysis for the project is here: https://onedrive.live.com/view.aspx?resid=32E6A245B0AB92ED!5392&ithint=file%2cpdf&app=WordPdf&authkey=!APosAtniXrQRu9M

You're free to read it and use any snippets you would like, please just don't use it for academic misconduct.

This project is chess with AI that can search a minimax tree in parallel. It contains code for playing a serial and a parallel  AI against each other with fixed time per turn and recording the winner. The code was adapted from Marcel's Simple Chess Program (that program was originally serial). Most of the chess code is in C and the test harness is in python. The parallelization is all done with Cilk.

List of files that I edited:

Makefile - I changed this to compile with Cilk.

analyze.py - This contains many functions for reading output files to see the average depth of search, how much we lost to non-maximum utilization of the cores, average search time, etc.

bulkScript.sh - submits a series of jobs to the latedays cluster

latedays.qsub -  this submits a single mainScript.sh job to the latedays cluster

mainScript.sh - this sets the environment variables, builds our chess program, runs it, and runs the analysis script.

cycleTimer.h - This is an accurate C timer using the processors clock cycles. It works well when doing parallelism in the Cilk library. It is adapted from Kayvon's C++ cycle timer.

hscp.c - this is the program that lets people play chess. It also contains all of the AI.

inputBoth.txt - This is the input for hscp.txt when it runs; it's only two lines.

takeAverages.py - This is unused but it is similar to analyze.py


book.txt - this is the book of opening chess patterns; it is unused here

cscp.c - this is unused; it is a version of the program where both sides are serial AI

mscp.c - this is an older version of hscp.c
