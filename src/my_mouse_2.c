/*
This source file needs library -lrt linked in for the time functions.
*/
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>
#include <errno.h>

#include "my_mouse_2.h"

static int FindMouseHandler(int * p_event_num);
static int OpenMouseHandler(int event_num);
static int CheckKeycodeBit(char * bitArray, int keyCode);

static int mouse_fd;		//this is the file descriptor for the mouse evdev
static char mouse_state;	//these are flags. bit 0 = current mouse button up/down state, bit 1 = mouse down event occurred. assume mouse is up. 

/*
This function sets up the mouse file descriptor, mouse_fd.
*/
int in_InitMouseInput(void)
{
	char key_b[(KEY_MAX/8)+1];
	int r;
	int mouse_event_num;
	int fd_flags;
	
	r = FindMouseHandler(&mouse_event_num);
	if(r == 0) //had an error
	{
		return 0;
	}
	
	mouse_fd = OpenMouseHandler(mouse_event_num);
	if(mouse_fd == -1)
	{
		return 0;
	}
	
	//set the file-descriptor to be non-blocking
	fd_flags = fcntl(mouse_fd, F_GETFL, 0);
	if(fd_flags == -1)
	{
		printf("in_InitMouseInput: error. could not get event%d flags.\n", mouse_event_num);
		close(mouse_fd);
		return 0;
	}
	r = fcntl(mouse_fd, F_SETFL, fd_flags | O_NONBLOCK);
	if(r == -1)
	{
		printf("in_InitMouseInput: error. could not set file-descriptor to non-blocking.\n");
		close(mouse_fd);
		return 0;
	}

	//Get the current mouse button state:
	ioctl(mouse_fd, EVIOCGKEY(sizeof(key_b)), key_b);
	mouse_state = (char)CheckKeycodeBit(key_b, BTN_LEFT);
	
	//everything is setup return success.
	return 1;
}

int in_CloseMouseInput(void)
{
	close(mouse_fd);
}

/*
This is a local function that finds the eventN node that is the mouse in /dev/input
by parsing /proc/bus/input/devices.
*/
static int FindMouseHandler(int * p_event_num)
{
	FILE * pFile;
	char c;
	char line[255];
	int i = 0;
	char * p_event_string;
	int event_num = -1;
	int line_num=1;
	
	pFile = fopen("/proc/bus/input/devices","r");
	if(pFile == 0)
	{
		printf("FindMouseHandler: error. could not open /proc/bus/input/devices.\n");
		return 0;
	}
	
	//loop through the file looking for lines starting with H:
	while(1)
	{
		c = (char)fgetc(pFile);
		
		//stop if we found end of file.
		if(feof(pFile) != 0)
			break;
			
		if(i == 255)
		{
			printf("FindMouseHandler: error line to long at %d.\n", line_num);
			break;
		}
		
		line[i] = c;
		
		//if a full line has been fetched see what we got
		//we only care about lines that start with H:
		if(c == '\n')
		{
			if(line[0] == 'H')
			{
				//null terminate the line
				line[i] = '\0';
			
				//check to see if the mouse is in it
				if(strstr(line, "mouse") != 0)
				{
					//print the line
					printf("%s\n", line);
					
					//look for the event keyword
					p_event_string = strstr(line, "event");
					if(p_event_string != 0)
					{
						p_event_string += 5; //move at the end past event
						sscanf(p_event_string, "%d", &event_num);
						printf("found mouse at event%d\n", event_num);
						*p_event_num = event_num;
						fclose(pFile);
						return 1;
					}
					else
					{
						printf("FindMouseHandler: error could not find 'event'.\n");
					}
				}
			}
			line_num += 1;
			i = -1;//silliness. but i will get incremented after this. need i to be 0 for a new line.
		}
		
		i++;

	}
	fclose(pFile);
	printf("FindMouseHandler: error could not find mouse0 in /dev/input\n");
	return 0;
}

