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

int main(int argc, char *argv[]) {

	// 1. Validate Command Line Arguments
	if(argc != 8){
        	fprintf(stderr, "Usage: %s <server-host> <port> <timeout-ms> <data-loss-rate> <seed> <ack-loss-rate> <seed>\n", argv[0]);
        	exit(EXIT_FAILURE);
    	}

	char *server_ip = argv[1];
    	int port = atoi(argv[2]);
	int timeout = atoi(argv[3]);
	double data_loss_rate = atof(argv[4]);
	unsigned int data_seed = (unsigned int)strtoul(argv[5], NULL, 10);
	double ack_loss_rate = atof(argv[6]);
	unsigned int ack_seed = (unsigned int)strtoul(argv[7], NULL, 10);
	(void)data_loss_rate;
	(void)data_seed;

	// The loss simulator for client
	lossy_init(ack_loss_rate, ack_seed);

	// Prompt user for filename
	char filename[MAX_DATA_SIZE + 1];
	printf("Enter filename to request: ");
	if(fgets(filename, sizeof(filename), stdin) == NULL){
		fprintf(stderr, "Error: Failed to read filename\n");
		exit(EXIT_FAILURE);
	}
	// Strip trailing newline
	filename[strcspn(filename, "\r\n")] = 0;

    	if(strlen(filename) == 0 || strlen(filename) > MAX_DATA_SIZE){
        	fprintf(stderr, "Error: Invalid filename length.\n");
        	exit(EXIT_FAILURE);
    	}

	// 2. UDP Socket Creation
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

	// 3. Send REQUEST packet
	char request_buffer[MAX_PACKET_SIZE];
	int request_len = serialize_packet(PKT_REQUEST, 0, strlen(filename), filename, strlen(filename), request_buffer);	
	
	if(sendto(sockfd, request_buffer, request_len, 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
		fprintf(stderr, "Error: Failed to send request\n");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	// 4. Open output file
	FILE *outfile = fopen("out.txt", "w");
	if(!outfile){
		fprintf(stderr, "Error: Failed to create output file out.txt\n");
        	close(sockfd);
        	exit(EXIT_FAILURE);
	}

	// 5. Go-Back-N Receiver
	uint16_t expected_seq = 1;
	long long bytes_received = 0;
    	int packets_accepted = 0;
    	int packets_discarded = 0;
	char rcv_buffer[MAX_PACKET_SIZE];
    	struct sockaddr_in from_addr;
    	socklen_t from_len = sizeof(from_addr);
	int transfer_done = 0;
	uint16_t final_eot_seq = 0;

	while(!transfer_done){
		ssize_t n = recvfrom(sockfd, rcv_buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);

		if(n < 0){
			if(errno != EINTR){
				fprintf(stderr, "Error: Failed to recvfrom\n");
				break;
			}
			continue;
		}

		// Ignore packets from unauthorized sources
		if(from_addr.sin_addr.s_addr == server_addr.sin_addr.s_addr && from_addr.sin_port == server_addr.sin_port){
			if(n >= HEADER_SIZE){
				uint16_t packet_type;
				uint16_t packet_seq;
				uint16_t packet_len;
				char data_buffer[MAX_DATA_SIZE];

				deserialize_packet(rcv_buffer, data_buffer, &packet_type, &packet_seq, &packet_len);
				
				// Handle EOT packet
				if(packet_type == PKT_EOT){
					final_eot_seq = packet_seq;
					printf("EOT %u received\n", packet_seq);
					
					char ack_buffer[MAX_PACKET_SIZE];
					int ack_len = serialize_packet(PKT_ACK, packet_seq, 0, NULL, 0, ack_buffer);
					lossy_sendto(sockfd, ack_buffer, ack_len, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
					printf("ACK %u sent\n", packet_seq);
					
					transfer_done = 1;
					break;
				}

				// Handle DATA packets
				if(packet_type == PKT_DATA){
					if(packet_seq == expected_seq){
						printf("DATA %u received with %u data bytes\n", packet_seq, packet_len);
						fwrite(data_buffer, 1, packet_len, outfile);
                    				bytes_received += packet_len;
                    				packets_accepted++;

                    				// Send cumulative ACK
                    				char ack_buffer[MAX_PACKET_SIZE];
                    				int ack_len = serialize_packet(PKT_ACK, expected_seq, 0, NULL, 0, ack_buffer);
						lossy_sendto(sockfd, ack_buffer, ack_len, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));	
						printf("ACK %u sent\n", expected_seq);

						expected_seq++;
					}
					else{
						// Duplicate or out-of-order packet
						packets_discarded++;
						printf("DATA %u discarded; expected packet %u\n", packet_seq, expected_seq);

						uint16_t ack_to_send = (expected_seq > 1) ? (expected_seq - 1) : 0;
                        			char ack_buffer[MAX_PACKET_SIZE];
                        			int ack_len = serialize_packet(PKT_ACK, ack_to_send, 0, NULL, 0, ack_buffer);
                        			lossy_sendto(sockfd, ack_buffer, ack_len, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
						printf("ACK %u resent\n", ack_to_send);
					}
				}
			}
		}
	}

	fclose(outfile);

	// 6. EOT
	long long linger_time_remaining_ms = 2LL * timeout;
	while(linger_time_remaining_ms > 0){
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(sockfd, &readfds);

		struct timeval tv;
		tv.tv_sec = linger_time_remaining_ms / 1000;
		tv.tv_usec = (linger_time_remaining_ms % 1000) * 1000;

		long long loop_start_ms = linger_time_remaining_ms;
		int ret = select(sockfd + 1, &readfds, NULL, NULL, &tv);

		if(ret < 0) {
			if (errno != EINTR) break;
				continue;
		} 
		else if (ret == 0) {
			break;
		}
		else {
			char linger_rcv_buffer[MAX_PACKET_SIZE];
			struct sockaddr_in linger_from_addr;
			socklen_t linger_from_len = sizeof(linger_from_addr);
			ssize_t n = recvfrom(sockfd, linger_rcv_buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&linger_from_addr, &linger_from_len);

			long long elapsed_ms = loop_start_ms - (tv.tv_sec * 1000 + tv.tv_usec / 1000);
			if (elapsed_ms < 0) elapsed_ms = 0;
			linger_time_remaining_ms -= elapsed_ms;

			if(n >= HEADER_SIZE && linger_from_addr.sin_addr.s_addr == server_addr.sin_addr.s_addr && linger_from_addr.sin_port == server_addr.sin_port){
				uint16_t packet_type;
				uint16_t packet_seq; 
				uint16_t packet_len;
				char test[MAX_DATA_SIZE];
				deserialize_packet(linger_rcv_buffer, test, &packet_type, &packet_seq, &packet_len);

				if(packet_type == PKT_EOT && packet_seq == final_eot_seq){
					printf("EOT %u received again\n", packet_seq);
					char ack_buffer[MAX_PACKET_SIZE];
					int ack_len = serialize_packet(PKT_ACK, final_eot_seq, 0, NULL, 0, ack_buffer);
					lossy_sendto(sockfd, ack_buffer, ack_len, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
					printf("ACK %u sent\n", final_eot_seq);
					
					// Restart linger interval on duplicate EOT
					linger_time_remaining_ms = 2LL * timeout;
				}
			}
		}
	}

	// 7. Print Statistics
    	printf("Number of data packets accepted: %d\n", packets_accepted);
    	printf("Number of data packets discarded: %d\n", packets_discarded);
    	printf("Total number of data bytes written: %lld\n", bytes_received);

	close(sockfd);
	return 0;
}
