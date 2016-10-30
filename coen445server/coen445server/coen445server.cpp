#include "allheaders.h"
#include "protocol.h"

#define BUFLEN 512

void printMsg(my_MSG* MSGPacket);
bool finished = false;

server_status my_status;				//my addr/port, next addr/port, clients registered & online
protocol * server_manager;

std::vector<my_MSG> messages_to_send;
std::vector<my_MSG> messages_received;	//to do, cleanup old messages
std::vector<my_MSG> temp;
std::mutex			mut_send;			//mutex for temp (which is a holder for messages_to_send
std::mutex			mut_recv;			//mutex for messages_received 
std::mutex			mut_clients;		//mutex for clients registered

// My functions
void initializeConnection();
void loadServersList();
void closeServer();
void loadClientsData();
void registerClient(my_MSG data);
void updateClientsData(my_MSG data);
void registrationHandler(my_MSG recived_packet);
void clientHandler(sockaddr_in sockaddr, my_MSG MSG);
void send(my_MSG msg);
void sender();
void listener();

// Winsock structures
SOCKET s;
struct sockaddr_in server, si_send, si_recv;
int slen, recv_len;
char buf[BUFLEN];
WSADATA wsa;

BOOL WINAPI ConsoleCtrlEventHandler(DWORD dwCtrlType);

int _tmain(int argc, _TCHAR* argv[])
{
	SetConsoleCtrlHandler(&ConsoleCtrlEventHandler, TRUE);

	try {
		initializeConnection();
		std::async(sender);
		listener();

	}
	catch (std::exception e) {
		printf(e.what());
		closeServer();
	}
	return 0;
}


//sends all messages in temp vector, todo: change name temp
void sender() {

	while (1)
	{	
		if (temp.size() > 0) {
			printf("Sending (%d) pending messages...\n", temp.size());

			mut_send.lock();
			messages_to_send = temp;
			temp.clear();
			mut_send.unlock();
		}
		while (messages_to_send.size() > 0) {

			my_MSG msg_to_send = messages_to_send.front();

			//reply to sender
			std::wstring stemp = std::wstring(msg_to_send.addr.begin(), msg_to_send.addr.end());
			LPCWSTR sw = stemp.c_str();

			InetPton(AF_INET, sw, &si_send.sin_addr);
			si_send.sin_addr.S_un.S_addr = inet_addr(msg_to_send.addr.c_str());
			si_send.sin_port = htons(msg_to_send.port);

			msg_to_send.addr = my_status.MY_ADDRESS;
			msg_to_send.port = my_status.MY_PORT;

			if (sendto(s, (char*)&msg_to_send, BUFLEN, 0, (struct sockaddr*) &si_send, slen) == SOCKET_ERROR)
			{
				printf("sendto() failed with error code : %d\n", WSAGetLastError());
			}
			messages_to_send.erase(messages_to_send.begin());
			
		}

	}

}


//listens to all messages
//pushes them to messages_received if they're needed by client handler
//calls registerationHandler
void listener() {

	server_manager = new protocol(&my_status);

	while (1) {
		std::cout << "Thread {" << my_status.MY_NAME << "} waiting for data...\n";

		//clear the buffer by filling null, it might have previously received data
		memset(buf, '\0', BUFLEN);

		//try to receive some data, timeouts after 2 seconds
		if ((recv_len = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_recv, &slen)) == SOCKET_ERROR)
		{
			printf("recvfrom() failed with error code : %d\n", WSAGetLastError());
		}

		if (recv_len > 0) {
			
			my_MSG received_packet; //Re-make the struct
			memcpy(&received_packet, buf, sizeof(received_packet));
			received_packet.addr = inet_ntoa(*(struct in_addr *)&si_recv.sin_addr); //use ip from packet instead of given one because external Ip not working on local machine
			
			printf("\nReceived packet from %s:%d\n", inet_ntoa(si_recv.sin_addr), ntohs(si_recv.sin_port));
			printMsg(&received_packet);

			registrationHandler(received_packet);

		}
	}
}


//handles the packet first, does server/registration queries, creates client handler if needed
void registrationHandler(my_MSG received_packet) {

	bool registered_client = false;
	int i = 0;
	mut_clients.lock();
	for (; i < my_status.clients_registered.size(); i++){
		if (received_packet.name == my_status.clients_registered[i].name) {
			registered_client = true;
			break;
		}
	}

	if (received_packet.type == "REGISTER"
		&& !registered_client
		&& my_status.clients_registered.size() >= 5)
	{
		my_MSG denied = server_manager->deny_register(received_packet);
		send(denied);
		mut_clients.unlock();
		return;
	}

	if (registered_client && received_packet.SERVER_MSG != 1) {
		bool client_online = false;
		int j = 0;
		for (; j < my_status.clients_online.size(); j++) {
			if (received_packet.name == my_status.clients_online[j].name) {
				client_online = true;
				break;
			}
		}

		if (!client_online) {

			my_status.clients_online.push_back(my_status.clients_registered[i]);
			std::cout << "Launching {" << received_packet.name << "} thread.\n";
			std::async(clientHandler, si_recv, received_packet);
		}

		mut_clients.unlock();

		mut_recv.lock();
		messages_received.push_back(received_packet);
		mut_recv.unlock();

	}
	else {		
		mut_clients.unlock();

		if (received_packet.type == "REGISTER") {
			//check other servers
			my_MSG registered = server_manager->is_registered_query(received_packet);

			send(registered);
		}

		if (received_packet.type == "IS-REGISTERED") {

			mut_clients.lock();
			my_MSG is_registered = server_manager->is_registered_query_answer(received_packet);
			mut_clients.unlock();

			if (is_registered.type == "REGISTERED") {
				registerClient(is_registered);
			}

			send(is_registered);
		}
		
	}
}


