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
		fprintf(stderr, "Usage: %s <port> <window-size> <timeout-ms>\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	int port = atoi(argv[1]);
	int window_size = atoi(argv[2]);
	int timeout = atoi(argv[3]);

	if(window_size <= 0 || window_size > 65534){
		fprintf(stderr, "Error: Window size must be between 1 and 65534.\n");
		exit(EXIT_FAILURE);
	} 
		
	if(port < 1024 || port > 65535){
		fprintf(stderr, "Error: Port must be between 1024 and 65535.\n");
		exit(EXIT_FAILURE);
	}
	if(timeout <= 0){
		fprintf(stderr, "Error: Timeout must be a positive integer.\n");
		exit(EXIT_FAILURE);
	}

	// 2. Create UDP Socket and Binding
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    	if(sockfd < 0){
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
		if(n < 0){
			continue;
		}
		
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
		fprintf(stderr, "Error: Failed to open file %s\n", filename);
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	// 5. Read File Lines for GBN
	char *file_lines = malloc(65534 * MAX_DATA_SIZE);
	uint16_t *line_len = malloc(65534 * sizeof(uint16_t));
	if (!file_lines || !line_len) {
		fprintf(stderr, "Error: Memory allocation failed\n");
		fclose(file);
		close(sockfd);
		exit(EXIT_FAILURE);
	}
	memset(file_lines, 0, 65534 * MAX_DATA_SIZE);

    	int total = 0;
	while(total < 65534){
		int c;
		int i = 0;
		char *current_line = file_lines + (total * MAX_DATA_SIZE);
		
		while((i < MAX_DATA_SIZE) && ((c = fgetc(file)) != EOF)){
            		current_line[i++] = (char)c;
            		if (c == '\n'){
				break;
			}
        	}

		if(ferror(file)){
			fprintf(stderr, "Error reading file stream\n");
			free(file_lines);
			free(line_len);
			fclose(file);
			close(sockfd);
			exit(EXIT_FAILURE);
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

	char *window_packet = malloc(window_size * MAX_PACKET_SIZE);
	uint16_t *window_packet_len = malloc(window_size * sizeof(uint16_t));
	if(!window_packet || !window_packet_len){
		fprintf(stderr, "Error: Window buffer memory allocation failed\n");
		free(file_lines);
		free(line_len);
		close(sockfd);
		exit(EXIT_FAILURE);
	}
	
	// Loop until all packets are acknowledged
	while(curr <= total_packet){
		while((next < (curr + window_size)) && (next <= total_packet)){
			int line_index = next - 1;
			char send_buffer[MAX_PACKET_SIZE];
			char *line_ptr = file_lines + (line_index * MAX_DATA_SIZE);
			
			int packet_len = serialize_packet(PKT_DATA, next, line_len[line_index], line_ptr, line_len[line_index], send_buffer);
			int window_index = (next - 1) % window_size;
			
			char *window_ptr = window_packet + (window_index * MAX_PACKET_SIZE);
			memcpy(window_ptr, send_buffer, packet_len);
			window_packet_len[window_index] = packet_len;
			
			lossy_sendto(sockfd, send_buffer, packet_len, 0, (struct sockaddr *)&client_addr, client_len);	
			printf("DATA %u sent with %u data bytes\n", next, line_len[line_index]);

			bytes_sent += line_len[line_index];
			next++;
		}
		
		// Monitor and handle timeouts via select()
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
			printf("Timeout for packet %u\n", curr);			

			for(uint16_t seq = curr; seq < next; seq++){
				int window_index = (seq - 1) % window_size;
				char *win_ptr = window_packet + (window_index * MAX_PACKET_SIZE);
				
				lossy_sendto(sockfd, win_ptr, window_packet_len[window_index], 0, (struct sockaddr *)&client_addr, client_len);
                		
				uint16_t l_len = window_packet_len[window_index] - HEADER_SIZE;
				printf("DATA %u retransmitted with %u data bytes\n", seq, l_len);
				transmission_ct++;
			}			
		}
		else{
			char ack_buffer[MAX_PACKET_SIZE];
            		struct sockaddr_in from_addr;
            		socklen_t from_len = sizeof(from_addr);

            		ssize_t n = recvfrom(sockfd, ack_buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);
            		if(n < 0){
                		continue; 
            		}

            		// Process only packets from authorized client
            		if(from_addr.sin_addr.s_addr == client_addr.sin_addr.s_addr && from_addr.sin_port == client_addr.sin_port){
            			if(n >= HEADER_SIZE){
                			uint16_t ack_type;
					uint16_t ack_seq;
					uint16_t ack_len;
                			char test_data[MAX_DATA_SIZE];
                			deserialize_packet(ack_buffer, test_data, &ack_type, &ack_seq, &ack_len);

                			// Check for cumulative ACK
                			if(ack_type == PKT_ACK && ack_seq >= curr && ack_seq <= total_packet){
                				printf("ACK %u received\n", ack_seq); 
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

	lossy_sendto(sockfd, eot_buffer, eot_len, 0, (struct sockaddr *)&client_addr, client_len);
	printf("EOT %u sent\n", eot_seq);

	// Acknowledge EOT packet loop
	while(!eot_ack){
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
			printf("EOT %u retransmitted\n", eot_seq);
		}
		else{
			char ack_buffer[MAX_PACKET_SIZE];
			struct sockaddr_in from_addr;
            		socklen_t from_len = sizeof(from_addr);
			ssize_t n = recvfrom(sockfd, ack_buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);
			
			if(n < 0){
				continue;
			}

			if((from_addr.sin_addr.s_addr == client_addr.sin_addr.s_addr) && (from_addr.sin_port == client_addr.sin_port) && (n >= HEADER_SIZE)){
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
    	printf("Number of original data packets: %d\n", total_packet);
    	printf("Number of data-packet retransmissions: %d\n", transmission_ct);
    	printf("Total number of original data bytes: %lld\n", bytes_sent);
    	printf("Number of timeouts: %d\n", timeout_ct);

	// Clean up dynamic arrays and close socket
	free(file_lines);
	free(line_len);
	free(window_packet);
	free(window_packet_len);
	close(sockfd);
	return 0;
}
