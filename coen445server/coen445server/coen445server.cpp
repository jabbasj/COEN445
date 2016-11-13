#include "allheaders.h"
#include "protocol.h"

#define BUFLEN 1024
//TODO: Error handling to prevent crashes due to corrupt/invalid format messages
//TODO: protocol error tolerance, i.e. if contents dmgd ask for resend (currently assuming reply has correct contents)
//TODO: set all clients' status as off when closing

void printMsg(my_MSG MSGPacket);
bool finished = false;

server_status my_status;				//my addr/port, next addr/port, clients registered & online
protocol * protocol_manager;

std::vector<my_MSG> messages_to_send;
std::vector<my_MSG> messages_received;
std::vector<my_MSG> temp;
std::mutex			mut_send;			//mutex for temp (which is a holder for messages_to_send
std::mutex			mut_recv;			//mutex for messages_received 
std::mutex			mut_clients;		//mutex for clients registered
std::mutex			mut_client_file;

// My functions
void initializeConnection();
void loadServersList();
void closeServer();
void loadClientsData();
void registerClient(my_MSG);
bool updateClientsData(my_MSG);
void saveClientsData(client_data, bool);
void registrationHandler(my_MSG);
bool findRequestHandler(my_MSG);
void clientHandler(sockaddr_in, my_MSG);
void printClientsRegistered();
void send(my_MSG);
void receive(my_MSG);
void sender();
void listener();
void getMyExternalIP();
void deserialize(char*, my_MSG*);
void serialize(char*, my_MSG*);

// Winsock structures
SOCKET s;
struct sockaddr_in server, si_send, si_recv;
int slen, recv_len;
char buf[BUFLEN];
char message[BUFLEN];
WSADATA wsa;

BOOL WINAPI ConsoleCtrlEventHandler(DWORD dwCtrlType);


int _tmain(int argc, _TCHAR* argv[])
{
	try {
		SetConsoleCtrlHandler(&ConsoleCtrlEventHandler, TRUE);
		protocol_manager = new protocol(&my_status);

		initializeConnection();
		std::async(printClientsRegistered);
		std::thread s(sender);
		std::thread l(listener);
		s.join();
		l.join();
	}
	catch (std::exception e) {
		printf(e.what());
		closeServer();		
	}
	system("pause");
	return 0;
}


//sends all messages in temp vector, todo: change name temp
void sender() {

	try {
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

				serialize(message, &msg_to_send);

				printMsg(msg_to_send);

				if (sendto(s, message, BUFLEN, 0, (struct sockaddr*) &si_send, slen) == SOCKET_ERROR)
				{
					printf("sendto() failed with error code : %d\n", WSAGetLastError());
				}
				messages_to_send.erase(messages_to_send.begin());

			}

		}
	}
	catch (std::exception e) {
		throw e;
	}
}


//listens to all messages
//pushes them to messages_received if they're needed by client handler
//calls registerationHandler
void listener() {

	try {

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
				bool success = false;
				try {
					deserialize(buf, &received_packet);

					received_packet.addr = inet_ntoa(*(struct in_addr *)&si_recv.sin_addr); //use ip from packet instead of given one because external Ip not working on local machine

					printf("\nReceived packet from %s:%d\n", inet_ntoa(si_recv.sin_addr), ntohs(si_recv.sin_port));
					printMsg(received_packet);

					success = true;
				}
				catch (...) {
					std::cout << "\nERROR PACKET FORMAT\n";
					std::cout << buf << std::endl;
					success = false;
				}

				if (success) {
					std::async(registrationHandler, received_packet);
				}
				else {
					//send error to sender
				}
			}
		}
	}
	catch (std::exception e) {
		throw e;
	}
}


//handles the packet first, does server/registration queries
//launches other handlers if needed: clientHandler, findRequestHandler
void registrationHandler(my_MSG received_packet) {
	try {
		//check if it's related to findReq, which doesn't require registration
		if (findRequestHandler(received_packet)) { return; }

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
			my_MSG denied = protocol_manager->deny_register(received_packet);
			send(denied);
			mut_clients.unlock();
			return;
		}

		if (registered_client && received_packet.SERVER_MSG != 1) {
			std::async(clientHandler, si_recv, received_packet);
			mut_clients.unlock();
		}
		else {
			mut_clients.unlock();

			if (received_packet.type == "REGISTER") {
				//check other servers
				my_MSG registered = protocol_manager->is_registered_query(received_packet);

				send(registered);
			}

			if (received_packet.type == "IS-REGISTERED") {

				mut_clients.lock();
				my_MSG is_registered = protocol_manager->is_registered_query_answer(received_packet);
				mut_clients.unlock();

				if (is_registered.type == "REGISTERED") {
					registerClient(is_registered);
				}

				send(is_registered);
			}
		}
	}
	catch (std::exception e) {
		throw e;
	}
}


