#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <string>
#include <iostream>
#include <poll.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <array>
#include "socket.h"

#define DEBUG
#define CHECKSUM

using namespace std;
using namespace chrono;

// Functions
void parseFlags(int argc, char* argv[]);
void runAsClient();
void runAsServer();
void clientTimeout(int client_socket, char* packet, char* expectedSeqNum);
void promptForValues();
void promptForTimeout();
void resetPacket(char* packet);
string calcChecksum(char* dataSection);

// Global variables
char flags = 0;
const char HELP = 1;
const char RUN_AS_SERVER = 1<<1;
const char SERVER_HOST = 1<<2;

char recMessage[64] = "";
int numRetrans = 0;
char sendAddr[8] = "";
char recAddr[8] = "";
char seqNum[8] = "";
int sizeOfPreData = sizeof(sendAddr) + sizeof(recAddr) + sizeof(seqNum);
int sizeOfData = 0;
int sizeOfPostData = 16;
int sizeOfPacket = 0;
int rangeOfSeqNumMin = 0;
int rangeOfSeqNumMax = 0;
char serverHost[100] = "localhost"; // Default
int portno = 9090;
char filename[100] = "";
unsigned long filesize = 0;
double timeoutLength = 0;
int timeoutScaleFactor = 0;

int main(int argc, char* argv[]) {
	// Parse flags
	parseFlags(argc, argv);

	// Run as Client or Server
	(flags & RUN_AS_SERVER) ? runAsServer() : runAsClient();
}

void parseFlags(int argc, char* argv[]) {
	// For getopt
	extern char* optarg;
	extern int optind;
	int c, err = 0;

	// Useage if command is entered incorrectly
	char usage[] = "usage: %s [-hr] [-s host] filename\n";

	// Parse flags
	while ((c = getopt(argc, argv, "hrs:")) != -1) {
		switch (c) {
			case 'h':
				flags += HELP;
				break;
			case 'r':
				flags += RUN_AS_SERVER;
				break;
			case 's':
				flags += SERVER_HOST;
				strcpy(serverHost, optarg);
				break;
			case '?':
				err = 1;
				break;
		}
	}

	// If there were invalid flags, inform the user and die
	if (err) {
		fprintf(stderr, usage, argv[0]);
		exit(1);
	}

	// If the user asked for help, print help screen and die
	if (flags & HELP) {
		printf("project [OPTIONS] filename\n");
		printf("\t-h\t\tPrint this help screen\n");
		printf("\t-r\t\tRun as server\n");
		printf("\t-s host\t\tSet server host\n");
		exit(0);
	}

	// Get filename
	if (argc >= 1 && optind < argc) {
		strcpy(filename, argv[optind]);
	} else {
		fprintf(stderr, usage, argv[0]);
		exit(1);
	}
}

