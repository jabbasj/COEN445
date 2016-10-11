// coen445server.cpp : Defines the entry point for the console application.
// Source: http://www.binarytides.com/udp-socket-programming-in-winsock/
//

#include "stdafx.h"

#define BUFLEN 512			//Max length of buffer
#define CLIENTS_MAX_COUNT 5

// My data
int CLIENTS_REGISTERED_COUNT = -1;
std::string MY_ADDRESS = "";
std::string NEXT_ADDRESS = "";
int MY_PORT = -1;						//The port on which to listen for incoming data (server 0)
int NEXT_PORT = -1;						//The port of the next known server

// My functions
void loadServersList();
void loadClientsList();
void closeServer();

// Winsock structures
SOCKET s;
struct sockaddr_in server, si_other;
int slen, recv_len;
char buf[BUFLEN];
WSADATA wsa;

int _tmain(int argc, _TCHAR* argv[])
{
	atexit(closeServer);
	//signal(SIGINT, closeServer);

	slen = sizeof(si_other);

	//Initialise winsock
	printf("\nInitialising Winsock...");
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		printf("Failed. Error Code : %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	printf("Initialised.\n");

	//Create a socket
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
	{
		printf("Could not create socket : %d", WSAGetLastError());
	}
	printf("Socket created.\n");


	printf("Reading server list...\n");
	loadServersList();
	if (MY_PORT < 0 || NEXT_PORT < 0)
	{
		printf("Could not read server list.\n");
		exit(EXIT_FAILURE);
	}
	//return 0; //<- this properly calls "closeServer" and updates the file...??????????????????????

	printf("Reading client list...\n");
	loadClientsList();

	//Prepare the sockaddr_in structure
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(MY_PORT);

	//Bind
	if (bind(s, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
	{
		printf("Bind failed with error code : %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	puts("Bind done");

	/* Timeout code:
	int iTimeout = 1600;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&iTimeout, sizeof(iTimeout));
	*/

	//keep listening for data
	while (1)
	{
		printf("Waiting for data...\n");
		fflush(stdout);

		//clear the buffer by filling null, it might have previously received data
		memset(buf, '\0', BUFLEN);

		//try to receive some data, this is a blocking call
		if ((recv_len = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_other, &slen)) == SOCKET_ERROR)
		{
			printf("recvfrom() failed with error code : %d", WSAGetLastError());
			exit(EXIT_FAILURE);
		}

		std::cout << "testfdsfsdf";

		//print details of the client/peer and the data received
		printf("Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
		printf("Data: %s\n", buf);

		//now reply the client with the same data
		if (sendto(s, buf, recv_len, 0, (struct sockaddr*) &si_other, slen) == SOCKET_ERROR)
		{
			printf("sendto() failed with error code : %d", WSAGetLastError());
			exit(EXIT_FAILURE);
		}
	}

	return 0;
}

/*
Read serverconfig.txt, extract available port and NEXT server's ip:port
Change status from off to on

Assumption: 
	- serverconfig.txt is shared among the servers.
	- file is accessed 1 at a time or an updated copy exists for all servers

serverconfig.txt format:

	name:server0,status:off,ip:127.0.0.1,port:8888
	name:server1,status:off,ip:127.0.0.1,port:8889
	name:server2,status:off,ip:127.0.0.1,port:8890
	name:server3,status:off,ip:127.0.0.1,port:8891
	name:server4,status:off,ip:127.0.0.1,port:8892

*/
void loadServersList() {

	std::ifstream input_file("serverconfig.txt");
	std::ofstream output_file("temp.txt");

	std::string line;
	if (input_file.is_open()) {

		while (getline(input_file, line)) {

			if (MY_PORT < 0) {
				if (line.find("on") != std::string::npos) {
					output_file << line + "\n";
					continue;
				}
				else if (line.find("off") != std::string::npos) {
					// Extract port
					MY_PORT = std::stoi(line.substr(line.find("port:") + std::strlen("port:"), -1));

					MY_ADDRESS = line.substr(line.find("ip:") + std::strlen("ip:"), line.find(",port") - line.find("ip:") - std::strlen("ip:"));

					// Set off to on
					line.replace(line.find("off"), std::strlen("off"), "on");
					output_file << line + "\n";
					
					// Get next server's port
					getline(input_file, line);
					output_file << line + "\n";
					if (line.size() > 0) {
						NEXT_PORT = std::stoi(line.substr(line.find("port:") + std::strlen("port:"), -1));

						NEXT_ADDRESS = line.substr(line.find("ip:") + std::strlen("ip:"), line.find(",port") - line.find("ip:") - std::strlen("ip:"));
					}
					else {
						input_file.clear();
						input_file.seekg(0, std::ios::beg);
						getline(input_file, line);
						NEXT_PORT = std::stoi(line.substr(line.find("port:") + std::strlen("port:"), -1));
						NEXT_ADDRESS = line.substr(line.find("ip:") + std::strlen("ip:"), line.find(",port") - line.find("ip:") - std::strlen("ip:"));
						output_file << line + "\n";
					}
					continue;
				}
			}
			else {
				output_file << line + "\n";
			}
		}

		input_file.close();
		output_file.close();
		// delete the original file
		remove("serverconfig.txt");
		// rename old to new
		rename("temp.txt", "serverconfig.txt");
	}
}


void closeServer() {

	std::cout << "Terminating...\n";

	//fflush(stdout);
	closesocket(s);
	WSACleanup();

	// ACCESS VIOLATION?????
	
	/*
	std::ifstream input_file("serverconfig.txt");
	std::ofstream output_file("temp.txt");

	std::string line;
	if (input_file.is_open()) {

		while (getline(input_file, line)) {

			if (MY_PORT == std::stoi(line.substr(line.find("port:") + std::strlen("port:"), -1))) {
				line.replace(line.find("on"), std::strlen("on"), "off");
			}
			output_file << line + "\n";
		}

		input_file.close();
		output_file.close();
		// delete the original file
		std::remove("serverconfig.txt");
		// rename old to new
		std::rename("temp.txt", "serverconfig.txt");
	}*/

	// Still doesn't work WTF?!  cause: recvfrom?, but why is this even a problem? How to handle ???
	
	FILE *input_file;
	FILE *output_file;

	fopen_s(&input_file,"serverconfig.txt", "r");
	fopen_s(&output_file, "temp.txt", "w");

	if (input_file && output_file) {

		char line[80];
		while (fgets(line, 80,input_file) != NULL) {

			if (line[0] == '\n') {
				break;
			}

			//printf("%s\n", line);
			std::string line_str(line);
			
			if (line_str.find("port:") != std::string::npos) {

				std::string port = line_str.substr(line_str.find("port:") + std::strlen("port:"), -1);
				port.pop_back();

				if (MY_PORT == std::stoi(port)) {

					if (line_str.find("on") != std::string::npos) {
						line_str.replace(line_str.find("on"), std::strlen("on"), "off");
					}
				}

				fwrite(line_str.c_str(), sizeof(line_str.c_str()[0]), strlen(line_str.c_str()), output_file);
				//fclose(output_file); //<- this properly saves the first line in the file... why is the loop not working in atexit?
			}
		}
	}

	fclose(output_file);
	fclose(input_file);

	std::remove("serverconfig.txt");
	std::rename("temp.txt", "serverconfig.txt");

	exit(1);
}

void loadClientsList() {

}