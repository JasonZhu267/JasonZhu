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
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    	if (sockfd < 0) {
        	fprintf(stderr, "Error: Socket Creation Failed\n");
        	exit(EXIT_FAILURE);
    	}

    	struct sockaddr_in server_addr;
    	memset(&server_addr, 0, sizeof(server_addr));
    	server_addr.sin_family = AF_INET;
    	server_addr.sin_addr.s_addr = INADDR_ANY;
    	server_addr.sin_port = htons(port);

        if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        	fprintf(stderr, "Error: Bind Failed\n");
        	close(sockfd);
        	exit(EXIT_FAILURE);
        }

        printf("Server listening on port %d...\n", port);

	// Wait for client REQUEST packet
	
	

}
