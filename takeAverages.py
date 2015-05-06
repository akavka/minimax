#!/usr/bin/python
import sys
#latepath="/home/akavka/minimax/"



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

def analyzeDivergence(filename, numCoresString):
    numCores=float(numCoresString)
    inFile=open(filename, "r");
    lines=inFile.readlines();
 #   sums=[]
 
    partialUsefulSum=0
    usefulSum=0
    totalSum=0
    for line in lines:
        words=line.split()
        if words[0]=="fence":
            utilization=partialUsefulSum/(numCores*float(words[1]))
    #        utilization=sum(sums)/(4*float(words[1]))
            print("Utilization was " + str(utilization))
            totalSum+=numCores*float(words[1])
            partialUsefulSum=0
   #         sums=[0,0,0,0]
        else:
  #          sums[int(words[0])]+=float(words[1])
            partialUsefulSum+=float(words[1])
            usefulSum+=float(words[1])
    print("Total utilization was " + str(usefulSum/totalSum))


def processAFile(index,cores, utilization, speedup, workEfficiency, searchOverhead, loss):
    filename="latedays.qsub.o"+str(index)
    infile=open(filename, "r")
    lines= infile.readlines()
    words=lines[0].split()
    core=int(words[3])
    lossTerm=0
    if (not (core in cores)):
        cores.append(core)
        utilization[core]=[]
        speedup[core]=[]
        workEfficiency[core]=[]
        searchOverhead[core]=[]
        loss[core]=[]
    for line in lines:
        words=line.split()
        if len(words)<2:
            continue
        if (words[0]=="Total" and words[1]=="utilization"):
        #if utilization
            utilization[core].append(float(words[3]))
            lossTerm=float(words[3])
        #if efficiency
        if (words[0]=="Work" and words[1]=="ratio"):
            searchOverhead[core].append(float(words[3]))
            lossTerm*=float(words[3])
            loss[core].append(lossTerm)
        if (words[0]=="Overall" and words[1]=="efficiency"):
            workEfficiency[core].append(float(words[4]))

        if (words[0]=="Overall" and words[1] =="speedup"):
            speedup[core].append(float(words[3]))
        
        
        #if speedup


        


def main():
    lowerRange=int(sys.argv[1])
    upperRange=int(sys.argv[2])
    
    cores=[]
    utilization={}
    speedup={}
    workEfficiency={}
    searchOverhead={}
    loss={}
    
    for i in range(lowerRange,upperRange+1):
        processAFile(i,cores,utilization, speedup, workEfficiency, searchOverhead, loss)
    
    print("Cores:\n")
    for core in cores:
        print(str(core))
    
    print ("Speedup:\n")
    for core in cores:
        meanSpeed=str(sum(speedup[core])/len(speedup[core]))
        meanUtil=str(sum(utilization[core])/len(speedup[core]))
        print(meanSpeed+"\t"+ meanUtil)



if __name__=="__main__":
    main()