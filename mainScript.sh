#!/bin/bash

make
./mscp < inputBoth.txt

python analyze.py