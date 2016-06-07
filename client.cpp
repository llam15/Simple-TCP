
#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <iterator>
#include <ctime>

#include <time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>

#include "TCP.h"

using namespace std;

const timeval TIMEOUT{0, 500000};

char* SERVER_IP;
int PORT;

int CURRENT_SEQ = -1;     // Last byte sent
int NEXT_EXPECTED = -1;    // Last byte ACKed
int FINSTART = -1;

const int MAX_PKT_LEN = 1032; //Max packet = 1032. Max payload = 1024
const int MAX_SEQ_NUM = 30720;
const int MAX_WIN = 15360;

void usage_msg()
{
    cerr << "usage: client [SERVER-HOST-OR-IP] [PORT-NUMBER]" << endl;
    exit(0);
}

int main(int argc, char *argv[])
{
    // Invalid number of arguments
    if (argc != 3) {
        usage_msg();
    }
    
    SERVER_IP = argv[1];
    PORT = atoi(argv[2]);
    char* FILENAME = (char*) "received.data";
    
    struct sockaddr_in servaddr;    /* server address */
    socklen_t servsize = sizeof(servaddr);
    
    /* fill in the server's address and data */
    memset((char*)&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &servaddr.sin_addr);
    
    int sockfd;
    
    if ((sockfd = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
        cerr << "ERROR: Could not create socket" << endl;
        return -1;
    }
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &TIMEOUT, sizeof(timeval)) == -1) {
        cerr << "ERROR: Could not set socket options" << endl;
        return -1;
    }
    
    TCPHeader synHeader(0, 0, MAX_WIN, 2);
    TCPHeader fromHeader;
    CURRENT_SEQ = 0;
    vector<char> encoded_header = synHeader.encode();
    vector<char> payload(MAX_PKT_LEN);
    
    bool connected = false;
    bool retransmit = false;
    
    //SEND SYN
    if (sendto(sockfd, &encoded_header[0], encoded_header.size(), 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        cerr << "ERROR: Failed to send packet" << endl;
        return -1;
    }
    cout << "Sending packet " << CURRENT_SEQ << " SYN" << endl;
    CURRENT_SEQ = 1;
    
    remove(FILENAME);
    
    //Create output stream to filename specified by FILENAME
    ofstream output_file(FILENAME);
    
    //Outputs the contents of the vector to the file
    ostream_iterator<char> it(output_file);
    
    
    struct timeval start, current;
    gettimeofday(&start, NULL);
    
    while(1) {
        gettimeofday(&current, NULL);
        int bytes_read;
        payload.clear();
        payload.resize(MAX_PKT_LEN);
        
        // Check if time out after 500 ms
        if (!connected && (((current.tv_sec - start.tv_sec) * 1000.0) + ((current.tv_usec - start.tv_usec) / 1000.0)) >= 500) {
            if (FINSTART == 1) {
                TCPHeader toHeader(CURRENT_SEQ, NEXT_EXPECTED, MAX_WIN, 5);
                encoded_header.clear();
                encoded_header = toHeader.encode();
                
                if (sendto(sockfd, &encoded_header[0], encoded_header.size(), 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
                    cerr <<"sendto failed" << endl;
                    return -1;
                }
                cout << "Sending packet " << NEXT_EXPECTED << " Retransmission" << endl;
            }
            else {
                if (sendto(sockfd, &encoded_header[0], encoded_header.size(), 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
                    cerr <<"sendto failed" << endl;
                    return -1;
                }
                cout << "Sending packet 0 Retransmission" << endl;
            }
            gettimeofday(&start, NULL);
        }
        
        //RECV TCP packet
        if ((bytes_read = recvfrom(sockfd, &payload[0], MAX_PKT_LEN, 0, (struct sockaddr *)&servaddr, &servsize)) < 0) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                // Timed out
                continue;
            }
        }
        else {
            if (bytes_read > 0) {
                // Get TCP header
                fromHeader.decode(payload);
                
                // Flag = 6 = AS (SYN ACK)
                if (fromHeader.getFlags() == 6) {
                    connected = true;
                    cout << "Receiving packet " << fromHeader.getSEQ() << endl;
                    
                    // Next expected SEQ number
                    NEXT_EXPECTED = (fromHeader.getSEQ() + 1) % MAX_SEQ_NUM;
                    
                    // Create TCP header
                    TCPHeader toHeader(CURRENT_SEQ, NEXT_EXPECTED, MAX_WIN, 4);
                    encoded_header.clear();
                    payload.clear();
                    payload.resize(MAX_PKT_LEN);
                    encoded_header = toHeader.encode();
                    
                    //send final ACK handshake
                    if (sendto(sockfd, &encoded_header[0], encoded_header.size(), 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
                        cerr << "ERROR: Failed to send packet" << endl;
                        return -1;
                    }
                    if (retransmit == false) {
                        cout << "Sending packet " << NEXT_EXPECTED << endl;
                    }
                    else {
                        cout << "Sending packet " << NEXT_EXPECTED << " Retransmission" << endl;
                    }
                    
                }
                
                // Flag = 1 = F (FIN)
                else if (fromHeader.getFlags() == 1) {
                    cout << "Receiving packet " << fromHeader.getSEQ() << endl;
                    
                    NEXT_EXPECTED = (fromHeader.getSEQ() + 1) % MAX_SEQ_NUM;
                    
                    TCPHeader toHeader(CURRENT_SEQ, NEXT_EXPECTED, MAX_WIN, 5);
                    encoded_header.clear();
                    encoded_header = toHeader.encode();
                    
                    //send FIN-ACK
                    if (sendto(sockfd, &encoded_header[0], encoded_header.size(), 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
                        cerr << "ERROR: Failed to send packet" << endl;
                        return -1;
                    }
                    cout << "Sending packet " << NEXT_EXPECTED << " FIN" << endl;
                    FINSTART = 1; // Waiting for final ACK from server
                    gettimeofday(&start, NULL);
                    connected = false;
                    
                    
                }
                
                // If waiting for final ACK and received ACK with correct sequence number, then done
                else if (FINSTART == 1) {
                    cout << "Receiving packet " << fromHeader.getSEQ() << endl;
                    if (fromHeader.getSEQ() == NEXT_EXPECTED) {
                        break;
                    }
                }
                
                // Receiving normal data packet
                else {
                    cout << "Receiving packet " << fromHeader.getSEQ() << endl;
                    retransmit = false;
                    if (fromHeader.getSEQ() == NEXT_EXPECTED) {
                        NEXT_EXPECTED = (NEXT_EXPECTED + bytes_read - 8) % MAX_SEQ_NUM;
                        it = copy(payload.begin() + 8, payload.begin() + bytes_read, it);
                    }
                    else if (fromHeader.getSEQ() < NEXT_EXPECTED && abs(NEXT_EXPECTED-fromHeader.getSEQ()) < (MAX_SEQ_NUM+1)/2) {
                        retransmit = true;
                    }
                    
                    TCPHeader toHeader(CURRENT_SEQ, NEXT_EXPECTED, MAX_WIN, 4);
                    encoded_header.clear();
                    encoded_header = toHeader.encode();
                    
                    //send ACK
                    if (sendto(sockfd, &encoded_header[0], encoded_header.size(), 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
                        cerr <<"sendto failed" << endl;
                        return -1;
                    }
                    if (retransmit == false) {
                        cout << "Sending packet " << NEXT_EXPECTED << endl;
                    }
                    else {
                        cout << "Sending packet " << NEXT_EXPECTED << " Retransmission" << endl;
                    }
                }
            }
        }
    }
    
    close(sockfd);
    output_file.close();
    return 0;
}