//Thread that handles messages from the given client
//Dies after 1 min of idle time
void clientHandler(sockaddr_in sockaddr, my_MSG recv_msg) {
	try {
		my_MSG to_send;
		bool success = false;

		if (recv_msg.type == "REGISTER") {
			to_send = protocol_manager->register_client(recv_msg);
		}
		else if (recv_msg.type == "PUBLISH") {
			success = updateClientsData(recv_msg);

			if (success) {
				to_send = protocol_manager->published(recv_msg);
			}
			else {
				to_send = protocol_manager->unpublished(recv_msg);
			}
		}
		else if (recv_msg.type == "INFORMReq") {

			bool client_found = false;
			mut_clients.lock();
			for (int i = 0; i < my_status.clients_registered.size(); i++) {
				if (my_status.clients_registered[i].name == recv_msg.name) {
					to_send = protocol_manager->inform_resp(recv_msg, my_status.clients_registered[i]);
					client_found = true;
					break;
				}
			}
			mut_clients.unlock();

			if (!client_found) {
				//shouldn't get here
				to_send = protocol_manager->error(recv_msg, "NAME_NOT_RESOLVED");
			}
		}
		else {
			to_send = protocol_manager->error(recv_msg, "UNKNOWN");
		}

		to_send.addr = inet_ntoa(*(struct in_addr *)&sockaddr.sin_addr);
		to_send.port = recv_msg.port;
		send(to_send);
	}
	catch (std::exception e) {
		throw e;
	}
}


//handles find requests, which doesn't require registration
bool findRequestHandler(my_MSG recv_msg) {

	if (recv_msg.type == "FINDReq") {
		my_MSG to_send;

		bool client_found = false;
		mut_clients.lock();
		for (int i = 0; i < my_status.clients_registered.size(); i++) {
			if (my_status.clients_registered[i].name == recv_msg.message) {
				to_send = protocol_manager->find_resp(recv_msg, my_status.clients_registered[i]);
				client_found = true;
				break;
			}
		}
		mut_clients.unlock();

		if (!client_found) {
			to_send = protocol_manager->refer(recv_msg);
		}
		send(to_send);

		return true;
	}	
	return false;
}


