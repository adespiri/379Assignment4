all: a4tasks

clean:
	rm -rf a4tasks submit.tar a4tasks

tar:
	tar -czf submit.tar a4tasks.cpp Makefile A4AssignmentReport.pdf
 
a4tasks: a4tasks.cpp
	g++ a4tasks.cpp -o a4tasks -lpthread


