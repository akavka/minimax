#
#	Makefile for mscp
#

CC=gcc
CFLAGS=-Wall -O2 -ansi -pedantic

TARGETS=mscp

mscp:	mscp.c
	gcc -fcilkplus -lcilkrts -o mscp mscp.c
#	$(CC) $(CFLAGS) -o mscp mscp.c

clean:
	rm -f ${TARGETS}