void runAsClient() {
	printf("Running as Client\n------------------\n\n");

	// Prompt for values
	promptForValues();
	promptForTimeout();

	// Open socket and connect to server
	int client_socket = callServer(serverHost, portno);
	
	// Read from input file
	FILE* file = fopen(filename, "r");

	// Find size of file
	fseek(file, 0L, SEEK_END);
	unsigned long filesize = ftell(file);
	rewind(file);

	if (sizeOfData > filesize) {
		sizeOfData = filesize;
	}
	sizeOfPacket = sizeOfPreData + sizeOfData + sizeOfPostData;

	// Send size of file
	writeLong(filesize, client_socket);
	write(client_socket, &timeoutLength, sizeof(timeoutLength));
	
	// Setup
	char packet[sizeOfPacket];
	char expectedSeqNum[8] = {'0','0','0','0','0','0','0','0'};
	char dataSection[sizeOfData];
	string checksum = "";
	unsigned long remainingBytesToSend = filesize;
	int totalNumPackets = 0;
	auto startTime = high_resolution_clock::now();	
	memset(&packet, 0, sizeOfPacket);

	// Progress bar
	unsigned long fivePercent = remainingBytesToSend / 20;
	int fivePercentCounter = 1;
#ifndef DEBUG
	printf("0..");fflush(stdout);
#endif

	// First packet
	fread(dataSection, 1, sizeOfData, file);
	resetPacket(packet);
	strncat(packet, dataSection, sizeOfData);
	packet[23] = expectedSeqNum[7];
	checksum = calcChecksum(dataSection);
	strncat(packet, checksum.c_str(), checksum.length());
	write(client_socket, packet, sizeOfPacket);
	auto sendTime = high_resolution_clock::now();
	auto sendDur = 0;
#ifdef DEBUG
	printf("Packet 0 sent.\n");
#endif
	clientTimeout(client_socket, packet, expectedSeqNum);
	memset(&packet, 0, sizeOfPacket);
	memset(&dataSection, 0, sizeOfData);
	remainingBytesToSend -= sizeOfData;
	totalNumPackets++;

	// Send remaining packets	
	while (remainingBytesToSend > sizeOfData) {
		// Check if ack is correct
		if (recMessage[4] == expectedSeqNum[7]) {
			sendDur = duration<double,std::milli>(high_resolution_clock::now() - sendTime).count();
		#ifdef DEBUG
			printf("%s (RTT = %lf)\n\n", recMessage, sendDur);
		#endif
		
			if (expectedSeqNum[7] == '0') {
				expectedSeqNum[7] = '1';
			} else {
				expectedSeqNum[7] = '0';
			}

			fread(dataSection, 1, sizeOfData, file);
			resetPacket(packet);
			strncat(packet, dataSection, sizeOfData);
			packet[23] = expectedSeqNum[7];
			checksum = calcChecksum(dataSection);
			strncat(packet, checksum.c_str(), checksum.length());

		#ifdef CHECKSUM
			// Flip some data bits
			if (totalNumPackets % (filesize/20) == 0) { // Flip every 5 packets
				packet[sizeOfPreData+1] = (packet[sizeOfPreData+1] + 1); // Set 2nd data char to +1 ASCII
			}
		#endif

			write(client_socket, packet, sizeOfPacket);

		#ifdef DEBUG
			printf("Packet %c sent.\n", expectedSeqNum[7]);
		#endif

			sendTime = high_resolution_clock::now();
			clientTimeout(client_socket, packet, expectedSeqNum);
			memset(&packet, 0, sizeOfPacket);
			memset(&dataSection, 0, sizeOfData);
			remainingBytesToSend -= sizeOfData;		
			totalNumPackets++;

		#ifndef DEBUG
			// Progress bar
			while ((filesize - remainingBytesToSend) > (fivePercent * fivePercentCounter) && (fivePercentCounter < 20)) {
				printf("%d..", (5*fivePercentCounter));fflush(stdout);
				fivePercentCounter++;
			}
		#endif
		} else {
			printf("Timeout: Sequence Number\n");
			exit(0);
		}
	}

	// Last packet
	if (recMessage[4] == expectedSeqNum[7]) {
		sendDur = duration<double,std::milli>(high_resolution_clock::now() - sendTime).count();
	#ifdef DEBUG
		printf("%s (RTT = %lf)\n\n", recMessage, sendDur);
	#endif

		if (expectedSeqNum[7] == '0') {
			expectedSeqNum[7] = '1';
		} else {
			expectedSeqNum[7] = '0';
		}

		memset(&packet, 0, sizeOfPacket);
		memset(&dataSection, 0, sizeOfData);
		fread(dataSection, 1, remainingBytesToSend, file);
		resetPacket(packet);
		strncat(packet, dataSection, sizeOfData);
		packet[23] = expectedSeqNum[7];
		checksum = calcChecksum(dataSection);
		strncat(packet, checksum.c_str(), checksum.length());
		write(client_socket, packet, sizeOfPacket);
		sendTime = high_resolution_clock::now();
	#ifdef DEBUG
		printf("Packet %c sent.\n", expectedSeqNum[7]);
	#endif
		clientTimeout(client_socket, packet, expectedSeqNum);

		totalNumPackets++;
	}

	sendDur = duration<double,std::milli>(high_resolution_clock::now() - sendTime).count();
#ifdef DEBUG
	printf("%s (RTT = %lf)\n\n", recMessage, sendDur);
#endif

	// Finished sending packets
#ifndef DEBUG
	printf("100\n\n");
#endif
	auto durationTime = (duration<double, std::milli>(high_resolution_clock::now() - startTime).count()) / 1000;

	// Clean resources
	fclose(file);
	close(client_socket);

	// Print stats
	printf("------------------\n");
	printf("File size: %lu\n", filesize);
	printf("Packet size: %d\n", sizeOfPacket);
	printf("Number of packets sent successfully: %d\n", totalNumPackets);
	printf("Number of packets retransmitted: %d\n", numRetrans);
	printf("Total file data sent: %d\n", sizeOfData * totalNumPackets);
	printf("Total packet data sent: %d\n", sizeOfPacket * totalNumPackets);
	printf("Total elapsed time: %f\n", durationTime);
	printf("Throughput: %f\n", sizeOfPacket / durationTime);
	char command[100] = "md5sum ";
	strcat(command, filename);
	printf("md5sum:\n");
	system(command);
	printf("\n");
}

