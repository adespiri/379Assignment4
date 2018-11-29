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
map<char*, int> resourceMap; //contain the resources from the file
vector<TASK> taskList; //contains all the tasks from the file
pthread_mutex_t threadMutex;
pthread_mutex_t iterationMutex;
pthread_t TID[NTASKS];
int ITERATIONS;

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
	char* tempName;
	int tempCount; 
	tempName = strtok(tempPair, ":");
	tempCount = atoi(strtok(NULL, ":"));
	//add to map https://www.geeksforgeeks.org/map-associative-containers-the-c-standard-template-library-stl/
	resourceMap.insert(pair<char*, int>(tempName, tempCount));

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
				newTask.status = WAIT;
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

void runIterations(TASK* task)
{ /*After a thread is created, it will come to this function to execute its
  main loop. We use another mutex to handle race conditions with other threads*/
	int iterationCounter;
	task->status = WAIT;
	while (1)
	{
		mutex_lock(&iterationMutex);

		//We need to check if resources are available to grab, if not then unlock and go back to waiting
		for (int i = 0; i < task->reqResources.size(); i++)
		{
			char resource[50];
			strcpy(resource, task->reqResources.at(i).c_str());
			char* resName = strtok(resource, ":");
			int resCount = atoi(strtok(NULL, ":"));
			cout << resName << endl;
			cout << resCount << endl;
		}
		mutex_unlock(&iterationMutex);
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

	//first parse the command line input
	if (argc != 4) { printf("invalid number of arguments\n"); exit(1); }
	//TODO more error checking
	
	
	strcpy(fileName, argv[1]);
	monitorTime = atoi(argv[2]);
	ITERATIONS = atoi(argv[3]);

	//We need to read the file line by line and map the resources to the resources map and the tasks to the task list
	readTaskFile(fileName);
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