//obtains my external IP using curl
void getMyExternalIP() {
	printf("Fetching my external IP...\n");

	//std::string answer = std::to_string(system("curl http://myexternalip.com/raw"));
	FILE *lsofFile_p = _popen("curl http://myexternalip.com/raw", "r");

	if (!lsofFile_p)
	{
		return;
	}

	char buffer[1024];
	char *line_p = fgets(buffer, sizeof(buffer), lsofFile_p);

	my_status.MY_ADDRESS = line_p;
	std::cout << "Found: " << my_status.MY_ADDRESS;
	my_status.MY_ADDRESS.erase(my_status.MY_ADDRESS.find('\n'));

	_pclose(lsofFile_p);
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


	printf("Reading server list...");
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
	std::cout << "Binding...";
	if (bind(s, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
	{
		printf("Bind failed with error code : %d", WSAGetLastError());
		throw("Bind failed with error code : %d", WSAGetLastError());
	}
	std::cout << "Bind done.\n";

	//getMyExternalIP();
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
	try {
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
						if (line.size() > 0) {
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

			std::string title = "Server: " + my_status.MY_NAME + " (" + my_status.MY_ADDRESS + ":" + std::to_string(my_status.MY_PORT) + ")";
			title += " - Next server: " + my_status.NEXT_ADDRESS + ":" + std::to_string(my_status.NEXT_PORT);
			SetConsoleTitle(std::wstring(title.begin(), title.end()).c_str());
		}
	}
	catch (...) {
		throw "Error loadServersList()\n";
	}
}



//loads registered clients
void loadClientsData() {
	
	std::string filename = "clientslist(" + my_status.MY_NAME + ").txt";
	std::ifstream input_file(filename);
	std::string line;

	try {
		if (input_file.is_open()) {

			std::cout << "Clients registered:\n";

			while (getline(input_file, line)) {

				std::cout << line << std::endl;

				client_data new_client;

				std::regex r("name:\\{(.*)\\}status:\\{(.*)\\}addr:\\{(.*)\\}port:\\{(.*)\\}friends:\\{(.*)\\}");
				std::smatch result;
				std::regex_match(line, result, r);

				std::string name, status, addr, port, friends = "";

				name = result[1];
				status = result[2];
				addr = result[3];
				port = result[4];
				friends = result[5];

				new_client.name = name;
				new_client.status = status;
				new_client.addr = addr;
				new_client.port = stoi(port);

				std::string temp = "";
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
	catch (...) {
		throw "Error loadClientsData()\n";
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

	try {
		mut_client_file.lock();
		output_file.open(filename, std::ios_base::app);

		if (output_file.is_open()) {

			output_file << line;

			output_file.close();
		}
	}
	catch (...) {
		throw "Error registerClient\n";
	}
	mut_client_file.unlock();
}


//update client_registered and client_online
//call saveClientData
//message field format: {ON/OFF}{friend1,friend2,friendx,}
bool updateClientsData(my_MSG data) {

	std::string client_status = "";
	int			client_port = data.port;
	std::string client_addr = data.addr;
	std::vector	<std::string> client_friends;
	bool update_friends = true;
	bool success = false;

	try {
		std::regex r("status:\\{(.*)\\}friends:\\{(.*)\\}");
		std::smatch match_result;
		std::regex_match(data.message, match_result, r);

		if (client_port < 0) { return false; }
		if (client_addr == "") { return false; }

		if (!match_result.empty() && match_result[0].length() > 0 && match_result.size() == 3) {
			client_status = match_result[1];

			if (client_status != "on" && client_status != "off") {
				return false;
			}

			std::string temp = match_result[2];

			std::string delimiter = ",";
			size_t pos = 0;
			std::string a_friend;
			while ((pos = temp.find(delimiter)) != std::string::npos) {
				a_friend = temp.substr(0, pos);
				client_friends.push_back(a_friend);
				temp.erase(0, pos + delimiter.length());
			}
			success = true;

		}
		else {
			std::regex rr("status:\\{(.*)\\}");
			std::smatch status_match;
			std::regex_match(data.message, status_match, rr);

			if (!status_match.empty() && status_match[0].length() > 0 && status_match.size() == 2) {
				client_status = status_match[1];

				if (client_status != "on" && client_status != "off") {
					return false;
				}
				update_friends = false;
				success = true;
			}
		}
	}
	catch (...) {
		std::cout << "Error updateClientsData\n";
		return false;
	}

	if (success) {
		mut_clients.lock();
		int i = 0;
		for (; i < my_status.clients_registered.size(); i++) {
			if (my_status.clients_registered[i].name == data.name) {
				my_status.clients_registered[i].status = client_status;
				my_status.clients_registered[i].port = client_port;
				my_status.clients_registered[i].addr = client_addr;
				if (update_friends) {
					my_status.clients_registered[i].friends = client_friends;
				}
				break;
			}
		}
		mut_clients.unlock();
		saveClientsData(my_status.clients_registered[i], update_friends);
	}
	return success;
}


//update client's data in clientslist(servername).txt
void saveClientsData(client_data client, bool update_friends) {

	mut_client_file.lock();
	std::string in_filename = "clientslist(" + my_status.MY_NAME + ").txt";
	std::string out_filename = "temp(" + my_status.MY_NAME + ").txt";
	std::ifstream input_file(in_filename);
	std::ofstream temp(out_filename);

	std::string line;
	try {
		if (input_file.is_open()) {

			std::cout << "\nSaving publish info for: " << client.name << "\n";

			while (getline(input_file, line)) {

				//name:{}status:{}addr:{}port:{}friends:{,,,}
				if (line.find("name:{" + client.name + "}") == std::string::npos) {
					temp << line + "\n";
					continue;
				}
				else {

					std::string name = "name:{" + client.name + "}";
					std::string status = "status:{" + client.status + "}";
					std::string addr = "addr:{" + client.addr + "}";
					std::string port = "port:{" + std::to_string(client.port) + "}";
					std::string friends = "friends:{";

					if (update_friends) {
						for (int i = 0; i < client.friends.size(); i++) {
							friends += client.friends[i] + ",";
						}
						friends = friends.substr(0, friends.find_last_of(',')) + "}";
					}
					else {
						friends = line.substr(line.find("friends:{"), -1);
					}

					temp << name + status + addr + port + friends + "\n";
				}
			}

			input_file.close();
			temp.close();
			// delete the original file
			remove(in_filename.c_str());
			// rename old to new
			rename(out_filename.c_str(), in_filename.c_str());
		}
	}
	catch (...) {
		throw "Error saveClientsData()\n";
	}
	mut_client_file.unlock();
}

//Periodically prints clients list
void printClientsRegistered() {

	while (1) {

		Sleep(40000);
		mut_clients.lock();
		std::cout << "\nPRINTING CLIENTS LIST:\n";
		std::cout << "================================================================================\n";
		for (int i = 0; i < my_status.clients_registered.size(); i++) {
			client_data client = my_status.clients_registered[i];

			std::cout << "name:{" << client.name << "}status:{" << client.status;
			std::cout << "}addr:{" << client.addr << "}port:{" << client.port;
			std::cout << "}friends:{";

			for (int j = 0; j < client.friends.size(); j++) {
				std::cout << client.friends[j] << ", ";
			}
			std::cout << "}\n";
		}
		std::cout << "================================================================================\n";
		mut_clients.unlock();
	}
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
void printMsg(my_MSG msgPacket)
{
	std::cout << "type::" << msgPacket.type << std::endl;
	std::cout << "id::" << msgPacket.id << std::endl;
	std::cout << "port::" << msgPacket.port << std::endl;
	std::cout << "addr::" << msgPacket.addr << std::endl;
	std::cout << "name::" << msgPacket.name << std::endl;
	std::cout << "msg::" << msgPacket.message << std::endl;
	std::cout << "sever_msg::" << msgPacket.SERVER_MSG << std::endl << std::endl;
}


//locks mut_send ,pushes message into queue to be sent
void send(my_MSG msg) {
	mut_send.lock();
	temp.push_back(msg);
	mut_send.unlock();
}

void receive(my_MSG msg) {
	mut_recv.lock();
	messages_received.push_back(msg);
	mut_recv.unlock();
}


//unexpected exit handler
//calls closeServer when you X out
BOOL WINAPI ConsoleCtrlEventHandler(DWORD dwCtrlType)
{
	switch (dwCtrlType)
	{
	case CTRL_C_EVENT:
		closeServer();
		return FALSE;
	case CTRL_BREAK_EVENT:
		closeServer();
		// Do nothing.
		// To prevent other potential handlers from
		// doing anything, return TRUE instead.
		return FALSE;

	case CTRL_CLOSE_EVENT:
		closeServer();
		//MessageBox(NULL, L"You clicked the 'X' in the console window! Ack!", L"I'm melting!", MB_OK | MB_ICONINFORMATION);
		return FALSE;

	case CTRL_LOGOFF_EVENT:
		closeServer();
		return FALSE;
	case CTRL_SHUTDOWN_EVENT:
		// Please be careful to read the implications of using
		// each one of these, and the applicability to your
		// code. Unless you are writing a Windows Service,
		// chances are you only need to pay attention to the
		// CTRL_CLOSE_EVENT type.
		closeServer();
		return FALSE;
	}
	closeServer();
	// If it gets this far (it shouldn't), do nothing.
	return FALSE;
}


//deserialize given buffer into a my_MSG
//expected format: ^^^type(str)^^^id(int)^^^port(int)^^^addr(str)^^^name(str)^^^message(str)^^^server_msg(int)^^^more_bit(int)^^^offset(int)^^^
void deserialize(char* to_deserialize, my_MSG* result) {
	/*memcpy(&result->type, to_deserialize, sizeof(result->type));
	memcpy(&result->id, to_deserialize + sizeof(result->type), sizeof(int));
	memcpy(&result->port, to_deserialize + sizeof(result->type) + sizeof(result->id), sizeof(int));
	memcpy(&result->addr, to_deserialize + sizeof(result->type) + sizeof(result->id) + sizeof(result->port), sizeof(result->addr));
	memcpy(&result->name, to_deserialize + sizeof(result->type) + sizeof(result->id) + sizeof(result->port) + sizeof(result->addr), sizeof(result->name));
	memcpy(&result->message, to_deserialize + sizeof(result->type) + sizeof(result->id) + sizeof(result->port) + sizeof(result->addr) + sizeof(result->name), sizeof(result->message));
	memcpy(&result->SERVER_MSG, to_deserialize + sizeof(result->type) + sizeof(result->id) + sizeof(result->port) + sizeof(result->addr) + sizeof(result->name) + sizeof(result->message), sizeof(int));
	*/

	std::string pattern = "^^^";
	std::string to_deserial = to_deserialize;
	/*	^^^type(str)^^^id(int)^^^port(int)^^^addr(str)^^^name(str)^^^message(str)^^^server_msg(int)^^^	*/
	std::regex r("\\^\\^\\^(.*)\\^\\^\\^(.*)\\^\\^\\^(.*)\\^\\^\\^(.*)\\^\\^\\^(.*)\\^\\^\\^(.*)\\^\\^\\^(.*)\\^\\^\\^(.*)\\^\\^\\^(.*)\\^\\^\\^");
	std::smatch match_result;
	std::regex_match(to_deserial, match_result, r);

	if (!match_result.empty() && match_result[0].length() > 0 && match_result.size() == 10) {
		result->type = match_result[1];
		result->id = stoi(match_result[2]);
		result->port = stoi(match_result[3]);
		result->addr = match_result[4];
		result->name = match_result[5];
		result->message = match_result[6];
		result->SERVER_MSG = stoi(match_result[7]);
		result->MORE_BIT = stoi(match_result[8]);
		result->OFFSET = stoi(match_result[9]);
	}
	else {
		throw "Error deserialize()\n";
	}

}


//serialize a my_MSG into a buffer using a ^^^ delimiter pattern for fields
void serialize(char* result, my_MSG* to_serialize) {

	/*
	memset(result, '\0', BUFLEN);
	memcpy(result, &to_serialize->type, sizeof(to_serialize->type));
	memcpy(result + sizeof(to_serialize->type), &to_serialize->id, sizeof(to_serialize->id));
	memcpy(result + sizeof(to_serialize->type) + sizeof(to_serialize->id), &to_serialize->port, sizeof(to_serialize->port));
	memcpy(result + sizeof(to_serialize->type) + sizeof(to_serialize->id) + sizeof(to_serialize->port), &to_serialize->addr, sizeof(to_serialize->addr));
	memcpy(result + sizeof(to_serialize->type) + sizeof(to_serialize->id) + sizeof(to_serialize->port) + sizeof(to_serialize->addr), &to_serialize->name, sizeof(to_serialize->name));
	memcpy(result + sizeof(to_serialize->type) + sizeof(to_serialize->id) + sizeof(to_serialize->port) + sizeof(to_serialize->addr) + sizeof(to_serialize->name), &to_serialize->message, sizeof(to_serialize->message));
	memcpy(result + sizeof(to_serialize->type) + sizeof(to_serialize->id) + sizeof(to_serialize->port) + sizeof(to_serialize->addr) + sizeof(to_serialize->name) + sizeof(to_serialize->message), &to_serialize->SERVER_MSG, sizeof(to_serialize->SERVER_MSG));
	*/

	try {
		memset(result, '\0', BUFLEN);
		char pattern[] = "^^^";
		int i = 0;
		int j = 0;
		std::string data;

		memcpy(result + i, pattern, strlen(pattern)); i += strlen(pattern); //start
		for (; j < to_serialize->type.size(); j++) { //type
			result[i++] = to_serialize->type[j];
		}
		memcpy(result + i, pattern, strlen(pattern)); i += strlen(pattern);

		data = std::to_string(to_serialize->id); //id
		for (j = 0; j < data.size(); j++) {
			result[i++] = data[j];
		}
		memcpy(result + i, pattern, strlen(pattern)); i += strlen(pattern);

		data = std::to_string(to_serialize->port); //port
		for (j = 0; j < data.size(); j++) {
			result[i++] = data[j];
		}
		memcpy(result + i, pattern, strlen(pattern)); i += strlen(pattern);

		for (j = 0; j < to_serialize->addr.size(); j++) { //addr
			result[i++] = to_serialize->addr[j];
		}
		memcpy(result + i, pattern, strlen(pattern)); i += strlen(pattern);

		for (j = 0; j < to_serialize->name.size(); j++) { //name
			result[i++] = to_serialize->name[j];
		}
		memcpy(result + i, pattern, strlen(pattern)); i += strlen(pattern);

		for (j = 0; j < to_serialize->message.size(); j++) { //message
			result[i++] = to_serialize->message[j];
		}
		memcpy(result + i, pattern, strlen(pattern)); i += strlen(pattern);

		data = std::to_string(to_serialize->SERVER_MSG); // server_msg
		for (j = 0; j < data.size(); j++) {
			result[i++] = data[j];
		}
		memcpy(result + i, pattern, strlen(pattern)); i += strlen(pattern); 
		
		data = std::to_string(to_serialize->MORE_BIT); // more_bit
		for (j = 0; j < data.size(); j++) {
			result[i++] = data[j];
		}
		memcpy(result + i, pattern, strlen(pattern)); i += strlen(pattern);

		data = std::to_string(to_serialize->OFFSET); // more_bit
		for (j = 0; j < data.size(); j++) {
			result[i++] = data[j];
		}		
		memcpy(result + i, pattern, strlen(pattern)); i += strlen(pattern); //finish
	}
	catch (...) {
		throw "Error serialize()\n";
	}
}