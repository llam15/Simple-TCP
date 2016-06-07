#include <algorithm>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>

#include "TCP.h"

using namespace std;

char* PORT;
string FILENAME;
struct timeval start, current;

int CWND = 1024;            // Initial congestion window size
int SSTHRESH = 15360;       // Initial slow start threshold
int LAST_SENT = -1;         // Last byte sent
int LAST_ACKED = -1;        // Last byte ACKed
int DATA_INDEX = 0;         // Index of file to read from
int ACKED_DATA_INDEX = 0;   // Index of file of last acked data
int state = 0;              // 0 = not connected, 1 = SYN, 2 = regular, 3 = FIN
FILE* file;                 // File descriptor
bool sentFIN = false;

const int MSS		  = 1024;
const int HEADER_LEN  = 8; 	  // Length of header
const int MAX_PKT_LEN = 1032; // Max packet = 1032. Max payload = 1024
const int MAX_SEQ_NUM = 30720;

const timeval TIMEOUT{0, 500000};

void usage_msg()
{
	cerr << "usage: server [port-number] [file-name]" << endl;
	exit(0);
}

// Called when timer times out
void timeout(const int sockfd, const struct sockaddr_storage &cli_addr, socklen_t &cli_addrlen) {
    // Set slow start threshold to half of CWND (or 1024 if <1024)
    SSTHRESH = CWND/2;
    if (SSTHRESH < 1024) {
        SSTHRESH = 1024;
    }
    
    // Reset CWND
    CWND = 1024;
    
    if (state == 1) { // SYN
        int SEQ = 0;
        int ACK = 1;
        int WND = CWND;
        TCPHeader to_header(SEQ, ACK, WND, 6);
        vector<char> encoded_header = to_header.encode();
        
        // Send TCP SYN-ACK packet
        while (sendto(sockfd, &encoded_header[0], 8, 0, (struct sockaddr*) &cli_addr, cli_addrlen) < 0) {
            cerr << "ERROR: Failed to send packet" << endl;
        }
        
        // Last seq is the seq we just sent
        LAST_SENT = SEQ;
        cout << "Sending packet "  << SEQ << " " << CWND << " " << SSTHRESH << " Retransmission SYN" << endl;
        
    }
    else if (state == 2) { // ACK
        // Reset file to start reading again from last acked packet
        DATA_INDEX = ACKED_DATA_INDEX;
        fseek(file, DATA_INDEX - 1, SEEK_SET);
        
        // Set up TCP packet to send
        int SEQ = LAST_ACKED + 1;		// SEQ = sequence number of this packet.
        int ACK = 2;                    // ACK = sequence number next sequence number we expect
        int WND = CWND;                  // WND = minimum of rcv_wnd, cwnd, and maxseq+1/2
        
        // Create TCP header
        TCPHeader to_header(SEQ, ACK, WND, 4);
        vector<char> packet = to_header.encode();
        packet.resize(MAX_PKT_LEN);
        
        // Read in payload
        int bytes_read = fread(&packet[8], 1, MSS, file);
       // cout << bytes_read << endl;
        
        if (bytes_read == 0) {
            state = 3;
        	return;
        }
        DATA_INDEX += bytes_read;
        packet.resize(HEADER_LEN + bytes_read);

        // Send TCP packet
        while (sendto(sockfd, &packet[0], packet.size(), 0, (struct sockaddr*) &cli_addr, cli_addrlen) < 0) {
            cerr << "ERROR: Failed to send packet" << endl;
        }
        cout << "Sending packet "  << SEQ << " " << CWND << " " << SSTHRESH << " Retransmission" << endl;
        SEQ = (SEQ + bytes_read) % MAX_SEQ_NUM;
        LAST_SENT = (SEQ - 1 == -1) ? MAX_SEQ_NUM : SEQ-1;
    }
    else if (state == 3){ // FIN
        // Set up TCP packet to send
        int SEQ = LAST_ACKED + 1;		// SEQ = sequence number of this packet.
        int ACK = 1;                    // ACK = sequence number next sequence number we expect
        int WND = CWND;                 // WND = minimum of rcv_wnd, cwnd, and maxseq+1/2
        
        TCPHeader fin_header(SEQ, ACK, WND, 1);
        vector<char> packet = fin_header.encode();
        
        // Send FIN packet
        while (sendto(sockfd, &packet[0], packet.size(), 0, (struct sockaddr*) &cli_addr, cli_addrlen) == -1) {
            cerr << "ERROR: Failed to send packet" << endl;
        }
        cout << "Sending packet "  << SEQ << " " << CWND << " " << SSTHRESH;
        if (sentFIN) {
            cout << " Retransmission FIN" << endl;
        }
        else {
            cout << " FIN" << endl;
        } 
        sentFIN = true;
        
        // Wait for FIN ACK
        do {
            while (recvfrom(sockfd, &packet[0], 8, 0, (struct sockaddr*) &cli_addr, &cli_addrlen) < 0) {
    		    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
        			// Timed out
        			timeout(sockfd, cli_addr, cli_addrlen);
        			return;
        		}
    		    
    		    cerr << "ERROR: Failed to read TCP header" << endl;
    		    break;
    		}
    		fin_header.decode(packet);
        } while (fin_header.getSEQ() != ACK); // Keep recv until get packet with correct SEQ
	    
	    LAST_SENT++;
	    SEQ++;
        ACK++;
	    
	    cout << "Receiving packet " << fin_header.getACK() << endl;
	    cerr << "Received FIN/ACK" << endl;
	    
	    // Last ACK. Flags = 4 = A
	    TCPHeader ack_header(SEQ, ACK, WND, 4);
        packet = ack_header.encode();
        
        // Send TCP packet
        while (sendto(sockfd, &packet[0], packet.size(), 0, (struct sockaddr*) &cli_addr, cli_addrlen) == -1) {
            cerr << "ERROR: Failed to send packet" << endl;
        }
        cout << "Sending packet "  << SEQ << " " << CWND << " " << SSTHRESH << endl;
        cerr << "(final ACK after FIN/ACK packet)" << endl;
        
        cerr << "Closing server after 2 RTO" << endl;
        vector<char> finack_packet(8);
        int num_time = 0;
        while (num_time < 2) {
            // Try to recv something from socket
            if (recvfrom(sockfd, &finack_packet[0], 8, 0, (struct sockaddr*) &cli_addr, &cli_addrlen) < 0) {
        		// If errno set, then time out (one RTO)
        		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
        			// Timed out
        			num_time++;
        			continue;
        		}
            }
    		else {
    		    // Received some packet. If it is FIN/ACK again, then resend last ACK.
    		    // Otherwise ignore it, because could be some lagging unnecessary ACK
    		    ack_header.decode(finack_packet);
    		    if (ack_header.getFlags() == 5) {
        		    while (sendto(sockfd, &packet[0], packet.size(), 0, (struct sockaddr*) &cli_addr, cli_addrlen) == -1) {
                        cerr << "ERROR: Failed to send packet" << endl;
                    }
                    num_time = 0;
    		    }
    		}
        }
        
        cerr << "Closing server now" << endl;
        // Done waiting 2RTO. Exiting.
        close(sockfd);
        fclose(file);
        exit(0);
    }

}