void runAsServer() {
	printf("Running as Server\n------------------\n\n");

	// Prompt for values
	promptForValues();

	// Open socket and wait
	int server_socket = setupServerSocket(portno);
	int client_socket = serverSocketAccept(server_socket);

	// File prep
	FILE* file = fopen(filename, "w");

	// Set up buffer
	char packet[sizeOfPacket];
	memset(&packet, 0, sizeOfPacket);
	unsigned long remainingBytesToReceive = readLong(client_socket);
	read(client_socket, &timeoutLength, sizeof(timeoutLength));
	printf("Timeout: %lf ms\n\n", timeoutLength);
	char packetData[sizeOfData];
	char* packetChecksum = new char [sizeOfPostData];
	string checksum;
	unsigned long clientFilesize = remainingBytesToReceive;

	if (sizeOfData > remainingBytesToReceive) {
		sizeOfData = remainingBytesToReceive;
	}
	sizeOfPacket = sizeOfPreData + sizeOfData + sizeOfPostData;

	// Progress bar
	unsigned long fivePercent = remainingBytesToReceive / 20;
	int fivePercentCounter = 1;
#ifndef DEBUG
	printf("Receiving...\n\n");fflush(stdout);
#endif

	// Read from client until there is no more to read
	auto startTime = high_resolution_clock::now();

	int totalNumPackets = 0;
	char expectedSeqNum[8] = {'0','0','0','0','0','0','0','0'};
	while (read(client_socket, packet, sizeOfPacket)) {
	#ifdef DEBUG
		printf("Expected seq num: %c.\n", expectedSeqNum[7]);
		printf("Packet %c received.\n", packet[23]);
	#endif

		// Check packet sequence number
		if (packet[23] == expectedSeqNum[7]) {
			// Checksum
			strncpy(packetChecksum, packet + (sizeOfPreData + sizeOfData), 16);
			strncpy(packetData, packet + sizeOfPreData, sizeOfData);
			checksum = calcChecksum(packetData);

			// Checksum fails
			if ((strcmp(packetChecksum, checksum.c_str()) != 0) && (strcmp(packetChecksum, "") != 0)) {
			#ifdef DEBUG
				printf("Packet %c checksum failed.\n\n", packet[23]);
			#endif
			} else {
				// For writing to out			
				int i = sizeOfPreData;
				int sizeToWrite = 0;
				
				while ((packet[i] != 0) && (i < sizeOfPacket - sizeOfPostData)) {
					sizeToWrite++;
					i++;
				}
				
				// Handles last packet
				if (i < sizeOfPacket - sizeOfPostData) {
					sizeToWrite -= sizeOfPostData;
				}
	
				fwrite(&packet[sizeOfPreData], 1, sizeToWrite, file);
	
				memset(&packet, 0, sizeOfPacket);
				totalNumPackets++;
				remainingBytesToReceive -= sizeOfPacket;
	
				if (expectedSeqNum[7] == '0') {
				#ifdef DEBUG
					printf("Ack 0 sent.\n\n");
				#endif
					char message[64] = "Ack 0 Received.";
					write(client_socket, message, sizeof(message));
					expectedSeqNum[7] = '1';
				} else {
				#ifdef DEBUG
					printf("Ack 1 sent.\n\n");
				#endif
					char message[64] = "Ack 1 Received.";
					write(client_socket, message, sizeof(message));
					expectedSeqNum[7] = '0';
				}
			}
		} else {
			printf("Timeout: Sequence Number\n");
		}
	}

	// Finished receiving packets
	auto durationTime = (duration<double, std::milli>(high_resolution_clock::now() - startTime).count()) / 1000;

	// Clean resources
	fclose(file);
	close(client_socket);

	// Print stats
	printf("------------------\n");
	printf("Client file size: %lu\n", clientFilesize);
	printf("Packet size received: %d\n", sizeOfPacket);
	printf("Number of packets received successfully: %d\n", totalNumPackets);
	printf("Total file data received: %d\n", sizeOfData * totalNumPackets);
	printf("Total packet data received: %d\n", sizeOfPacket * totalNumPackets);
	printf("Total elapsed time: %f\n", durationTime);
	char command[100] = "md5sum ";
	strcat(command, filename);
	printf("md5sum:\n");
	system(command);
	printf("\n");
}

