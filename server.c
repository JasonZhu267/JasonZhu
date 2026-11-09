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

	// 1. Validate Command Line Arguments
	if(argc != 4){
		fprintf(stderr, "Must contain only the program, port number, window size, and timeout\n");
		exit(EXIT_FAILURE);
	}
	
	int port = atoi(argv[1]);
	int window_size = atoi(argv[2]);
	int timeout = atoi(argv[3]);

	if (window_size <= 0){
		fprintf(stderr, "Error: Window size must be greater than 0.\n");
		exit(EXIT_FAILURE);
	} 

	// 2. Create UDP Socket and Binding
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

        if(bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
        	fprintf(stderr, "Error: Bind Failed\n");
        	close(sockfd);
        	exit(EXIT_FAILURE);
        }

        printf("Server listening on port %d...\n", port);

	// 3. Wait for client REQUEST packet
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	char rcv_buffer[MAX_PACKET_SIZE];
	char filename[MAX_DATA_SIZE + 1];
	memset(filename, 0, sizeof(filename));
	uint16_t request_type;
	uint16_t request_seq;
	uint16_t request_len;
	int received = 0;
	
	// Loop until a valid request from the client is received
	while(!received){
		ssize_t n = recvfrom(sockfd, rcv_buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, &client_len);
		
		if(n >= HEADER_SIZE){
			deserialize_packet(rcv_buffer, filename, &request_type, &request_seq, &request_len);
			if((request_type == PKT_REQUEST) && (request_len > 0 && request_len <= MAX_DATA_SIZE)){
				filename[request_len] = '\0';
				received = 1;
			}	
		}
	}

	printf("Received request for file %s\n", filename);

	// 4. Open file
	FILE *file = fopen(filename, "r");
	
	if(!file){
		fprintf(stderr, "Error: Failed to open file\n");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	// 5. Read File Lines for GBN Transmission
	char file_lines[65536][MAX_DATA_SIZE];
    	uint16_t line_len[65536];
    	int total = 0;

	while(total < 65536){
		int c;
		int i = 0;
		
		while ((i < MAX_DATA_SIZE - 1) && ((c = fgetc(file)) != EOF)) {
            		file_lines[total][i++] = (char)c;
            		if (c == '\n'){
				break;
			}
        	}

		if (i == 0){
			 break;
		}
        	
		line_len[total] = i;
        	total++;
    	}
    	fclose(file);

	// 6. Go-Back-N Transmission
	uint16_t curr = 1;
	uint16_t next = 1;
	int total_packet = total;
	int transmission_ct = 0;
	int timeout_ct = 0;
	long long bytes_sent = 0;
	char window_packet[window_size][MAX_PACKET_SIZE];
	uint16_t window_packet_len[window_size];
	
	// Loop until all packets are acknowedged
	while(curr <= total_packet){
		while((next < (curr + window_size)) && (next <= total_packet)){
			int line_index = next - 1;
			
			char send_buffer[MAX_PACKET_SIZE];
			
			int packet_len = serialize_packet(PKT_DATA, next, line_len[line_index],file_lines[line_index], line_len[line_index], send_buffer);
			
			int window_index = (next-1) % window_size;
			
			memcpy(window_packet[window_index], send_buffer, packet_len);
			
			window_packet_len[window_index] = packet_len;
			
			lossy_sendto(sockfd, send_buffer, packet_len, 0, (struct sockaddr *)&client_addr, client_len);	
			
			bytes_sent += line_len[line_index];

			next++;
		}
		
		// Monitor and handle timeouts
		fd_set readfds;
        	FD_ZERO(&readfds);
        	FD_SET(sockfd, &readfds);

		struct timeval tv;
        	tv.tv_sec = timeout / 1000;
        	tv.tv_usec = (timeout % 1000) * 1000;
		
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
                		transmission_ct++;
			}			
		}

		else{
			char ack_buffer[MAX_PACKET_SIZE];
            		struct sockaddr_in from_addr;
            		socklen_t from_len = sizeof(from_addr);

            		ssize_t n = recvfrom(sockfd, ack_buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);
            
            		// Ignore packets from unauthorized sources
            		if(from_addr.sin_addr.s_addr == client_addr.sin_addr.s_addr && from_addr.sin_port == client_addr.sin_port){

            			if(n >= HEADER_SIZE){
                			uint16_t ack_type;
					uint16_t ack_seq;
					uint16_t ack_len;
                			char test_data[MAX_DATA_SIZE];
                			deserialize_packet(ack_buffer, test_data, &ack_type, &ack_seq, &ack_len);

                			// Check for cumulative ACK
                			if(ack_type == PKT_ACK && ack_seq >= curr){
                			    	curr = ack_seq + 1;
						if(next < curr){
							next = curr;
						}
               			 	}
            			}			
			}
		}
	}

	// 7. Termination
	uint16_t eot_seq = total_packet + 1;
	char eot_buffer[MAX_PACKET_SIZE];
	int eot_len = serialize_packet(PKT_EOT, eot_seq, 0, NULL, 0, eot_buffer); 
	int eot_ack = 0;

	// Acknowledge eot packet
	while(!eot_ack){
		lossy_sendto(sockfd, eot_buffer, eot_len, 0, (struct sockaddr *)&client_addr, client_len);
		fd_set readfds;
        	FD_ZERO(&readfds);
        	FD_SET(sockfd, &readfds);

        	struct timeval tv;
        	tv.tv_sec = timeout / 1000;
        	tv.tv_usec = (timeout % 1000) * 1000;
		int return_value = select(sockfd + 1, &readfds, NULL, NULL, &tv);
	
		if(return_value < 0){
			if(errno != EINTR){
				fprintf(stderr, "Error: Failed to select during EOT\n");
				break;
			}
		}
		else if(return_value == 0){
			timeout_ct++;
			lossy_sendto(sockfd, eot_buffer, eot_len, 0, (struct sockaddr *)&client_addr, client_len);
			transmission_ct++;
		}
		else{
			char ack_buffer[MAX_PACKET_SIZE];
			struct sockaddr_in from_addr;
            		socklen_t from_len = sizeof(from_addr);
			ssize_t n = recvfrom(sockfd, ack_buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);
			
			if((from_addr.sin_addr.s_addr == client_addr.sin_addr.s_addr) && (from_addr.sin_port == client_addr.sin_port && n >= HEADER_SIZE)){
                		uint16_t ack_type;
				uint16_t ack_seq;
				uint16_t ack_len;
                		char x[MAX_DATA_SIZE];
                		deserialize_packet(ack_buffer, x, &ack_type, &ack_seq, &ack_len);

                		if (ack_type == PKT_ACK && ack_seq == eot_seq) {
                    			printf("EOT %u acknowledged\n", eot_seq);
                    			eot_ack = 1;
                		}
            		}
		}
	}



	// 8. Print Statistics
	printf("\n--- Server Statistics ---\n");
    	printf("Original data packet count: %d\n", total_packet);
    	printf("Retransmission count: %d\n", transmission_ct);
    	printf("Total original data bytes: %lld\n", bytes_sent);
    	printf("Timeout count: %d\n", timeout_ct);

	close(sockfd);
	return 0;

}
