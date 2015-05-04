#!/usr/bin/python
import sys
def compareGames(inFile1, inFile2):
    in1=open(inFile1, "r")
    in2=open(inFile2,"r")
    #result=True
    lines1=in1.readlines()
    lines2=in2.readlines()
#    if (len(lines1)!=len(lines2)):
        
 #       return False
    
    #implicit else
    for i in range(len(lines1)):

        if lines1[i]!=lines2[i]:
            print("Discrepancy at line " + str(i))
            return False
    return True



def analyzeOutput(filename):
    inFile=open(filename, "r");
    lines=inFile.readlines();
    gameIndex=0
    numTurns=0
    mySum=0
    for line in lines:
        if line[0]==("g"):
            print("Average time from file " + filename + " was "+str(mySum/numTurns) + " for game " + str(gameIndex))
            return mySum
            numTurns=0
            mySum=0
            gameIndex+=1
         
        else:
            mySum+=float(line)
            numTurns+=1
    inFile.close();


def analyzeTime(filename):
    inFile=open(filename, "r");
    lines=inFile.readlines();
    mySum=0
    numTurns=0
    for line in lines:
        mySum+=float(line)
        numTurns+=1
    inFile.close();
    print("Average time from file " + filename + " was "+str(float(mySum)/numTurns) )
    return mySum


def analyzeCount(filename):
    inFile=open(filename, "r");
    lines=inFile.readlines();
    mySum=0
    numTurns=0
    for line in lines:
        mySum+=int(line)
        numTurns+=1
    inFile.close();
    print("Average time from file " + filename + " was "+str(float(mySum)/numTurns) )
    return mySum

def main():
    print("Comparing "+sys.argv[1]+ " and " + sys.argv[2] + ":\n")
    print(str(compareGames(sys.argv[1], sys.argv[2])))
    totalTime1=analyzeOutput("time1.dat")
    totalTime2=analyzeOutput("time2.dat")
    firstTime1=analyzeTime("time_first1.dat")
    firstTime2=analyzeTime("time_first2.dat")
    secondTime1=analyzeTime("time_second1.dat")
    secondTime2=analyzeTime("time_second2.dat")
    totalCount1=analyzeCount("count1.dat")
    totalCount2=analyzeCount("count2.dat")
    firstCount1=analyzeCount("count_first1.dat")
    firstCount2=analyzeCount("count_first2.dat")
    secondCount1=analyzeCount("count_second1.dat")
    secondCount2=analyzeCount("count_second2.dat")


    print("Overall speedup was " + str(totalTime1/totalTime2))
    print("Overall efficiency speedup was " + str((totalCount2/totalTime2)/(totalCount1/totalTime1)))
    print("First Branch efficiency speedup was " + str((firstCount2/firstTime2)/(firstCount1/firstTime1)))
    print("Second Branch efficiency speedup was " + str((secondCount2/secondTime2)/(secondCount1/secondTime1)))
    print("Fraction in first branch for serial was " + str(firstTime1/secondTime1))
    print("Fraction in first branch for parallel was " + str(firstTime2/secondTime2))
    
    
    #print("Overall efficiency 1 was " +str(totalCount1/totalTime1))
    
    print("First efficiency 1 was " +str(firstCount1/firstTime1))
    print("Second efficiency 1 was " +str(secondCount1/secondTime1))
    #print("Overall efficiency 2 was " +str(totalCount2/totalTime2))
    print("First efficiency 2 was " +str(firstCount2/firstTime2))
    print("Second efficiency 2 was " +str(secondCount2/secondTime2))


if __name__=="__main__":
    main()
