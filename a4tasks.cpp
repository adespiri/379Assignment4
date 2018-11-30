#include <stdio.h>
#include<iostream>
#include<sys/resource.h>
#include <stdlib.h>
#include <unistd.h>
#include<vector>
#include<string>
#include<boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <pthread.h> 
#include <fstream>
#include <errno.h>
#include <time.h> 
#include <map> 
#include <sys/times.h>

#define NTASKS 25
#define NRES_TYPES 10
//https://stackoverflow.com/questions/17264984/undefined-reference-to-pthread-create correct compile command for pthreads
//https://www.cs.cmu.edu/afs/cs/academic/class/15492-f07/www/pthreads.html general pthreads information
using namespace std;

typedef enum {WAIT, RUN, IDLE} STATUS; //thread status

typedef struct {
	char name[100];
	int busyTime;
	int idleTime;
	long totalBusyTime;
	long totalIdleTime;
	long totalWaitTime;
	vector<string> reqResources;
	bool assigned;
	int timesExecuted;
	STATUS status;
} TASK; //contains the details of a particular task (name, busy time, idle time, required resources)

//GLOBAL VARIABLES
map<string, int> resourceMap; //contain the resources from the file
vector<TASK> taskList; //contains all the tasks from the file
pthread_mutex_t threadMutex;
pthread_mutex_t iterationMutex;
pthread_mutex_t monitorMutex; // used for monitor to prevent states from switching
pthread_t TID[NTASKS];
int ITERATIONS;
clock_t START, END;
struct tms tmsstart, tmsend;
static long clktck = 0;

void mutex_init(pthread_mutex_t* mutex)
{	//from lab exercise on eclass
	int rval = pthread_mutex_init(mutex, NULL);
	if (rval) { fprintf(stderr, "mutex_init: %s\n", strerror(rval)); exit(1); }
}

void mutex_lock(pthread_mutex_t* mutex)
{	//from lab exercise on eclass
	int rval = pthread_mutex_lock(mutex);
	if (rval) { fprintf(stderr, "mutex_lock: %s\n", strerror(rval)); exit(1); }
}

void mutex_unlock(pthread_mutex_t* mutex)
{	//from lab exercise on eclass
	int rval = pthread_mutex_unlock(mutex);
	if (rval) { fprintf(stderr, "mutex_unlock: %s\n", strerror(rval)); exit(1); }
}

void addResources(char* nameCountPair)
{
	//split the name:count pair and add to the map
	char tempPair[40];
	strcpy(tempPair, nameCountPair);
	int tempCount; 
	string tempName(strtok(tempPair, ":"));
	tempCount = atoi(strtok(NULL, ":"));
	//add to map https://www.geeksforgeeks.org/map-associative-containers-the-c-standard-template-library-stl/
	//resourceMap.insert(pair<char*, int>(tempName, tempCount));
	resourceMap[tempName] = tempCount;
}

void defineResources(char* resourceLine)
{
	//we have one line and need to tokenize each resource and map it.
	char* temp;
	char line[100];
	char* name;
	int resourceCount;
	vector<char*> resourceStrings;

	strcpy(line, resourceLine);
	temp = strtok(line, " "); //temp is keyword RESOURCES
	temp = strtok(NULL, " "); //move it to first name:count pair
	while (temp != NULL)
	{
		resourceStrings.push_back(temp);
		temp = strtok(NULL, " ");
	}

	for (int i = 0; i < resourceStrings.size(); i++)
	{	
		//add the resources to the map
		addResources(resourceStrings.at(i));
	}
}

