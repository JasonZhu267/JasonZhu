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
	
	printf("Sending request for file: %s\n", filename);
	
	// loop until packet starts arriving or handshake is completed
	
	for(int i = 0; i < 5; i++){
		lossy_sendto(sockfd, request_buffer, request_len, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
	}	



	// 4. Open file
	char output_filename[256];
	
	sprintf(output_filename, "received_%s", filename);
        printf("Saving received data to: %s\n", output_filename);	

	FILE *outfile = fopen(output_filename, "w");

	if(!outfile){
		fprintf(stderr, "Error: Failed to create output file %s\n", output_filename);
        	close(sockfd);
        	exit(EXIT_FAILURE);
	}

	

	// 5. Go-Back-N
	long long bytes_received = 0;
    	int packets_received = 0;
    	int duplicate_packets_dropped = 0;
	char rcv_buffer[MAX_PACKET_SIZE];
    	struct sockaddr_in from_addr;
    	socklen_t from_len = sizeof(from_addr);
	
	while(1){
		ssize_t n = recvfrom(sockfd, rcv_buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);

		if(n < 0){
			if(errno != EINTR){
				fprintf(stderr, "Error: Failed to recvfrom\n");
				break;
			}
		}

		if(from_addr.sin_addr.s_addr == server_addr.sin_addr.s_addr && from_addr.sin_port == server_addr.sin_port){
			
			if(n >= HEADER_SIZE){
				uint16_t packet_type;
				uint16_t packet_seq;
				uint16_t packet_len;
				char data_buffer[MAX_DATA_SIZE];

				deserialize_packet(rcv_buffer, data_buffer, &packet_type, &packet_seq, &packet_len);
				
				// Check if packet is end of transmission packet
				if (packet_type == PKT_EOT) {
				
					char ack_buffer[MAX_PACKET_SIZE];
                                        int ack_len = serialize_packet(PKT_ACK, packet_seq, 0, NULL, 0, ack_buffer);
					
					printf("Received EOT packet %u.\n", packet_seq);
					lossy_sendto(sockfd, ack_buffer, ack_len, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
					break;

				}

				// Handle incoming packets
				if(packet_type == PKT_DATA){
					
					if(packet_seq == seq){
						fwrite(data_buffer, 1, packet_len, outfile);
                    				bytes_received += packet_len;
                    				packets_received++;

                    				// Send cumulative ACK for this sequence number
                    				char ack_buffer[MAX_PACKET_SIZE];
                    				int ack_len = serialize_packet(PKT_ACK, seq, 0, NULL, 0, ack_buffer);
                    				
						lossy_sendto(sockfd, ack_buffer, ack_len, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));	
						seq++;
					}
				
					else if(packet_seq < seq){
						duplicate_packets_dropped++;

						// Send last successful packet Ack
                                                char ack_buffer[MAX_PACKET_SIZE];
                                                int ack_len = serialize_packet(PKT_ACK, seq-1, 0, NULL, 0, ack_buffer);
                                                
                                                lossy_sendto(sockfd, ack_buffer, ack_len, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
					}
				
					else{
						// Out-of-order packet
						if(seq > 1){
							char ack_buffer[MAX_PACKET_SIZE];
                       					int ack_len = serialize_packet(PKT_ACK, seq - 1, 0, NULL, 0, ack_buffer);
                       					lossy_sendto(sockfd, ack_buffer, ack_len, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
						}
					}
				}
			}
		}
	}

	fclose(outfile);

	// 6. Print Statistics
	printf("\n--- Client Statistics ---\n");
    	printf("Total data packets accepted: %d\n", packets_received);
    	printf("Total bytes written to %s: %lld\n", output_filename, bytes_received);
    	printf("Duplicate/old packets handled: %d\n", duplicate_packets_dropped);

	close(sockfd);
	return 0;
	
}
