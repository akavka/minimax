#!/usr/bin/python


inFile=open("time.dat", "r");
lines=inFile.readlines();
gameIndex=0
numTurns=0
mySum=0
for line in lines:
    if line[0]==("g"):
        print("Average time was "+str(mySum/numTurns) + " for game " + str(gameIndex))
        numTurns=0
        mySum=0
        gameIndex+=1
    else:
        mySum+=float(line)
        numTurns+=1
inFile.close();
