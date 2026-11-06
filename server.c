#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#define PORT 8080
#define BACKLOG 5
#define MAX_DATA_SIZE 80

// Helper function that loop until all bytes is sent over the TCP socket.
// Return 0 when successful transmit or -1 for error.
int send_all(int socket, const void *buffer, size_t len){
	size_t total = 0;
	const char *ptr = buffer;
	while(total < len){
		ssize_t sent = send(socket, ptr+total, len-total, 0);
		if(sent <= 0){
			return -1; // return an error for unexpected events
		} 
                total+=sent;
	}
	return 0;
}

// Helper function that byte received over TCP socket
// Return 0 when successful receive or -1 for error.
int rcv(int socket, void *buffer, size_t len){
	size_t total = 0;
	char *ptr = buffer;
        while(total < len){
                ssize_t received = recv(socket, ptr+total, len-total, 0);
                if(received <= 0){
                        return -1; // return a error for unexpected events
                }
                total+=received;
        }
        return 0;
}

int main(void){
	
        // initiate server    
	int server_fd, client_socket;
	struct sockaddr_in address;
	socklen_t addrlen = sizeof(address);
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	char filename[MAX_DATA_SIZE+1];
	
	// var for binding
	int opt = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // Prevent error "Address already in use"	
	
	// Creating TCP socket
	if(server_fd < 0){
		fprintf(stderr, "Socket creation failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Binding address and port
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(PORT); // Network byte order

	if(bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0){
                fprintf(stderr, "Bind failed: %s\n", strerror(errno));
		close(server_fd);
		exit(EXIT_FAILURE);
	}
		
	// Listening for client connections
	if(listen(server_fd, BACKLOG) < 0){
                fprintf(stderr, "Listen failed: %s\n", strerror(errno));
                close(server_fd);
                exit(EXIT_FAILURE);
        }

	printf("Server listening on port %d\n", PORT);

	// Accepting client connections
	client_socket = accept(server_fd, (struct sockaddr *) &address, &addrlen);
	if(client_socket < 0){
                fprintf(stderr, "Accept failed: %s\n", strerror(errno));
                close(server_fd);
                exit(EXIT_FAILURE);
        }
	
	// Receive filename packets
	uint16_t count, seq;
	if(rcv(client_socket, &count, sizeof(count)) < 0 || rcv(client_socket, &seq, sizeof(seq)) < 0){
                fprintf(stderr, "Receive failed: %s\n", strerror(errno));
		close(client_socket);
                close(server_fd);  
                exit(EXIT_FAILURE);
        }

	uint16_t filename_len = ntohs(count);
        uint16_t filename_seq = ntohs(seq);

	if(filename_seq != 0 || filename_len == 0 || filename_len > MAX_DATA_SIZE){
//		fprintf(stderr, "Invalid filename packet received: %s\n", strerror(errno));
		fprintf(stderr, "Rejected! seq=%u (expected 0), len=%u\n", filename_seq, filename_len);
		close(client_socket);
                close(server_fd);
                exit(EXIT_FAILURE);
	}

	if(rcv(client_socket, filename, filename_len) < 0){
                fprintf(stderr, "Cannot retrieve filename: %s\n", strerror(errno));
                close(client_socket);
                close(server_fd);
                exit(EXIT_FAILURE);
        }

	filename[filename_len]  = '\0';
	FILE *file = fopen(filename, "r");
	if(!file){
		fprintf(stderr, "Cannot open filename: %s\n", strerror(errno));
                close(client_socket);
                close(server_fd);
                exit(EXIT_FAILURE);
        }

	// Read file and transmit data packets
	uint16_t seq_num = 1;
        unsigned long data_packets = 0;
        unsigned long data_bytes = 0;
        char line_buffer[MAX_DATA_SIZE+1];

	while(fgets(line_buffer, sizeof(line_buffer), file) != NULL){
		size_t line_len = strlen(line_buffer);

		// Cap new line len at 80 bytes
		if(line_len > MAX_DATA_SIZE){
			line_len = MAX_DATA_SIZE;
		}

		uint16_t count = htons((uint16_t)line_len);
		uint16_t seq = htons(seq_num);

		if(send_all(client_socket, &count, sizeof(count)) < 0 || send_all(client_socket, &seq, sizeof(seq)) < 0){
                	fprintf(stderr, "Failure sending packet header: %s\n", strerror(errno));
                	close(client_socket);
                	close(server_fd);
                	exit(EXIT_FAILURE);
        	}

		if(send_all(client_socket, line_buffer, line_len) < 0){
                        fprintf(stderr, "Failure sending packet info: %s\n", strerror(errno));
                        close(client_socket);
                        close(server_fd);
                        exit(EXIT_FAILURE);
                }
		
		printf("Packet %u transmitted with %u data bytes.\n", seq_num, (uint16_t)line_len);
		data_packets++;
		data_bytes+=line_len;
		seq_num++;
	}

	fclose(file);

	// End-of-Transmission Packet
	uint16_t eot_count = htons(0);
	uint16_t eot_seq = htons(seq_num);
	
	if(send_all(client_socket, &eot_count, sizeof(eot_count)) < 0 || send_all(client_socket, &eot_seq, sizeof(eot_seq)) < 0){
                fprintf(stderr, "Failure sending EOT packet: %s\n", strerror(errno));
                close(client_socket);
                close(server_fd);
                exit(EXIT_FAILURE);
        }

	printf("End of Transmission Packet with sequence number %u transmitted with 0 data bytes\n", seq_num);

        
        printf("Number of data packets transmitted: %lu\n", data_packets);
        printf("Total number of data bytes transmitted: %lu\n", data_bytes);

        // Close sockets
        close(client_socket);
        close(server_fd);

	return 0;
}
