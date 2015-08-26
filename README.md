# minimax
This is Adam's project for Parallel Computing 15-418 with Dr. Kayvon.

You're free to read it and use any snippets you would like, please just don't use it for academic misconduct.

This project is chess with AI that can search a minimax tree in parallel. It contains code for playing a serial and a parallel  AI against each other with fixed time per turn and recording the winner. The code was adapted from Marcel's Simple Chess Program (that program was originally serial). Most of the chess code is in C and the test harness is in python. The parallelization is all done with Cilk.

List of files that I edited:

Makefile - I changed this to compile with Cilk.
analyze.py - This contains many functions for reading output files to see the average depth of search, how much we lost to non-maximum utilization of the cores, average search time, etc.





List of files unchanged from Marcel's Simple Chess Program:
