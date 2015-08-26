#!/usr/bin/python
import sys
"""
Adam Kavka
30 April 2015

This file contains many functions for doing analysis of your games.
It can count time per move, search depth per move, see who won, etc.

"""


#This function looks at the input file and says who won the game.
def getResult(inFile):
    wins =0;
    draws=0;
    losses=0;
    instream=open(inFile,"r")
    lines=instream.readlines()
    for i in range (len(lines)):
        if lines[i].startswith("game over: 1-0"):
            print ("Result loss")
            
        if lines[i].startswith("game over: 1/2-1/2"):
            print ("Result draw")
        if lines[i].startswith("game over: 0-1"):
            print("Result win")


#This function is used in debugging. It looks at the files for two different games and makes sure they are identical.
def compareGames(inFile1, inFile2):
    in1=open(inFile1, "r")
    in2=open(inFile2,"r")

    lines1=in1.readlines()
    lines2=in2.readlines()

    
    #implicit else
    for i in range(len(lines1)):

        if lines1[i]!=lines2[i]:
            print("Discrepancy at line " + str(i))
            return False
    return True


#This function tells you the average time per turn in the input file's game.
#This takes an input file with a different format than 'analyzeTime'; the files here terminate with a "g" character
def analyzeOutput(filename):
    inFile=open(filename, "r");
    lines=inFile.readlines();
    gameIndex=0
    numTurns=0
    mySum=0
    for line in lines:
        
        #Check if this line was the end of the game
        if line[0]==("g"):
            print("Average time from file " + filename + " was "+str(mySum/numTurns) + " for game " + str(gameIndex))
            return mySum
            numTurns=0
            mySum=0
            gameIndex+=1
         
         #Add this turn to our average
        else:
            mySum+=float(line)
            numTurns+=1
    inFile.close();

#This function tells you the average time per turn in the input file's game.
#This takes an input file with a different format than 'analyzeTime'; every line in the input file here is the timing for a turn
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

#This method looks at an output file to count the total number of minimax nodes visited
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


#This method tells you what fraction of the time cores are being used.
# For example if a search lasts 6 seconds, and one core is actually doing work for 3 seconds,
# and another core is actully doing work for 1 second,
# This will print a utilization of '3'
def analyzeDivergence(filename, numCoresString):
    numCores=float(numCoresString)
    inFile=open(filename, "r");
    lines=inFile.readlines();
 #   sums=[]
 
 #This keeps track of how much work we did each turn
    partialUsefulSum=0
    
    #This is the total work we did across all turns
    usefulSum=0
    
    #This is the total time of the game
    totalSum=0
    
    for line in lines:
        words=line.split()
        
    #start a new turn, print results of previous turn    
        if words[0]=="fence":
            utilization=partialUsefulSum/(numCores*float(words[1]))
   
            print("Utilization was " + str(utilization))
            totalSum+=numCores*float(words[1])
            partialUsefulSum=0
            
            #add to the tally for this turn
        else:
            partialUsefulSum+=float(words[1])
            usefulSum+=float(words[1])
    print("Total utilization was " + str(usefulSum/totalSum))


#This method reades an input file to get the the average depth of search.
def analyzeDepth(filename, side):
    inFile=open(filename, "r");
    lines=inFile.readlines();
    mySum=0
    numTurns=0
    for line in lines:
        if int(line)<20:
            mySum+=int(line)
        numTurns+=1
    if numTurns==0:
        numTurns=1
    print("Average depth "+ side+" was " + str(float(mySum)/float(numTurns)))



def main():
    
    #These arguments get us to our path on latedays, the cluster we ran this on
    latepath=sys.argv[4]+sys.argv[5]
    
    analyzeDepth(latepath+"depth_white.dat", "W")
    analyzeDepth(latepath+"depth_black.dat", "B")    
    getResult(latepath+"out2.txt")

#everything below here is old analysis. I left it in incase you wat to uncomment something useful
############################################################################
    """
    print("Comparing "+sys.argv[1]+ " and " + sys.argv[2] + ":\n")
    print(str(compareGames(sys.argv[1], sys.argv[2])))


    totalTime1=analyzeOutput(latepath+"time1.dat")
    totalTime2=analyzeOutput(latepath+"time2.dat")

    
    firstTime1=analyzeTime(latepath+"time_first1.dat")
    firstTime2=analyzeTime(latepath+"time_first2.dat")
    secondTime1=analyzeTime(latepath+"time_second1.dat")
    secondTime2=analyzeTime(latepath+"time_second2.dat")
    totalCount1=analyzeCount(latepath+"count1.dat")
    totalCount2=analyzeCount(latepath+"count2.dat")
    firstCount1=analyzeCount(latepath+"count_first1.dat")
    firstCount2=analyzeCount(latepath+"count_first2.dat")
    secondCount1=analyzeCount(latepath+"count_second1.dat")
    secondCount2=analyzeCount(latepath+"count_second2.dat")
    """
    
#    analyzeDivergence(latepath+"divergence2.dat", sys.argv[3])
    """
    print("Overall speedup was " + str(totalTime1/totalTime2))
    print("Overall efficiency speedup was " + str((totalCount2/totalTime2)/(totalCount1/totalTime1)))
    print("Work ratio was " + str(float(totalCount1)/totalCount2))
    """
    """
    print("First Branch efficiency speedup was " + str((firstCount2/firstTime2)/(firstCount1/firstTime1)))
    print("Second Branch efficiency speedup was " + str((secondCount2/secondTime2)/(secondCount1/secondTime1)))
    print("Fraction in first branch for serial was " + str(firstTime1/secondTime1))
    print("Fraction in first branch for parallel was " + str(firstTime2/secondTime2))
    """
    
    #print("Overall efficiency 1 was " +str(totalCount1/totalTime1))
    """
    print("First efficiency 1 was " +str(firstCount1/firstTime1))
    print("Second efficiency 1 was " +str(secondCount1/secondTime1))
    #print("Overall efficiency 2 was " +str(totalCount2/totalTime2))
    print("First efficiency 2 was " +str(firstCount2/firstTime2))
    print("Second efficiency 2 was " +str(secondCount2/secondTime2))
    """

if __name__=="__main__":
    main()