void clientTimeout(int client_socket, char* packet, char* expectedSeqNum) {
	struct pollfd fd;
	int ret;
	fd.fd = client_socket;
	fd.events = POLLIN;
	ret = poll(&fd, 1, timeoutLength);

	switch (ret) {
		case -1:
			break;
		case 0:
		#ifdef DEBUG
			printf("Packet %c timed out.\n\n", packet[23]);
		#endif
		#ifdef CHECKSUM
			packet[sizeOfPreData+1] = (packet[sizeOfPreData+1] - 1);
		#endif

			write(client_socket, packet, sizeOfPacket);

		#ifdef DEBUG
			printf("Packet %c re-transmitted.\n", expectedSeqNum[7]);
		#endif

			clientTimeout(client_socket, packet, expectedSeqNum);
			numRetrans++;
			break;
		default:
			read(client_socket, recMessage, sizeof(recMessage));
			break;
	}
}

void promptForValues() {
	printf("Enter value for size of packet: ");
	scanf("%d", &sizeOfPacket);
	sizeOfData = sizeOfPacket - sizeOfPreData - sizeOfPostData;

	printf("Enter MIN value for the Range of Sequence Numbers: ");
	scanf("%d", &rangeOfSeqNumMin);

	printf("Enter MAX value for the Range of Sequence Numbers: ");
	scanf("%d", &rangeOfSeqNumMax);
	printf("\n");
}

void promptForTimeout() {
	int timeoutChoice;
	printf("Type '1' for timeout, or '2' for scale factor: ");
	scanf("%d", &timeoutChoice);

	if (timeoutChoice == 1) {
		printf("Enter timeout length in ms: ");
		scanf("%lf", &timeoutLength);
	} else if (timeoutChoice == 2) {
		printf("Enter Scale Factor: ");
		scanf("%d", &timeoutScaleFactor);

		// Ping 3 times to get average round trip time
		printf("\nPinging '%s'\n", serverHost);

		double pingAverage = 0;
		char command[100] = "ping -c 3 ";
		strcat(command, serverHost);
		
		std::array<char, 128> buffer;
		string result;
		std::shared_ptr<FILE> pipe(popen(command, "r"), pclose);
		if (!pipe) throw std::runtime_error("ERROR: popen() failed!");
		while (!feof(pipe.get())) {
			if (fgets(buffer.data(), 128, pipe.get()) != NULL)
			result += buffer.data();
		}
		
		pingAverage = stof(result.substr(result.length()-21, 5));
		timeoutLength = timeoutScaleFactor * (pingAverage);

		printf("Timeout: %lf ms\n", timeoutLength);
	} else {
		printf("Invalid value\n");
		promptForValues();
	}

	printf("\n");
}

void resetPacket(char* packet) {
	strncat(packet, "00000000", sizeof("00000000"));
	strncat(packet, "00000000", sizeof("00000000"));
	strncat(packet, "00000000", sizeof("00000000"));
}

string calcChecksum(char* dataSection) {
	char section[64000]; // Max packet size
	memcpy(section, dataSection, sizeOfData);

	// Create command
	char command[100] = "echo '";
	strcat(command, section);
	strcat(command, "' | cksum");

	// Run command and store output into cksumResult
	std::array<char, 128> buffer;
	string cksumResult;
	std::shared_ptr<FILE> pipe(popen(command, "r"), pclose);
	if (!pipe) throw std::runtime_error("ERROR: popen() failed!");
	while (!feof(pipe.get())) {
		if (fgets(buffer.data(), 128, pipe.get()) != NULL)
		cksumResult += buffer.data();
	}

	while (cksumResult.size() < 16) {
		cksumResult += "0";
	}

	return cksumResult;
}