static int OpenMouseHandler(int event_num)
{
	int fd;
	int r;
	char path_name[255];
	char num_string[10];
	
	//setup the pathname
	sprintf(num_string, "%d", event_num);
	path_name[0] = '\0';
	strcat(path_name, "/dev/input/event");
	strcat(path_name, num_string);
	printf("opening %s...\n", path_name);
	
	fd = open(path_name, O_RDONLY);
	if(fd == -1)
	{
		printf("OpenMouseHandler: error. could not open %s\n", path_name);
		return -1;
	}
	
	return fd;
}

/*
This function reads the event handler and returns the sum of any relative mouse events received.
-Assumes that in_InitMouseInput() has already been called.
-only collects mouse input for 1ms

arguments:
	mouse_rel - array of two integers
	pmouseState - flags. bit 0=up/down, bit 1=was there a mouse down event during this call.
*/
int in_MouseRelPos(int * mouse_rel, char * pmouseState)
{
	struct timespec start_time;
	struct timespec curr_time;
	struct input_event m_input;
	int was_down_event=0;
	int is_elapsed = 0;
	long elapsed_nsec;
	int r;
	
	//get the start time
	r = clock_gettime(CLOCK_MONOTONIC, &start_time);
	if(r == -1)
	{
		printf("in_MouseRelPos: error clock_gettime() failed.\n");
		return 0;
	}

	//Reset the flag marking a mouse-down event happening
	*pmouseState = 0;
	
	//read events until time is up
	while(1)
	{
		r = read(mouse_fd, &m_input, sizeof(m_input));
		if(r == -1 && errno != EAGAIN)
		{
			printf("in_MouseRelPos: error read() errno=%d\n", errno);
			return 0;
		}
		switch(m_input.type)
		{
		case EV_REL:
			if(m_input.code == REL_X)
				mouse_rel[0] += m_input.value;
			if(m_input.code == REL_Y)
				mouse_rel[1] += m_input.value;
			break;
		case EV_KEY:
			if(m_input.code == BTN_LEFT)
			{
				if(m_input.value == 1)	//down event.
				{
					mouse_state = 1;
					was_down_event = 1;
				}
				if(m_input.value == 0)	//up event.
				{
					mouse_state = 0;
				}
			}
			break;
		}
		
		//if we have spent all the time, read until the next event 
		//as marked by a EV_SYN.
		if(is_elapsed == 1 && m_input.type == EV_SYN)
			break;
		
		//check the time
		r = clock_gettime(CLOCK_MONOTONIC, &curr_time);
		if(r == -1)
		{
			printf("in_MouseRelPos: error clock_gettime() failed\n");
			return 0;
		}
		
		//check for rollover. calculate elapsed time
		if(curr_time.tv_sec > (start_time.tv_sec+1)) //if more than a second has elapsed don't even bother
		{
			is_elapsed = 1;
			continue;
		}
		else if(curr_time.tv_sec != start_time.tv_sec) //is there rollover?
		{
			elapsed_nsec = curr_time.tv_nsec + (1000000000-start_time.tv_nsec);
		}
		else
		{
			elapsed_nsec = curr_time.tv_nsec - start_time.tv_nsec; //normal subtract no-rollover
		}
		
		if(elapsed_nsec >= 1000000) //1 millisecond = 1,000,000 nanoseconds
			is_elapsed = 1;
	}

	//update pmouseState with the final mouse state:
	*pmouseState = mouse_state;	
	if(was_down_event == 1)
		*pmouseState |= 2;

	return 1;
}

static int CheckKeycodeBit(char * bitArray, int keyCode)
{
	int byteIndex;
	char bitIndex;
	char byteMask;

	byteIndex = keyCode >> 3;
	bitIndex = (keyCode & 7);
	byteMask = 1 << bitIndex;

	if((bitArray[byteIndex] & byteMask) == byteMask)
	{
		return 1;
	}
	
	return 0;
}
