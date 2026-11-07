#define PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

/* Packet Types */
#define PKT_REQUEST 1
#define PKT_DATA    2
#define PKT_ACK     3
#define PKT_EOT     4

#define HEADER_SIZE 6
#define MAX_DATA_SIZE 80
#define MAX_PACKET_SIZE 86

typedef struct {
    uint16_t type;
    uint16_t seq;     
    uint16_t len;
} Packet;

static int serialize_packet(uint16_t type, uint16_t seq, uint16_t len, const char *data, uint16_t data_len, char *destination_buffer){

	uint16_t net_type = htons(type);
        uint16_t net_seq = htons(seq);
	uint16_t net_len = htons(len);

	memcpy(destination_buffer, &net_type, 2);
        memcpy(destination_buffer + 4, &net_seq, 2);
        memcpy(destination_buffer + 2, &net_len, 2);

	if (data != NULL && data_len > 0) {
        	memcpy(destination_buffer + HEADER_SIZE, data, data_len);
    	}

	return HEADER_SIZE + len;

}

static int deserialize_packet(){

	

	return 0;

}

#endif /* PROTOCOL_H */
