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
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	char rcv_buffer[MAX_PACKET_SIZE];
	char filename[MAX_DATA_SIZE + 1];
	memset(filename, 0, sizeof(filename));
	uint16_t request_type;
	uint16_t request_seq;
	uint16_t request_len;
	int received = 0;
	
	while(!received){
		ssize_t n = recvfrom(sockfd, recv_buf, MAX_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, &client_len);
		
		if(n > HEADER_SIZE){
			deserialize_packet(rcv_buffer, filename, &request_type, &request_seq, &request_len);
			if((request_type == PKT_REQUEST) && (request_len > 0 && request_len <= MAX_DATA_SIZE)){
				filename[request_len] = '\0;
				received = 1;
			}	
		}
	}

	printf("Received request for file %s\n", filename);

	// Open file
	File *file = fopen(filename, r);
	
	if(!file){
		fprintf(stderr, "Error: Failed to open file\n");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	// Read File Lines for GBN Transmission
	char file_lines[65536][MAX_DATA_SIZE];
    	uint16_t line_len[65536];
    	int total = 0;

	while(total < 65536)P
		int c;
		int i = 0;
		
		while ((i < MAX_DATA_SIZE - 1) && ((c = fgetc(file)) != EOF)) {
            		file_lines[total_lines][i++] = (char)c;
            		if (c == '\n') break;
        	}

		if (i == 0){
			 break;
		}
        	
		line_len[total] = i;
        	total++;
    	}
    	fclose(file);

	// Go-Back-N
	unint_t curr = 1;
	unint_t next = 1;
	int total_packet = total;
	int transmission_ct = 0;
	int timeout_ct = 0;
	long long byte_sent = 0;
	char window_packet[window_size][MAX_PACKET_SIZE];
	uint16_t window_packet_len[window_size];
	
	while(curr <= total_packet){
		while(next < (curr + window_size)){
			int line_index = next+1;
			
			char send_buffer[MAX_PACKET_SIZE];
			
			int packet_len = serialize_packet(PKT_DATA, next, line_len[line_index],file_lines[line_index], line_len[line_index], send_buffer);
			
			int window_index = (next-1) % window_size;
			
			memcpy(window_packet[window_index], send_buffer, packet_len);
			
			window_packet_len[window_index] = packet_len;
			
			lossy_sendto(sockfd, send_buffer, packet_len, 0, (struct sockaddr *)&client_addr, client_len);	
			
			btye_sent += line_len[line_index];

			next++;
		}
		
		fd_set readfds;
        	FD_ZERO(&readfds);
        	FD_SET(sockfd, &readfds);

		struct timeval tv;
        	tv.tv_sec = timeout_ms / 1000;
        	tv.tv_usec = (timeout_ms % 1000) * 1000;
		
		int return_value = select(sockfd + 1, &readfds, NULL, NULL, &tv);
		
		if(return_value < 0){
			if(errno != EINTR){
				fprintf(stderr, "Error: Failed to select\n");
				break;
			}
		}

		else if(return_value == 0){
			timeout_ct++;
			
			for(uint16_t seq = curr; seq < next; seq++){
				int window_index = (seq - 1) % window_size;
				lossy_sendto(sockfd, window_packet[window_index], window_packet_len[window_index], 0, (struct sockaddr *)&client_addr, client_len);
                		transmissions_count++;
			}			
		}

		else{
			char ack_buf[MAX_PACKET_SIZE];
            		struct sockaddr_in from_addr;
            		socklen_t from_len = sizeof(from_addr);

            		ssize_t n = recvfrom(sockfd, ack_buf, MAX_PACKET_SIZE, 0,
                                 (struct sockaddr *)&from_addr, &from_len);
            
            		// Security check: Ignore packets from unauthorized sources
            		if(from_addr.sin_addr.s_addr != client_addr.sin_addr.s_addr || from_addr.sin_port != client_addr.sin_port){

            			if(n >= HEADER_SIZE){
                			uint16_t ack_type, ack_seq, ack_len;
                			char dummy_data[MAX_DATA_SIZE];
                			deserialize_packet(ack_buf, dummy_data, &ack_type, &ack_seq, &ack_len);

                			// Check for cumulative ACK
                			if(ack_type == PKT_ACK && ack_seq >= base && ack_seq < next_seq){
                			    	base = ack_seq + 1;
               			 	}
            			}			
			}
		}
	}

	// Termination
	

}