int main(int argc, char *argv[])
{
	// Structs for resolving host names
	struct addrinfo hints;
	struct addrinfo *res_addr;
	struct sockaddr_storage cli_addr;
    socklen_t cli_addrlen = sizeof(struct sockaddr_storage);
    
	// socket IDs
	int yes, sockfd;

	// Invalid number of arguments
	if (argc != 3) {
		usage_msg();
	}

	PORT = argv[1];
	FILENAME = argv[2];

	// Resolve hostname to IP address
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	// Bind to current IP and PORT
	if (getaddrinfo(NULL, PORT, &hints, &res_addr) != 0) {
		cerr << "ERROR: Could not resolve hostname to IP address." << endl;
		return -1;
	}

	// Create socket
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);

	// Allow concurrent connections?
	yes = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		cerr << "ERROR: Could not set socket options" << endl;
		return -1;
	}

	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &TIMEOUT, sizeof(timeval)) == -1) {
		cerr << "ERROR: Could not set socket options" << endl;
		return -1;
	}

	// Bind address to socket
	bind(sockfd, res_addr->ai_addr, res_addr->ai_addrlen);

	// Open the file
	file = fopen(FILENAME.c_str(), "r");
    struct stat st;
    stat(FILENAME.c_str(), &st);
    int file_size = st.st_size;

	if (!file) {
		// File not found
		cerr << "ERROR: Could not open specified file." << endl;
		return -1;
	}
	                    
    vector<char> header_bytes(8);	// Buffer to hold received TCP header
    TCPHeader from_header;			// TCPHeader object to hold received header
    
    gettimeofday(&start, NULL);
    
	while(1) {
	    gettimeofday(&current, NULL);
	    int elapsed_time = ((current.tv_sec - start.tv_sec) * 1000.0) + (current.tv_usec - start.tv_usec) / 1000.0;
	    if (state && (elapsed_time >= 500)) {
	        timeout(sockfd, cli_addr, cli_addrlen);
	        gettimeofday(&start, NULL);
	    }
	    
		// Read TCP packet from socket
		if (recvfrom(sockfd, &header_bytes[0], 8, 0, (struct sockaddr*) &cli_addr, &cli_addrlen) < 0) {
		    if (state != 0) {
    		    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
        			// Timed out
        			timeout(sockfd, cli_addr, cli_addrlen);
        			gettimeofday(&start, NULL);
        			continue;
        		}
    		    cerr << "ERROR: Failed to read TCP header" << endl;
		    }
		    continue;
		}
		else {
			// Decode the TCP header
		    from_header.decode(header_bytes);
		    
		    // flag = 2 means SYN (set up connection)
		    if (from_header.getFlags() == 2) {
		        state = 1;
		        int num_unacked = LAST_SENT - LAST_ACKED; // Number of packets in flight
		        
		        // Set up TCP header
		        // SYN-ACK packet. (ACK = client seq + 1, WND = minimum, flags = 6 = AS)
		        int SEQ = 0; // SEQ = sequence number of this packet. Starts at 0 
		        int ACK = from_header.getSEQ() + 1; // ACK = next expected sequence
		        int WND = min(from_header.getWND() - num_unacked, min(CWND-num_unacked, (MAX_SEQ_NUM+1)/2)-num_unacked);
		       
                TCPHeader to_header(SEQ, ACK, WND, 6);
                vector<char> encoded_header = to_header.encode();
                
                // Send TCP SYN-ACK packet
                if (sendto(sockfd, &encoded_header[0], 8, 0, (struct sockaddr*) &cli_addr, cli_addrlen) < 0) {
                    cerr << "ERROR: Failed to send packet" << endl;
                    continue;
                }
                else {
                    // Last seq is the seq we just sent
                    LAST_SENT = SEQ;
                    cout << "Sending packet "  << SEQ << " " << CWND << " " << SSTHRESH << " SYN" << endl;
                    gettimeofday(&start, NULL);
                }
                
		    }
		    
		    // flag = 4 means ACK
		    else if (from_header.getFlags() == 4) {
		        state = 2;
		        // Updated last acked sequence byte
		        int acked_num = (from_header.getACK()-1 < 0) ? MAX_SEQ_NUM : from_header.getACK()-1;
		        
		        // Greater than last acked. then ok to update
		        if (acked_num > LAST_ACKED) {
		           ACKED_DATA_INDEX += acked_num - LAST_ACKED;
		           LAST_ACKED = acked_num; // ACK number received - 1
	               gettimeofday(&start, NULL);
		        }
		        // If less than, then check if within (max_seq_num+1)/2
		        else if (acked_num < LAST_ACKED && abs(acked_num-LAST_ACKED) > (MAX_SEQ_NUM+1)/2) {
		            ACKED_DATA_INDEX += (MAX_SEQ_NUM - LAST_ACKED) + acked_num;
		            LAST_ACKED = acked_num;
		            gettimeofday(&start, NULL);
		        }
		        int num_unacked = (LAST_SENT - LAST_ACKED + MAX_SEQ_NUM) % MAX_SEQ_NUM; // Number of packets (bytes) in flight
		        // Set up TCP packet to send
		        int SEQ = LAST_SENT + 1;		// SEQ = sequence number of this packet.
		        int ACK = from_header.getSEQ(); // ACK = sequence number next sequence number we expect
		        int WND = min(from_header.getWND()-num_unacked, min(CWND-num_unacked, (MAX_SEQ_NUM+1)/2)-num_unacked);
		        									// WND = minimum of rcv_wnd, cwnd, and maxseq+1/2

		        cout << "Receiving packet " << from_header.getACK() << endl;

                 // Increase window size Slow start
                if (CWND < SSTHRESH) {
		            CWND += MSS;
                }
                 // Congestion avoidance
                else {
                     CWND += MSS*((double)MSS/CWND);
                }

                // Read in and sent whole file. All packets have been ACKed.
                // Send FIN
                if (DATA_INDEX == file_size && num_unacked == 0) {
                    state = 3;
                	// FIN packet. Flags = 1 = F
                    TCPHeader fin_header(SEQ, ACK, WND, 1);
                    vector<char> packet = fin_header.encode();
                    
                    // Send TCP packet
                    if (sendto(sockfd, &packet[0], packet.size(), 0, (struct sockaddr*) &cli_addr, cli_addrlen) == -1) {
                        cerr << "ERROR: Failed to send packet" << endl;
                        continue;
                    }
                    else {
                        sentFIN = true;
                        cout << "Sending packet "  << SEQ << " " << CWND << " " << SSTHRESH << " FIN" << endl;
                        
                        // Get FINACK. Send final ACK
                        // Wait for FIN ACK
                        do {
                            if (recvfrom(sockfd, &packet[0], 8, 0, (struct sockaddr*) &cli_addr, &cli_addrlen) < 0) {
                    		    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                        			// Timed out
                        			timeout(sockfd, cli_addr, cli_addrlen);
                        			continue;
                        		}
                    		    
                    		    cerr << "ERROR: Failed to read TCP header" << endl;
                    		    break;
                    		}
                    		fin_header.decode(packet);
                        } while (fin_header.getSEQ() != ACK); // Keep recv until get packet with correct SEQ
                		
            		    LAST_SENT++;
            		    SEQ++;
            		    ACK++;
            		    
            		    cout << "Receiving packet " << fin_header.getACK() << endl;
            		    cerr << "(FIN/ACK)" << endl;
            		    
            		    // Last ACK. Flags = 4 = A
            		    TCPHeader ack_header(SEQ, ACK, WND, 4);
                        vector<char> packet = ack_header.encode();
                        
                        // Send TCP packet
                        if (sendto(sockfd, &packet[0], packet.size(), 0, (struct sockaddr*) &cli_addr, cli_addrlen) == -1) {
                            cerr << "ERROR: Failed to send packet" << endl;
                            continue;
                        }
                        else {
                            cout << "Sending packet "  << SEQ << " " << CWND << " " << SSTHRESH << endl;
                            cerr << "(Final ACK after FIN/ACK)" << endl;
                            
                            cerr << "Closing server after 2 RTO" << endl;
                            vector<char> finack_packet(8);
                            int num_time = 0;
                            while (num_time < 2) {
                                if (recvfrom(sockfd, &finack_packet[0], 8, 0, (struct sockaddr*) &cli_addr, &cli_addrlen) < 0) {
                            		// If errno set, then time out
                            		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                            			// Timed out
                            			num_time++;
                            			continue;
                            		}
                                }
                        		else {
                        		    // Received some packet. If it is FIN/ACK again, then resend last ACK.
                        		    // Otherwise ignore it, because could be some lagging unnecessary ACK
                        		    ack_header.decode(finack_packet);
                        		    if (ack_header.getFlags() == 5) {
                            		    while (sendto(sockfd, &packet[0], packet.size(), 0, (struct sockaddr*) &cli_addr, cli_addrlen) == -1) {
                                            cerr << "ERROR: Failed to send packet" << endl;
                                        }
                                        num_time = 0;
                        		    }
                        		}
                            }
                            
                            cerr << "Closing server now" << endl;
                            // Done waiting 2RTO. Exiting.
                            close(sockfd);
                            fclose(file);
                            exit(0);
                        }
                		
                    }
                }
                else {
                    // Number of packets to send = WND/1024
                    for(int num_pkts = 0; num_pkts < WND/MSS; num_pkts++) {
                        
                        // Create TCP header
    		            TCPHeader to_header(SEQ, ACK, WND, 4);
                        vector<char> packet = to_header.encode();
                        packet.resize(MAX_PKT_LEN);
                        
                        // Read in payload
                        int bytes_read = fread(&packet[8], 1, MSS, file);
                        
                        if (bytes_read == 0) {
                        	break;
                        }
                        DATA_INDEX += bytes_read;
                        packet.resize(HEADER_LEN + bytes_read);
    
                        // Send TCP packet
                        if (sendto(sockfd, &packet[0], packet.size(), 0, (struct sockaddr*) &cli_addr, cli_addrlen) < 0) {
                            cerr << "ERROR: Failed to send packet" << endl;
                            continue;
                        }
                        else {
                            cout << "Sending packet "  << SEQ << " " << CWND << " " << SSTHRESH << endl;
                            SEQ = (SEQ + bytes_read) % MAX_SEQ_NUM;
                            LAST_SENT = (SEQ - 1 == -1) ? MAX_SEQ_NUM : SEQ-1;
                        }
                    } 
                }  	  
		    }
		}
	}
}