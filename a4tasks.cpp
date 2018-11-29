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
#include <poll.h> 
#include <fstream>
#include <poll.h>
#include <signal.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h> 
#include <map>

using namespace std;

typedef struct {
	char name[100];
	int busyTime;
	int idleTime;
	vector<string> reqResources;
} TASK; //contains the details of a particular task (name, busy time, idle time, required resources)

map<char*, int> resourceMap; //contain the resources from the file
vector<TASK> taskList; //contains all the tasks from the file

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

	//map<char*, int>::iterator itr; FOR DEBUGGING
	//cout << "\nThe map resourceMap is : \n";
	//cout << "\tKEY\tELEMENT\n";
	//for (itr = resourceMap.begin(); itr != resourceMap.end(); ++itr) {
	//	cout << '\t' << itr->first
	//		<< '\t' << itr->second << '\n';
	//}
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

int main(int argc, char* argv[]) 
{	
	int numIterations;
	int monitorTime;
	char fileName[20];
	//first parse the command line input
	if (argc != 4) { printf("invalid number of arguments\n"); exit(1); }
	//TODO more error checking
	
	strcpy(fileName, argv[1]);
	monitorTime = atoi(argv[2]);
	numIterations = atoi(argv[3]);

	//We need to read the file line by line and map the resources to the resources map and the tasks to the task list
	readTaskFile(fileName);
	


	return 0;
}