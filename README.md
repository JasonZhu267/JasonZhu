# GBN Implementation Programming Assignment


---


Group Members

* Jason Zhu (Student ID: 702734977)


---


List of Relevant Files

* server.c: Source code for GBN sender. Handle file, packet serialization, sliding window, retransimission, and EOT handshake.

* client.c: Source code for GBN receiver. Prompt user for filename, send initial request, accept data packets, write out.txt, send cumulative ACKs, and manage end-of-transmission.

* protocol.h: Header file that defines packet types, structure constants, and healper function for serialization and deserialization of packets

* lossy.h: Header file for the loss simulation

* lossy.c: Source code for the loss simulation

* Makefile: Script for compiling program

* README.md: Documentation File


---


Implementation Explanations

* Sender State (base, next_seq, window, and timer):
	- base: The sequence number of the oldest unacknowledged data packet in the window
	- next_seq: The sequence number assigned to the next data packet sent
	- window: The max number of unacknowledged data packet allowed to be sent at a time.
	- timer: It starts when a packet is sent to a window and restarts whenever the base advances or a timeout occurs.

* How Cumulative ACKs Move the Sender Window:
	- ACK carrying the sequence number n acknowledges every data packet up to packet n, including n.
	- When a valid ACK n arrives, the server sets base = n + 1 to slide the window.
	- If there is unacknowledged packets remaining, the timer restarts.

* Timeout and Retransmissions:
	- The server monitors the socket with a configured timeout with select().
	- When timer expires, the server records a timeout, retransmits all packets from base to next_seq - 1 with lossy_sendto().

* Handling Duplicates or Out-of-Order Packets:
	- When a packet matches expected_seq, the client writes the data into out.txt, increments expected_seq, and sends a cumulative ACK.
	- Otherwise, it discards the data and resends an ACK for the most recently acknowledged packet. 

* Handling EOT and Lost EOT ACKs:
	- When all data packets are acknowledged, the server sends an EOT packet using lossy_sendto() and set a timer. 
	- When the client receives the EOT data packet, it closes out.txt and sends the EOT ACK.
	- It then enter a lingering state to acknowledge lost EOT ACKs and set a timer for the state.
	- It will reset the timer if duplicate EOTs arrive.

___


Compilation Instructions


To compile the programs on Linux system or macOS terminal, run make.


---


Running Instruction

1) create a test file

2) run ./server <port> <window-size> <timeout-ms>

3) open a new terminal and run ./client <server-host> <port> <timeout-ms> <data-loss-rate> <seed> <ack-loss-rate> <seed>

4) input test file when prompted by client

5) run make clean to remove server, client, and out.txt