void readTaskFile(char* fileName)
{	
	string line; //line read from file

	ifstream file(fileName);
	if (file.fail()) {
		printf("FILE DOES NOT EXIST\n"); exit(1);
	} //check if file exists

	if (file.good())
	{
		while (getline(file, line))
		{
			//ignore any comments or white lines 
			if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') {
				continue;
			}
			
			char cline[100];
			char* temp;
			strcpy(cline, line.c_str());

			//determine what the leading keyword is
			temp = strtok(cline, " "); //temp is now keyword (task or resource)
			if (!(strcmp(temp, "resources") == 0 || strcmp(temp, "task") == 0)) { printf("UNKNOWN KEYWORD FROM FILE\n"); exit(1); }

			if (strcmp(temp, "resources") == 0)
			{	
				//we need to define the resources that will be used
				strcpy(cline, line.c_str());
				defineResources(cline);
			}

			else
			{	//if it is not a resource, it is a task. initialize new task and add it to task list
				TASK newTask;
				temp = strtok(NULL, " ");
				strcpy(newTask.name, temp);
				temp = strtok(NULL, " ");
				newTask.busyTime = atoi(temp);
				temp = strtok(NULL, " ");
				newTask.idleTime = atoi(temp);
				temp = strtok(NULL, " ");
				newTask.assigned = false;
				newTask.status = IDLE;
				newTask.totalIdleTime = 0;
				newTask.totalBusyTime = 0;
				newTask.totalWaitTime = 0;
				newTask.timesExecuted = 0;
				//add resource strings to list
				while (temp != NULL)
				{
					string str(temp);
					newTask.reqResources.push_back(str);
					temp = strtok(NULL, " ");
				}

				//add to task list
				taskList.push_back(newTask);
			}
			
		}
	}
}

void delay(int milliseconds) //from lab experiments on eClass
{
	struct timespec interval;
	interval.tv_sec = (long) milliseconds / 1000;
	interval.tv_nsec = (long) ((milliseconds % 1000) * 1000000);
	if (nanosleep(&interval, NULL) < 0)
		printf("warning: delay: %s\n", strerror(errno));
}

bool checkResources(TASK* task)
{	//this function is called by a thread when it is going to check available resources
	//We need to check if resources are available to grab, if not then unlock and go back to waiting
	for (int i = 0; i < task->reqResources.size(); i++)
	{
		char resource[50];
		strcpy(resource, task->reqResources.at(i).c_str());
		char* resName = strtok(resource, ":");
		int resCount = atoi(strtok(NULL, ":"));

		if (resourceMap[resName] >= resCount) //if there are enough resources for this thread
		{
			continue;
		}

		else { return false; }
	}
	//if the process reaches this point that means there are enough of the required resources
	return true;
}

void procureResources(TASK* task)
{	//this function will decrement the appropriate resources when called
	for (int i = 0; i < task->reqResources.size(); i++)
	{
		char resource[50];
		strcpy(resource, task->reqResources.at(i).c_str());
		char* resName = strtok(resource, ":");
		int resCount = atoi(strtok(NULL, ":"));
		int currentValue = resourceMap[resName];
		int newValue = currentValue - resCount;
		resourceMap[resName] = newValue;
	}
	return;
}

void returnResources(TASK* task)
{	//this function will return the appropriate resources when called
	for (int i = 0; i < task->reqResources.size(); i++)
	{
		char resource[50];
		strcpy(resource, task->reqResources.at(i).c_str());
		char* resName = strtok(resource, ":");
		int resCount = atoi(strtok(NULL, ":"));
		int currentValue = resourceMap[resName];
		int newValue = currentValue + resCount;
		resourceMap[resName] = newValue;
	}
	return;
}

float getTime()
{	// gets time since start of main program execution

	END = times(&tmsend);

	clock_t time = END - START;
	return time/(double) clktck * 1000;
}

