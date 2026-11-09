#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "protocol.h"
#include "lossy.h"

int main(int argc, char *argv[]) {



	// 1. Valid Command Line Arguments
	if(argc != 4){
        	fprintf(stderr, "Only include program, server_ip, port, and filename\n");
        	exit(EXIT_FAILURE);
    	}

	char *server_ip = argv[1];
    	int port = atoi(argv[2]);
    	char *filename = argv[3];

    	if(strlen(filename) > MAX_DATA_SIZE){
        	fprintf(stderr, "Error: Filename is too long.\n");
        	exit(EXIT_FAILURE);
    	}


	
	// 2. UDP Socket
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    	
	if(sockfd < 0){
        	fprintf(stderr, "Error: Failed to create socket\n");
        	exit(EXIT_FAILURE);
    	}

    	struct sockaddr_in server_addr;
    	memset(&server_addr, 0, sizeof(server_addr));
    	server_addr.sin_family = AF_INET;
    	server_addr.sin_port = htons(port);
    	
	if(inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0){
        	fprintf(stderr, "Error: Invalid Server IP Address\n");
        	close(sockfd);
        	exit(EXIT_FAILURE);
    	}



	// 3. Send Packet to Server
	char request_buffer[MAX_PACKET_SIZE];
	uint16_t seq = 1;
	int request_len = serialize_packet(PKT_REQUEST, 0, strlen(filename), filename, strlen(filename), request_buffer);	
	int sent_request = 0;
	
	printf("Sending request for file: %s\n", filename);
	
	// loop until packet starts arriving or handshake is completed
	while(sent_request < 10){
		lossy_sendto(sockfd, request_buffer, request_len, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
		sent_request++;
		break;
	}	



	// 4. Open file
	char filename[256];
	FILE *outfile = fopen(output_filename, "w");
	
	printf("Saving received data to: %s\n", output_filename);

	if(!outfile){
		fprintf(stderr, "Error: Failed to create output file %s\n", output_filename);
        	close(sockfd);
        	exit(EXIT_FAILURE);
	}

	

	// 5. Go-Back-N
	long long total_bytes_received = 0;
    	int total_packets_received = 0;
    	int duplicate_packets_dropped = 0;
	
	// 6. Print Statistics
		








	
}
