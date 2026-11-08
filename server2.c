#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>

#include "protocol.h"
#include "lossy.h"

int main(int argc, char *argv[]){

	// Validate Command Line Arguments
	if(argc != 4){
		fprintf(stderr, "Must contain only the program, port number, window size, and timeout(ms\n");
		exit(EXIT_FAILURE):
	}
	
	int port = atoi(argv[1]);
	int window_size = atoi(argv[2]);
	int timeout = atoi(argv[3]);

	if (window_size <= 0){
		fprintf(stderr, "Error: Window size must be greater than 0.\n");
		exit(EXIT_FAILURE);
	} 

	// Create UDP Socket
	

}