void runIterations(TASK* task)
{ /*After a thread is created, it will come to this function to execute its
  main loop. We use another mutex to handle race conditions with other threads*/
	int iterationCounter = 0;
	bool enoughResources;
	clock_t waitStart, waitFinish; //used to determine how long a task will wait
	struct tms tmswaitstart, tmswaitend;

	mutex_lock(&monitorMutex); //make sure cannot change state if monitor is printing
	task->status = WAIT;
	mutex_unlock(&monitorMutex);

	waitStart = times(&tmswaitstart);
	while (1)
	{
		mutex_lock(&iterationMutex);

		//We need to check if resources are available to grab, if not then unlock and go back to waiting
		enoughResources = checkResources(task);
		if (!enoughResources)
		{	//release mutex and go back to waiting
			mutex_unlock(&iterationMutex);
			delay(20);
			continue;
		}

		procureResources(task); //will actually grab the resources from the shared resource pool
		waitFinish = times(&tmswaitend);
		task->totalWaitTime += (waitFinish - waitStart) / (double)clktck * 1000; //update wait time
		mutex_unlock(&iterationMutex);

		//after resources are taken, simulate the execution of the process
		mutex_lock(&monitorMutex); //cant switch states if monitor is printing
		task->status = RUN;
		mutex_unlock(&monitorMutex);
		delay(task->busyTime);
		task->totalBusyTime += task->busyTime;
		
		//after running the busytime then return the resources back to the pool
		mutex_lock(&iterationMutex);
		returnResources(task);
		mutex_unlock(&iterationMutex);
		
		//now we wait for idle time and increment iteration counter
		mutex_lock(&monitorMutex); // cant switch states if monitor printing
		task->status = IDLE;
		mutex_unlock(&monitorMutex);

		delay(task->idleTime);
		task->totalIdleTime += task->idleTime;
		iterationCounter += 1;
		task->timesExecuted += 1;
		//print out iteration info
		printf("Task: %s (tid= %lu, iter= %d, time= %.0fms) \n", task->name, pthread_self(), iterationCounter, getTime());
		if (iterationCounter == ITERATIONS) { return; }

		mutex_lock(&monitorMutex); //cant switch states if monitor printing
		task->status = WAIT;
		mutex_unlock(&monitorMutex);
		waitStart = times(&tmswaitstart);
	}

}

void *threadExecute(void *arg)
{	/* This is the starting routine when a new thread is created*/
	//first iterate through the task list and assign an unassigned task to this thread
	TASK task;
	TID[(long)arg] = pthread_self();
	for (int i = 0; i < taskList.size(); i++)
	{
		task = taskList.at(i);
		if (task.assigned) {continue; }
		else
		{
			taskList.at(i).assigned = true; //use this task for the main loop
			mutex_unlock(&threadMutex); //release mutex for next thread and jump to main loop
			runIterations(&taskList.at(i));
			break;
		}
	}
	pthread_exit(NULL);
}

void printMonitor()
{	/*Prints the tasks and their current status*/
	string waitString = "";
	string runString = "";
	string idleString = ""; //3 different strings for the three different states that will be printed

	for (int i = 0; i < taskList.size(); i++)
	{
		if (taskList.at(i).status == WAIT) { waitString = waitString + taskList.at(i).name + " "; }
		else if(taskList.at(i).status == RUN) { runString = runString + taskList.at(i).name + " "; }
		else { idleString = idleString + taskList.at(i).name + " "; }

	}
	//print to the terminal
	printf("\n\nMonitor: [WAIT] %s\n\t [RUN] %s\n\t [IDLE] %s\n\n", waitString.c_str(), 
		runString.c_str(), idleString.c_str());
}

void *monitorThread(void *arg)
{	/*monitor thread that prints out details periodically*/
	long monitorTime = (long)arg;
	while (1) //go indefinitely until main program quits
	{
		delay(monitorTime);
		mutex_lock(&monitorMutex);
		printMonitor();
		mutex_unlock(&monitorMutex); //monitor mutex ensures the tasks will not change states while printing
	}
}

