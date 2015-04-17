#!/usr/bin/python
import sys
def compareGames(inFile1, inFile2):
    in1=open(inFile1, "r")
    in2=open(inFile2,"r")
    #result=True
    lines1=in1.readlines()
    lines2=in2.readlines()
    if (len(lines1)!=len(lines2)):
        return False
    
    #implicit else
    for i in range(len(lines1)):

        if lines1[i]!=lines2[i]:
            return False
    return True


def main():
    print("Comparing "+sys.argv[1]+ " and " + sys.argv[2] + ":\n")
    print(str(compareGames(sys.argv[1], sys.argv[2])))

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

if __name__=="__main__":
    main()
