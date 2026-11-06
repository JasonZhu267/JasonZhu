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
#define SERVER_IP "127.0.0.1"
#define MAX_DATA_SIZE 80

// Helper function that loop until all bytes is sent over the TCP socket.
// Return 0 when successful transmit or -1 for error.
int send_all(int socket, const void *buffer, size_t len){
	size_t total = 0;
	const char *ptr = buffer;
	while(total < len){
		ssize_t sent = send(socket, ptr+total, len-total, 0);
		if(sent <= 0){
			return -1; // return a error for unexpected events
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
	
        // Initiate server    
	int client_socket;
	struct sockaddr_in serv_addr;
	char filename[MAX_DATA_SIZE+1];
	
	// Prompting user for filename
	printf("Enter filename: ");
	if(fgets(filename, sizeof(filename), stdin) == NULL){
		fprintf(stderr, "Invalid input: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	filename[strcspn(filename, "\n")] = '\0';

	size_t filename_len = strlen(filename);

	// Makes sure filename length is not 0 and capped at 80 bytes
	if(filename_len == 0 || filename_len > MAX_DATA_SIZE){
		fprintf(stderr, "Filename length must be between 1 and %d bytes.\n", MAX_DATA_SIZE);
		exit(EXIT_FAILURE);
	}

	// Create TCP socket
	client_socket = socket(AF_INET, SOCK_STREAM, 0);
	if(client_socket < 0){
		fprintf(stderr, "Socket creation failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT);
	if(inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0 ){
		fprintf(stderr, "Invalid address or address no supported: %s\n", strerror(errno));
                close(client_socket);
		exit(EXIT_FAILURE);
	}

	// Establish coonection with server
	if(connect(client_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
                fprintf(stderr, "Server failed to connect: %s\n", strerror(errno));
                close(client_socket);
                exit(EXIT_FAILURE);
	}

	// Send Filename Packet
	uint16_t f_count = htons((uint16_t)filename_len);
   	uint16_t f_seq = htons(0);

	if(send_all(client_socket, &f_count, sizeof(f_count)) < 0 || send_all(client_socket, &f_seq, sizeof(f_seq)) < 0 || send_all(client_socket, filename, filename_len) < 0){
		fprintf(stderr, "Failed to send file packets: %s\n", strerror(errno));
                close(client_socket);
                exit(EXIT_FAILURE);
        }

	FILE *outfile = fopen("out.txt", "w");
	if(!outfile){
        	fprintf(stderr, "Failed to open out.txt for writing: %s\n", strerror(errno));
        	close(client_socket);
        	exit(EXIT_FAILURE);
    	}

	// Receive stream packets
	unsigned long packets_received = 0;
    	unsigned long bytes_received = 0;

	while(1){
		uint16_t p_count, seq_num;
		if(rcv(client_socket, &p_count, sizeof(p_count)) < 0 || rcv(client_socket, &seq_num, sizeof(seq_num)) < 0){
			fprintf(stderr, "Failed to receive packets: %s\n", strerror(errno));
                	fclose(outfile);
			close(client_socket);
                	exit(EXIT_FAILURE);
		}
		
		uint16_t count = ntohs(p_count);
        	uint16_t seq = ntohs(seq_num);

		// Base Case
		if (count == 0) {
            		printf("End of Transmission Packet with sequence number %u received with 0 data bytes\n", seq);
            		break;
        	}

		char data_buffer[MAX_DATA_SIZE];
		if(rcv(client_socket, data_buffer, count) < 0){
			fprintf(stderr, "Failed to receive packet data: %s\n", strerror(errno)); 
                        fclose(outfile);
			close(client_socket);
                        exit(EXIT_FAILURE);
		}
		
		fwrite(data_buffer, 1, count, outfile);
		printf("Packet %u received with %u data bytes.\n", seq, count);
		packets_received++;
		bytes_received+=count;

	}
		
	fclose(outfile);
	
	printf("Number of data packets received: %lu\n", packets_received);
    	printf("Total number of data bytes received: %lu\n", bytes_received);
	
	return 0;
}