void printTerminationInfo()
{
	//iterate through the map and print all resource types and their available counts
	map<string, int>::iterator itr;
	printf("\n\nSystem Resources:\n");
	for (itr = resourceMap.begin(); itr != resourceMap.end(); itr++)
	{
		printf("\t\t%s: ", (itr->first).c_str());
		printf("(maxAvail=\t%d, held=\t0)\n", resourceMap[itr->first]);
	}

	//print out the task information
	printf("\n\nSystem Tasks:\n");
	for (int i = 0; i < taskList.size(); i++)
	{	
		char status[20];
		if (taskList.at(i).status == IDLE) { strcpy(status, "IDLE"); }
		else if (taskList.at(i).status == WAIT) { strcpy(status, "WAIT"); }
		else { strcpy(status, "RUN"); }
		printf("[%d] %s (%s, runTime= %lu msec, idleTime= %lu msec):\n", i, taskList.at(i).name, status,
			taskList.at(i).totalBusyTime, taskList.at(i).totalIdleTime);
		printf("\t (tid= %lu\n", TID[i]);
		//print the required resources
		for (int j = 0; j < taskList.at(i).reqResources.size(); j++)
		{	
			char* resourceName;
			int resourcesNeeded;
			char resourceString[50];
			strcpy(resourceString, taskList.at(i).reqResources.at(j).c_str());
			resourceName = strtok(resourceString, ":");
			resourcesNeeded = atoi(strtok(NULL, ":"));

			printf("\t %s: (needed=\t%d, held= 0)\n", resourceName, resourcesNeeded );
		}
		printf("\t (RUN: %d times, WAIT: %lu msec\n\n", taskList.at(i).timesExecuted, taskList.at(i).totalWaitTime);
	}

	//print the total running time of the program
	printf("Total Running Time: %.0f msec\n\n", getTime());
}

int main(int argc, char* argv[]) 
{	
	long monitorTime;
	int rval;
	char fileName[20];
	pthread_t ntid;

	mutex_init(&threadMutex);
	mutex_init(&iterationMutex);
	mutex_init(&monitorMutex);

	if (clktck == 0) //get number of clock cycles per second. Will be used for timing functions
	{
		if ((clktck = sysconf(_SC_CLK_TCK)) < 0)
		{
			printf("systemconf error"); return -1;
		}
	}

	START = times(&tmsstart);
	//first parse the command line input
	if (argc != 4) { printf("invalid number of arguments\n"); exit(1); }

	strcpy(fileName, argv[1]);
	monitorTime = atoi(argv[2]);
	ITERATIONS = atoi(argv[3]);

	if (!(monitorTime >= 0) || !(monitorTime < 99999999)) //arbitrarily large number
	{
		printf("MonitorTime NOT VALID\n"); exit(1);
	}

	if (!(ITERATIONS >= 0) || !(ITERATIONS < 99999999)) //arbitrarily large number
	{
		printf("ITERATIONS NOT VALID\n"); exit(1);
	}

	//We need to read the file line by line and map the resources to the resources map and the tasks to the task list
	readTaskFile(fileName);
	
	//create monitor thread
	rval = pthread_create(&ntid, NULL, monitorThread, (void*) monitorTime);
	if (rval) {
		fprintf(stderr, "pthread_create: %s\n", strerror(rval));
		exit(1);
	}

	//for every task in the task list we need to execute a new thread
	for (long i = 0; i < taskList.size(); i++)
	{
		mutex_lock(&threadMutex);
		rval = pthread_create(&ntid, NULL, threadExecute, (void *) i);
		if (rval) {
			fprintf(stderr, "pthread_create: %s\n", strerror(rval));
			exit(1);
		}
	}
	delay(400);
	//wait for all other threads to complete before continuing
	for (long i = 0; i < taskList.size(); i++)
	{
		rval = pthread_join(TID[i], NULL);
		if (rval) {
			fprintf(stderr, "\n** pthread_join: %s\n", strerror(rval));
			exit(1);
		}
	}
	
	//handle the termination output
	printf("\n\nThreads Finished\nMain Program Terminating...\n");
	printTerminationInfo();
	return 0;
}