//TODO: save client data to file
//Thread that handles messages from the given client
//Dies after 1 min of idle time
void clientHandler(sockaddr_in sockaddr, my_MSG first_msg) {

	protocol * protocol_manager = new protocol();

	my_MSG recv_msg;
	my_MSG * to_send;
	auto start = std::chrono::system_clock::now();

	while (1) {

		bool new_msg = false;

		mut_recv.lock();
		std::vector<my_MSG>::iterator i = messages_received.begin();
		while (i != messages_received.end())
		{
			if (i->name == first_msg.name) {
				recv_msg = *i;
				messages_received.erase(i);
				new_msg = true;
				break;
			}
			++i;
		}
		mut_recv.unlock();


		if (new_msg) {
			//handle new msg
			start = std::chrono::system_clock::now();

			to_send = new my_MSG(recv_msg);
			to_send->type = "ECHO";
			to_send->addr = inet_ntoa(*(struct in_addr *)&sockaddr.sin_addr);
			//to_send->addr = recv_msg.addr; //Why doesn't external IP work?
			to_send->port = recv_msg.port;

			send(*to_send);
		}
		else {

			int elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - start).count();
			if (elapsed_time > 60) {
				std::cout << "Killing {" << first_msg.name << "} thread.\n";
				mut_clients.lock();
				for (int i = 0; i < my_status.clients_online.size(); i++) {
					if (my_status.clients_online[i].name == first_msg.name) {						
						my_status.clients_online.erase(my_status.clients_online.begin() + i);
						break;
					}
				}
				mut_clients.unlock();
				return;
			}
		}
	}
}


