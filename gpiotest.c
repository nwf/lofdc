/*
 * GPIO RASPBERRYpi Tests
 * (c) 2012 Matthias Lee 
 */

#define DOOR_GPIO_PIN 21

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


int init_GPIO(int pin, int direction){
	// TODO: perhaps fail more gracefully? ret -1 on fail?
	//
	// check if pin has already been setup
	// int pin - pin to be exported as GPIO
	// int direction - 1 for input 0 for output
	char pinDir[36];
	int fd;
	snprintf(pinDir, sizeof pinDir, "/sys/class/gpio/gpio%d/", pin);
	if( access( pinDir, F_OK ) != -1 ) {
	    // already setup.. nothing todo
	    printf("Warning: GPIO pin %d has already been exported\n",pin);
	} else {
		if((fd = open("/sys/class/gpio/export", O_WRONLY|O_TRUNC))==-1) {
			printf("Error opening gpio/export\n");
			exit(-1);
		}
		
		// TODO: check if GPIO already exists
		char pinb[2];
		sprintf(pinb, "%d", pin);
		if( write(fd, (char*)pinb, 2) < 0){
			printf("Error exporting pin %d\n", pin);
			exit(-1);
		}
		close(fd);
	}
	
	// Setup pin direction Input/Output
	char opt[64];
	strcpy(opt,pinDir);
	strcat(opt,"direction");

	if((fd = open(opt, O_WRONLY|O_TRUNC)) == -1) {
		printf("Error opening gpio/export\n");
		exit(-1);
	}
	
	// TODO: check if GPIO exists
	if( direction == 0){	
		if(write(fd, (char*)"out", 3) < 0){
			printf("Error setting pin %d to OUT\n", pin);
			exit(-1);
		}
	} else if ( direction == 1){
		if(write(fd, (char*)"in", 2) < 0){
			printf("Error setting pin %d to IN\n", pin);
			exit(-1);
		}
	}
	close(fd);
	return 0;
}

int set_GPIO(int pin, int val){
	char pinDir[36];
	int fd;
	snprintf(pinDir, sizeof pinDir, "/sys/class/gpio/gpio%d/", pin);
	if( access( pinDir, F_OK ) != -1 ) {
		// already setup.. nothing todo
		char opt[64];
		strcpy(opt,pinDir);
		strcat(opt,"value");
		if((fd = open(opt, O_WRONLY|O_TRUNC)) == -1) {
			printf("Error opening gpio/gpio%d/value\n", pin);
			exit(-1);
		}
		
		if(val == 1){
			if(write(fd, (char*)"1", 3) < 0){
				printf("Error setting pin %d to OUT\n", pin);
				exit(-1);
			}
		}else if(val == 0){
			if(write(fd, (char*)"0", 3) < 0){
				printf("Error setting pin %d to OUT\n", pin);
				exit(-1);
			}
		} else {
			printf("Error: value %d out of range\n",val);
			return(-1);
		}
	} else {
		printf("Error GPIO pin %d has not been initialized\n",pin);
	}
	
	return 0;
	
}

int main(int argc, char **argv){
	init_GPIO(DOOR_GPIO_PIN, 0);
	set_GPIO(DOOR_GPIO_PIN, 1);
  return 0;
}
