#include <stdio.h>
#include<iostream>
#include<sys/resource.h>
#include<sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include<vector>
#include<string>
#include<boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h> 
#include <fstream>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <time.h> 
#include <map>
#include <sys/times.h>

#define NTASKS 25
#define NRES_TYPES 10
//https://stackoverflow.com/questions/17264984/undefined-reference-to-pthread-create
using namespace std;

typedef enum {WAIT, RUN, IDLE} STATUS; //thread status

typedef struct {
	char name[100];
	int busyTime;
	int idleTime;
	vector<string> reqResources;
	bool assigned;
	STATUS status;
} TASK; //contains the details of a particular task (name, busy time, idle time, required resources)

//GLOBAL VARIABLES
map<string, int> resourceMap; //contain the resources from the file
vector<TASK> taskList; //contains all the tasks from the file
pthread_mutex_t threadMutex;
pthread_mutex_t iterationMutex;
pthread_t TID[NTASKS];
int ITERATIONS;
clock_t START, END;
struct tms tmsstart, tmsend;

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
	static long clktck = 0;
	if (clktck == 0)
	{
		if ((clktck = sysconf(_SC_CLK_TCK)) < 0)
		{
			printf("systemconf error"); return -1;
		}
	}
	clock_t time = END - START;
	return time/(double) clktck * 1000;
}

void runIterations(TASK* task)
{ /*After a thread is created, it will come to this function to execute its
  main loop. We use another mutex to handle race conditions with other threads*/
	int iterationCounter = 0;
	bool enoughResources;
	task->status = WAIT;
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
		mutex_unlock(&iterationMutex);
		//after resources are taken, simulate the execution of the process
		task->status = RUN;
		delay(task->busyTime);
		//after running the busytime then return the resources back to the pool
		mutex_lock(&iterationMutex);
		returnResources(task);
		mutex_unlock(&iterationMutex);
		//now we wait for idle time and increment iteration counter
		task->status = IDLE;
		delay(task->idleTime);
		iterationCounter += 1;
		//print out iteration info
		printf("Task: %s (tid= %lu, iter= %d, time= %.0fms) \n", task->name, pthread_self(), iterationCounter, getTime());
		if (iterationCounter == ITERATIONS) { return; }
		task->status = WAIT;
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


int main(int argc, char* argv[]) 
{	
	int monitorTime;
	int rval;
	char fileName[20];
	pthread_t ntid;

	mutex_init(&threadMutex);
	mutex_init(&iterationMutex);

	START = times(&tmsstart);
	//first parse the command line input
	if (argc != 4) { printf("invalid number of arguments\n"); exit(1); }
	//TODO more error checking
	
	
	strcpy(fileName, argv[1]);
	monitorTime = atoi(argv[2]);
	ITERATIONS = atoi(argv[3]);

	//We need to read the file line by line and map the resources to the resources map and the tasks to the task list
	readTaskFile(fileName);

	//map<string, int>::iterator it; FOR DEBUGGING

	//for (it = resourceMap.begin(); it != resourceMap.end(); it++)
	//{
	//	cout << it->first  // string (key)
	//		<< ':'
	//		<< it->second   // string's value 
	//		<< std::endl;
	//}

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


	return 0;
}