//initializes socket & related structures
//loads server and client data
void initializeConnection() {

	slen = sizeof(si_send);

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


	printf("\nReading server list...");
	loadServersList();
	if (my_status.MY_PORT < 0 || my_status.NEXT_PORT < 0)
	{
		printf("Could not read server list.\n");
		exit(EXIT_FAILURE);
	}
	//return 0; //<- this properly calls "closeServer" and updates the file...??????????????????????

	printf("\nReading client list...");
	loadClientsData();

	//Prepare the sockaddr_in structure
	si_send.sin_family = AF_INET;
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(my_status.MY_PORT);

	//Bind
	if (bind(s, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
	{
		printf("Bind failed with error code : %d", WSAGetLastError());
		throw("Bind failed with error code : %d", WSAGetLastError());
	}
	puts("Bind done");

	// todo: automatic port. 8888 and go up, like client

	
	// set timeout after 2 seconds of listening
	//int iTimeout = 2000;
	//setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&iTimeout, sizeof(iTimeout));
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

			if (my_status.MY_PORT < 0) {
				if (line.find("on") != std::string::npos) {
					output_file << line + "\n";
					continue;
				}
				else if (line.find("off") != std::string::npos) {
					// Extract port
					my_status.MY_NAME = line.substr(line.find("name:") + std::strlen("name:"), line.find(",status:") - line.find("name:") - std::strlen("name:"));

					my_status.MY_PORT = std::stoi(line.substr(line.find("port:") + std::strlen("port:"), -1));

					my_status.MY_ADDRESS = line.substr(line.find("ip:") + std::strlen("ip:"), line.find(",port") - line.find("ip:") - std::strlen("ip:"));

					// Set off to on
					line.replace(line.find("off"), std::strlen("off"), "on");
					output_file << line + "\n";

					// Get next server's port
					getline(input_file, line);	
					if (line.size() > 0 && !input_file.eof()) {
						output_file << line + "\n";
						my_status.NEXT_PORT = std::stoi(line.substr(line.find("port:") + std::strlen("port:"), -1));

						my_status.NEXT_ADDRESS = line.substr(line.find("ip:") + std::strlen("ip:"), line.find(",port") - line.find("ip:") - std::strlen("ip:"));
					}
					else {
						input_file.clear();
						input_file.seekg(0, std::ios::beg);
						getline(input_file, line);
						my_status.NEXT_PORT = std::stoi(line.substr(line.find("port:") + std::strlen("port:"), -1));
						my_status.NEXT_ADDRESS = line.substr(line.find("ip:") + std::strlen("ip:"), line.find(",port") - line.find("ip:") - std::strlen("ip:"));
						break;
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
		//remove("temp.txt");
		std::cout << "Server loaded: " << my_status.MY_NAME << " [" << my_status.MY_ADDRESS << ":" << my_status.MY_PORT << "]";
		std::cout << "\nNext server: " << "[" << my_status.NEXT_ADDRESS << ":" << my_status.NEXT_PORT << "]\n";
	}
}


//loads registered clients
void loadClientsData() {
	
	std::string filename = "clientslist(" + my_status.MY_NAME + ").txt";
	std::ifstream input_file(filename);

	std::string line;
	if (input_file.is_open()) {

		std::cout << "Clients registered:\n";

		while (getline(input_file, line)) {

			std::cout << line << std::endl;

			client_data new_client;

			std::regex r("name:(.*)status:(.*)addr:(.*)port:(.*)friends:(.*)");
			std::smatch result;
			std::regex_match(line, result, r);

			std::string name = result[1];
			name.replace(0, 1, "");
			name.replace(name.end() - 1, name.end(), "");

			std::string status = result[2];
			status.replace(0, 1, "");
			status.replace(status.end() - 1, status.end(), "");

			std::string addr = result[3];
			addr.replace(0, 1, "");
			addr.replace(addr.end() - 1, addr.end(), "");

			std::string port = result[4];
			port.replace(0, 1, "");
			port.replace(port.end() - 1, port.end(), "");

			new_client.name = name;
			new_client.status = status;
			new_client.addr = addr;
			new_client.port = stoi(port);
			
			std::string friends = result[5];
			friends.replace(0, 1, "");
			friends.replace(friends.end() - 1, friends.end(), "");

			std::string temp;
			for (int i = 0; i < friends.size(); i++) {				
				if (friends[i] != ',') {
					temp += friends[i];
				}
				else {
					new_client.friends.push_back(temp);
					temp = "";
				}
			}
			if (temp != "") { new_client.friends.push_back(temp); }

			my_status.clients_registered.push_back(new_client);
		}

		input_file.close();
		std::cout << std::endl;
	}

}


//saves registered client into my_status and clientslist(serverX).txt
void registerClient(my_MSG data) {

	client_data new_client;

	new_client.name = data.name;
	new_client.addr = data.addr;
	new_client.port = data.port;
	new_client.status = "off";

	mut_clients.lock();
	my_status.clients_registered.push_back(new_client);
	mut_clients.unlock();

	std::string line;
	line = "name:{" + data.name + "}status:{" + "off" +"}addr:{" + data.addr + "}port:{" + std::to_string(data.port) + "}friends:{" + "}\n";	

	std::string filename = "clientslist(" + my_status.MY_NAME + ").txt";
	std::ofstream output_file;

	// this file is shared between all servers in same folder lol, give server name I guess
	output_file.open(filename, std::ios_base::app);

	if (output_file.is_open()) {

		output_file << line;

		output_file.close();
	}
}


//TODO: update clientslist
void updateClientsData(my_MSG data) {

}

//sets status back to off
void closeServer() {

	std::cout << "\nTerminating...\n";

	closesocket(s);
	WSACleanup();

	std::ifstream input_file("serverconfig.txt");
	std::ofstream output_file("temp.txt");

	std::string line;
	if (input_file.is_open()) {

		while (getline(input_file, line)) {

			if (line.size() > 0) {

				if (my_status.MY_PORT == std::stoi(line.substr(line.find("port:") + std::strlen("port:"), -1))) {
					line.replace(line.find("on"), std::strlen("on"), "off");
				}

				output_file << line + "\n";
			}
		}

		input_file.close();
		output_file.close();
		// delete the original file
		std::remove("serverconfig.txt");
		// rename old to new
		std::rename("temp.txt", "serverconfig.txt");
	}
}

//cout my_MSG
void printMsg(my_MSG* msgPacket)
{
	std::cout << "type::" <<msgPacket->type << std::endl;
	std::cout << "id::" << msgPacket->id << std::endl;
	std::cout << "port::" << msgPacket->port << std::endl;
	std::cout << "addr::" << msgPacket->addr << std::endl;
	std::cout << "name::" << msgPacket->name << std::endl;
	std::cout << "msg::" << msgPacket->message << std::endl;
	std::cout << "sever_msg::" << msgPacket->SERVER_MSG << std::endl << std::endl;
}

//locks mut_send ,pushes message into queue to be sent
void send(my_MSG msg) {
	mut_send.lock();
	temp.push_back(msg);
	mut_send.unlock();
}

//unexpected exit handler
//calls closeServer when you X out
BOOL WINAPI ConsoleCtrlEventHandler(DWORD dwCtrlType)
{
	switch (dwCtrlType)
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
		// Do nothing.
		// To prevent other potential handlers from
		// doing anything, return TRUE instead.
		return FALSE;

	case CTRL_CLOSE_EVENT:
		closeServer();
		//MessageBox(NULL, L"You clicked the 'X' in the console window! Ack!", L"I'm melting!", MB_OK | MB_ICONINFORMATION);
		return FALSE;

	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		// Please be careful to read the implications of using
		// each one of these, and the applicability to your
		// code. Unless you are writing a Windows Service,
		// chances are you only need to pay attention to the
		// CTRL_CLOSE_EVENT type.
		return FALSE;
	}

	// If it gets this far (it shouldn't), do nothing.
	return FALSE;
}