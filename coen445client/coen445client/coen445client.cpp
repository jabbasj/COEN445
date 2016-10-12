// coen445client.cpp : Defines the entry point for the console application.
// Source: http://www.binarytides.com/udp-socket-programming-in-winsock/
//

#include "stdafx.h"


#define BUFLEN 512  //Max length of buffer

struct my_MSG
{
	int port;
	std::string addr;
	std::string name;
	std::string message;
};

#define PACKETSIZE sizeof(my_MSG)

void printMsg(my_MSG* MSGPacket);

std::string SERVER_ADDRESS = "";
int SERVER_PORT = -1;

// My functions
void loadServersList();
void initializeConnection();
void communicationHandler();

struct sockaddr_in si_other;
int s, slen = sizeof(si_other);
char buf[BUFLEN];
char message[BUFLEN];
WSADATA wsa;

int _tmain(int argc, _TCHAR* argv[])
{
	try {
		initializeConnection(); // Initialize winsock, create socket, load server list, 

		communicationHandler();
	}
	catch (std::exception e) {
		printf(e.what());
	}

	closesocket(s);
	WSACleanup();

	system("pause");

	return 0;
}


void communicationHandler() {

	int count = 0;

	my_MSG msgPacket;

	//start communication
	while (1)
	{
		//TODO: add new name, registration requests, etc...
		//printf("Enter message : ");
		//gets_s(message);

		//clear the buffer by filling null, it might have previously received data
		memset(buf, '\0', BUFLEN);

		msgPacket.message = std::to_string(count++).c_str();
		msgPacket.name = "me";
		msgPacket.addr = "my_addr";
		msgPacket.port = 8888;

		//send the message
		if (sendto(s, (char*)&msgPacket, BUFLEN, 0, (struct sockaddr *) &si_other, slen) == SOCKET_ERROR)
		{
			printf("sendto() failed with error code : %d", WSAGetLastError());
			exit(EXIT_FAILURE);
		}	


		//try to receive some data, this is a blocking call
		if (recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_other, &slen) == SOCKET_ERROR)
		{
			printf("recvfrom() failed with error code : %d", WSAGetLastError());
			exit(EXIT_FAILURE);
		}

		//receive a reply and print it
		//print details of the client/peer and the data received
		my_MSG received_packet; //Re-make the struct
		memcpy(&received_packet, buf, sizeof(received_packet));
		printf("Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
		printMsg(&received_packet);
	}

}

void initializeConnection() {

	//Initialise winsock
	printf("\nInitialising Winsock...");
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		printf("Failed. Error Code : %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	printf("Initialised.\n");

	//create socket
	if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == SOCKET_ERROR)
	{
		printf("socket() failed with error code : %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}

	//load server addr/port
	loadServersList();
	if (SERVER_ADDRESS == "" || SERVER_PORT == -1) {
		printf("Error reading server addr/port\n");
		exit(EXIT_FAILURE);
	}

	//setup address structure
	memset((char *)&si_other, 0, sizeof(si_other));
	si_other.sin_family = AF_INET;
	si_other.sin_port = htons(SERVER_PORT);
	si_other.sin_addr.S_un.S_addr = inet_addr(SERVER_ADDRESS.c_str());
}


void loadServersList() {

	std::ifstream input_file("serverconfig.txt");

	std::string line;
	if (input_file.is_open()) {

		getline(input_file, line);

		SERVER_PORT = std::stoi(line.substr(line.find("port:") + std::strlen("port:"), -1));
		SERVER_ADDRESS = line.substr(line.find("ip:") + std::strlen("ip:"), line.find(",port") - line.find("ip:") - std::strlen("ip:"));

		input_file.close();
	}
}

void printMsg(my_MSG* msgPacket)
{
	std::cout << msgPacket->port << std::endl;
	std::cout << msgPacket->addr << std::endl;
	std::cout << msgPacket->name << std::endl;
	std::cout << msgPacket->message << std::endl;
}