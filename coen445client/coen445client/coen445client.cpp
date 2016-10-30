#include "allheaders.h"
#include "protocol.h"

#define BUFLEN 512  //Max length of buffer

void printMsg(my_MSG* MSGPacket);

// My data
std::string MY_NAME = "";
std::string SERVER_ADDRESS = "";
int			SERVER_PORT = -1;
std::string MY_ADDRESS = "";
int			MY_PORT = 10000;			// port attempted, increment by 1 until bind works

protocol *	protocol_manager;

std::vector<my_MSG> messages_to_send;
std::vector<my_MSG> messages_received;	// to do, cleanup old messages
std::vector<my_MSG> temp;				// temp holder for messages_to_send
std::mutex			mut_send;			// mutex for temp (which is a holder for messages_to_send)
std::mutex			mut_recv;			// mutex for messages_received 

// My functions
void loadServersList();
void initializeConnection();
void getMyExternalIP();
void listener();
void sender();
void send(my_MSG);

// UI
void myInterface();
void getRegistered();
bool registered = false;

// Socket structs
struct sockaddr_in client, si_send, si_recv;
int slen, recv_len;
SOCKET s;
char buf[BUFLEN];
char message[BUFLEN];
WSADATA wsa;

int _tmain(int argc, _TCHAR* argv[])
{
	try {
		protocol_manager = new protocol();

		initializeConnection(); // Initialize winsock, create socket, load server list
		getMyExternalIP();		// Not used

		printf("\nEnter name:\n");
		std::cin >> MY_NAME;
		
		std::async(listener);
		std::async(sender);

		Sleep(100);

		myInterface();
	}
	catch (std::exception e) {
		printf(e.what());
	}

	closesocket(s);
	WSACleanup();

	return 0;
}


//keeps trying to get registered/find server it's registered to
//todo: add timeout when all servers full
void getRegistered() {
	std::cout << "\nTarget server: " << SERVER_ADDRESS << ":" << SERVER_PORT << "\n";

	printf("Registering...\n");
	my_MSG send_this = protocol_manager->register_me(MY_NAME, SERVER_ADDRESS, SERVER_PORT);
	send(send_this);

	while (!registered) {

		bool new_msg = false;
		my_MSG recv_msg;

		mut_recv.lock();
		std::vector<my_MSG>::iterator i = messages_received.begin();
		while (i != messages_received.end())
		{
			if (i->name == MY_NAME) {
				recv_msg = *i;
				messages_received.erase(i);
				new_msg = true;
				break;
			}
			++i;
		}
		mut_recv.unlock();

		if (new_msg) {
			
			if (recv_msg.type == "REGISTERED" || recv_msg.type == "ECHO") {
				registered = true;

				SERVER_ADDRESS = recv_msg.addr;
				SERVER_PORT = recv_msg.port;

				break;
			}
			else {
				my_MSG reply = protocol_manager->register_me(recv_msg);

				if (reply.type != "REGISTER-DENIED") {
					send(reply);
				}
			}
		}
	}	
}


//todo better interface, all use cases
void myInterface() {
	getRegistered();

	while (1) {
		Sleep(100);
	}
}



//sends all messages in temp vector, todo: change name temp
void sender() {

	int count = 0;

	//start communication
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

			msg_to_send.name = MY_NAME;
			msg_to_send.addr = MY_ADDRESS;
			msg_to_send.port = MY_PORT;

			if (sendto(s, (char*)&msg_to_send, BUFLEN, 0, (struct sockaddr*) &si_send, slen) == SOCKET_ERROR)
			{
				printf("sendto() failed with error code : %d\n", WSAGetLastError());
			}
			messages_to_send.erase(messages_to_send.begin());

		}
	}

}


//listens to all messages & pushes them to messages_received
void listener() {

	while (1) {
		printf("Waiting for data...\n");

		//clear the buffer by filling null, it might have previously received data
		memset(buf, '\0', BUFLEN);

		//try to receive some data, this is a blocking call
		if ((recv_len = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_recv, &slen)) == SOCKET_ERROR)
		{
			printf("recvfrom() failed with error code : %d\n", WSAGetLastError());
		}

		if (recv_len > 0) {	

			//received a reply
			//print details of the client/peer and the data received
			my_MSG received_packet; //Re-make the struct
			memcpy(&received_packet, buf, sizeof(received_packet));

			printf("\nReceived packet from %s:%d\n", inet_ntoa(si_recv.sin_addr), ntohs(si_recv.sin_port));
			printMsg(&received_packet);

			mut_recv.lock();
			messages_received.push_back(received_packet);
			mut_recv.unlock();
		}
	}
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

	MY_ADDRESS = line_p;
	std::cout << "Found: " << MY_ADDRESS;
	MY_ADDRESS.erase(MY_ADDRESS.find('\n'));

	_pclose(lsofFile_p);
}


//initializes socket & related structures
//tries MY_PORT, keeps incrementing by 1 until it succeeds bind
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
	memset((char *)&si_send, 0, sizeof(si_send));
	si_send.sin_family = AF_INET;
	si_send.sin_port = htons(SERVER_PORT);
	si_send.sin_addr.S_un.S_addr = inet_addr(SERVER_ADDRESS.c_str());

	//Prepare the sockaddr_in structure
	client.sin_family = AF_INET;
	client.sin_addr.s_addr = INADDR_ANY;

	int i = 0;
	int err = SOCKET_ERROR;

	do {
		client.sin_port = htons(MY_PORT + i);

		//Bind
		if (err = bind(s, (struct sockaddr *)&client, sizeof(client)))
		{
			printf("Bind failed with error code : %d\n", WSAGetLastError());
			i++;
		}
		
	} while (err == SOCKET_ERROR);
	MY_PORT = MY_PORT + i;

	puts("Bind done");

	//int iTimeout = 3000;
	//setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&iTimeout, sizeof(iTimeout));
}


//read server to use from "serverconfig.txt"
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

//locks mut_send ,pushes message into queue to be sent
void send(my_MSG msg) {
	mut_send.lock();
	temp.push_back(msg);
	mut_send.unlock();
}

//cout my_MSG
void printMsg(my_MSG* msgPacket)
{
	std::cout << "type::" << msgPacket->type << std::endl;
	std::cout << "id::" << msgPacket->id << std::endl;
	std::cout << "port::" << msgPacket->port << std::endl;
	std::cout << "addr::" << msgPacket->addr << std::endl;
	std::cout << "name::" << msgPacket->name << std::endl;
	std::cout << "msg::" << msgPacket->message << std::endl;
	std::cout << "sever_msg::" << msgPacket->SERVER_MSG << std::endl << std::endl;
}