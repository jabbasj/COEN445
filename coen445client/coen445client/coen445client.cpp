#include "allheaders.h"
#include "protocol.h"
//TODO: chat message fragmentation
#define BUFLEN 1024  //Max length of buffer
#define START_PORT 10000

void printMsg(my_MSG MSGPacket);

client_status client_info;
std::vector<friend_data> friends_available;
protocol *	protocol_manager;

std::vector<my_MSG> messages_to_send;
std::vector<my_MSG> messages_received;	// to do, cleanup old messages
std::vector<my_MSG> temp;				// temp holder for messages_to_send
std::mutex			mut_send;			// mutex for temp (which is a holder for messages_to_send)
std::mutex			mut_recv;			// mutex for messages_received 
std::mutex			mut_friends;

// My functions
void loadServersList();
void initializeConnection();
void getMyExternalIP();
void listener();
void sender();
void send(my_MSG);
void receive(my_MSG msg);
void resend_old_messages();
void deserialize(char*, my_MSG*);
void serialize(char*, my_MSG*);
void message_handler(my_MSG);
void closeClient();
void cleanup();

// UI
void myInterface();
void getRegistered();
void getPublished();
void getFriend();
void getMyInfo();
void getChatting();
void requestStatusAndFriends(bool*, bool*);
std::string requestFindFriend();
bool registered, published, chat_mode = false;

// Socket structs
struct sockaddr_in client, si_send, si_recv;
int slen, recv_len;
SOCKET s;
char buf[BUFLEN];
char message[BUFLEN];
WSADATA wsa;

BOOL WINAPI ConsoleCtrlEventHandler(DWORD dwCtrlType);

int _tmain(int argc, _TCHAR* argv[])
{
	try {
		SetConsoleCtrlHandler(&ConsoleCtrlEventHandler, TRUE);
		client_info.MY_PORT = START_PORT;
		protocol_manager = new protocol(&client_info);

		initializeConnection(); // Initialize winsock, create socket, load server list
		getMyExternalIP();		// Not used

		std::thread ts(listener);
		std::thread tr(sender);
		std::thread trs(resend_old_messages);
		myInterface();
		ts.join();
		tr.join();
		trs.join();
	}
	catch (std::exception e) {
		printf(e.what());
		closeClient();
		cleanup();
		system("pause");
	}

	cleanup();
	return 0;
}


//keeps trying to get registered/find server it's registered to
void getRegistered() {
	
	printf("\nRegistering...");
	registered = false;
	friends_available.clear();

	printf("Enter your name:\n");
	std::cin >> client_info.MY_NAME;

	my_MSG send_this = protocol_manager->register_me();
	send(send_this);
	int attempts = 5;

	while (!registered) {

		if (messages_received.size() <= 0) {
			continue;
		}

		bool new_msg = false;
		my_MSG recv_msg;

		mut_recv.lock();
		std::vector<my_MSG>::iterator i = messages_received.begin();
		while (i != messages_received.end())
		{
			if (i->name == client_info.MY_NAME) {
				recv_msg = *i;
				messages_received.erase(i);
				new_msg = true;
				break;
			}
			++i;
		}
		mut_recv.unlock();

		if (new_msg) {
			
			if (recv_msg.type == "REGISTERED" && protocol_manager->replied(recv_msg)) {
				registered = true;
				
				//Set title
				std::string title = "Client: " + client_info.MY_NAME + " (" + client_info.MY_ADDRESS + ":" + std::to_string(client_info.MY_PORT) + ")";
				title += " - Target server: " + client_info.SERVER_ADDRESS + ":" + std::to_string(client_info.SERVER_PORT);
				SetConsoleTitle(std::wstring(title.begin(), title.end()).c_str());
				break;
			}
			else {
				my_MSG reply = protocol_manager->register_me(recv_msg);
				attempts--;
				if (attempts > 0) {
					send(reply);
				}
			}
		}
	}	
}


//sends a publish of status & friends
void getPublished() {
	
	printf("\nPublishing..."); if (!registered) { std::cout << "\nERROR: Must register first!\n"; return; };
	published = false; 
	client_info.friends.clear();
	bool status = false;
	bool update_friends = false;

	requestStatusAndFriends(&status, &update_friends);

	my_MSG pub = protocol_manager->publish(status, update_friends);
	send(pub);

	while (!published) {

		if (messages_received.size() <= 0) { continue; }

		bool new_msg = false;
		my_MSG recv_msg;

		mut_recv.lock();
		std::vector<my_MSG>::iterator i = messages_received.begin();
		while (i != messages_received.end())
		{
			if (i->name == client_info.MY_NAME) {
				recv_msg = *i;
				messages_received.erase(i);
				new_msg = true;
				break;
			}
			++i;
		}
		mut_recv.unlock();

		if (new_msg) {

			if (recv_msg.type == "PUBLISHED" && protocol_manager->replied(recv_msg)) {
				published = true;
				break;			
			}
			else if (recv_msg.type == "UNPUBLISHED" && protocol_manager->replied(recv_msg)) {

				my_MSG re_send = protocol_manager->publish(status, update_friends);
				send(re_send);
			}
			else {
				Sleep(1000);
			}
		}
	}
}


//sends an information request
void getMyInfo() {
	if (!registered) { std::cout << "\nERROR: Must register first!\n"; return; };
	std::cout << "Requesting my information...\n";
	bool info_received = false;

	my_MSG inform_req = protocol_manager->inform_req();
	send(inform_req);

	while (!info_received) {
		if (messages_received.size() <= 0) { continue; }

		bool new_msg = false;
		my_MSG recv_msg;

		mut_recv.lock();
		std::vector<my_MSG>::iterator i = messages_received.begin();
		while (i != messages_received.end())
		{
			if (i->name == client_info.MY_NAME) {
				recv_msg = *i;
				messages_received.erase(i);
				new_msg = true;
				break;
			}
			++i;
		}
		mut_recv.unlock();

		if (new_msg) {

			if (recv_msg.type == "INFORMResp" && protocol_manager->replied(recv_msg)) {
				info_received = true;

				client_status my_info = protocol_manager->extract_my_info(recv_msg);
				std::cout << "\n================================================================================\n";
				std::cout << "My info at server (" << client_info.SERVER_ADDRESS << ":" << client_info.SERVER_PORT << "):\n";
				std::cout << "Status: " << my_info.MY_NAME /*meh..*/ << "\nAddress: " << my_info.MY_ADDRESS << ":" << my_info.MY_PORT;
				std::cout << "\nFriends: "; 
				for (int j = 0; j < my_info.friends.size(); j++) {
					std::cout << my_info.friends[j].name << ", ";
				}
				std::cout << "\n================================================================================\n";

				break;
			}
			else {
				Sleep(1000);
			}
		}
	}

}


//tries to find friends to talk to
void getFriend() {
	printf("Finding friend...");

	std::string req_friend = requestFindFriend();
	my_MSG send_this = protocol_manager->find_req(req_friend);
	send(send_this);

	while (1) {

		if (messages_received.size() <= 0) {continue;}

		bool new_msg = false;
		my_MSG recv_msg;

		mut_recv.lock();
		std::vector<my_MSG>::iterator i = messages_received.begin();
		while (i != messages_received.end())
		{
			if (i->name == client_info.MY_NAME) {
				recv_msg = *i;
				messages_received.erase(i);
				new_msg = true;
				break;
			}
			++i;
		}
		mut_recv.unlock();

		if (new_msg) {

			if (recv_msg.type == "REFER") {
				
				my_MSG reply = protocol_manager->find_req(req_friend, recv_msg);

				if (reply.type != "FINDDenied") {
					send(reply);
				}
				else {
					std::cout << "\n================================================================================\n";
					std::cout << "Friend (" << req_friend << ") not found on any server.\n";
					std::cout << "================================================================================\n";
					break;
				}
			}
			else if (recv_msg.type == "FINDDenied" && protocol_manager->replied(recv_msg)) {
				std::cout << "\n================================================================================\n";
				std::cout << "Friend (" << req_friend << ") does not have you added.\n";
				std::cout << "================================================================================\n";
				break;
			}
			else if (recv_msg.type == "FINDResp" && protocol_manager->replied(recv_msg)) {
				if (recv_msg.message == "off") {
					std::cout << "\n================================================================================\n";
					std::cout << "Friend (" << req_friend << ") currently offline.\n";
					std::cout << "================================================================================\n";
				}
				else {
					std::cout << "\n================================================================================\n";
					std::cout << "Friend (" << req_friend << ") available to chat at ("<< recv_msg.message <<").\n";
					std::cout << "================================================================================\n";
					
					friend_data new_friend = protocol_manager->extract_friend_data(recv_msg, req_friend);
					
					bool friend_exists = false;
					for (int j = 0; j < friends_available.size(); j++) {
						if (friends_available[j].name == req_friend) {
							friend_exists = true; //update existing friend
							friends_available[j].addr = new_friend.addr;
							friends_available[j].port = new_friend.port;
							break;
						}
					}
					if (!friend_exists) {
						friends_available.push_back(new_friend);
					}
				}
				break;
			}
		}
	}
}


//TODO: add fragmentation of long chat messages
void getChatting() {

	if (!registered) { std::cout << "\nERROR: Must register first!\n"; return; };
	if (!published) { std::cout << "\nERROR: Must publish first!\n"; return; };
	if (friends_available.size() == 0) { std::cout << "\nDiscover friends first!\n"; return; }
	std::cout << "Entered Chat Mode...";
	chat_mode = true;

	std::string input = "";
	while (friends_available.size() > 0) {
		std::cout << "\nSend message to: (";

		for (int i = 0; i < friends_available.size(); i++) {
			std::cout << i << ": " << friends_available[i].name << ", "; 
		}
		std::cout << ")........\n";
		try {
			std::cin >> input;

			if (input == "end" || friends_available.size() == 0) {
				chat_mode = false;
				return;
			}

			int selection = stoi(input);

			if (selection > friends_available.size() - 1 || selection < 0) {
				std::cout << "Wrong index!\n";
				input = "";
				continue;
			}
			
			friend_data selected_friend = friends_available[selection];
			std::string my_message;
			std::cout << "================================================================================\n";
			std::cout << "Enter message for (" << selected_friend.name << "): \n";	
			std::cin.ignore();
			std::getline(std::cin, my_message);
			std::cout << "================================================================================";

			my_MSG chat_msg = protocol_manager->chat(selected_friend, my_message);
			send(chat_msg);

			Sleep(100);

		}
		catch (...) {
			std::cout << "Wrong input!\n";
			input = "";
		}
	}

}


void myInterface() {
	getRegistered();

	std::string input = "";

	while (1) {
		std::cout << "Select option (1: Publish, 2: Info Request, 3: Friend search, 4: Chatting): \n";
		try {
			std::cin >> input;

			if (input == "end") {
				closeClient();
				exit(0);
			}

			switch (stoi(input)) {
				case 0:
					closeClient();
					getRegistered();
					break;
				case 1:
					getPublished();
					break;
				case 2:
					getMyInfo();
					break;
				case 3:
					getFriend();
					break;
				case 4:
					getChatting();
					break;
				default:
					std::cout << "Invalid option.\n";
			}
		}
		catch (...) {
			std::cout << "Wrong input!\n";
			input = "";
		}
	}
}


//sends all messages in temp vector, todo: change name temp
void sender() {

	while (1)
	{
		if (temp.size() > 0) {
			if (!chat_mode) {
				printf("Sending (%d) pending messages...\n", temp.size());
			}

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

			msg_to_send.name = client_info.MY_NAME;
			msg_to_send.addr = client_info.MY_ADDRESS;
			msg_to_send.port = client_info.MY_PORT;

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


//listens to all messages & pushes them to messages_received
void listener() {

	while (1) {
		if (!chat_mode) {
			printf("Waiting for data...\n");
		}

		//clear the buffer by filling null, it might have previously received data
		memset(buf, '\0', BUFLEN);


		//try to receive some data, this is a blocking call
		if ((recv_len = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_recv, &slen)) == SOCKET_ERROR)
		{
			printf("recvfrom() failed with error code : %d\n", WSAGetLastError());
		}

		if (recv_len > 0) {	
			my_MSG received_packet;
			deserialize(buf, &received_packet);
			
			received_packet.addr = inet_ntoa(*(struct in_addr *)&si_recv.sin_addr); //use ip from packet instead of given one because external Ip not working on local machine

			printf("\nReceived packet from %s:%d\n", inet_ntoa(si_recv.sin_addr), ntohs(si_recv.sin_port));
			printMsg(received_packet);

			std::thread h(message_handler, received_packet);
			h.detach();			
		}
	}
}


void message_handler(my_MSG recv_msg) {

	if (recv_msg.type == "ACK") {		

		if (!protocol_manager->replied(recv_msg)) {
			send(protocol_manager->error(recv_msg, "UNEXPECTED"));
		}
		std::cout << "ACK\n";
	}
	else if (recv_msg.type == "CHAT") {
		std::cout << "================================================================================\n";
		std::cout << "Message from (" << recv_msg.name << "):\n" << recv_msg.message << std::endl;
		std::cout << "================================================================================\n";
		my_MSG reply = protocol_manager->ack(recv_msg);
		send(reply);
	}
	else if (recv_msg.type == "BYE") {
		mut_friends.lock();
		std::vector<friend_data>::iterator i = friends_available.begin();
		while (i != friends_available.end())
		{
			if (i->name == recv_msg.name) {
				std::cout << "================================================================================\n";
				std::cout << "BYE from (" << recv_msg.name << "):\n";
				std::cout << "================================================================================\n";
				friends_available.erase(i);
				break;
			}
			++i;
		}
		mut_friends.unlock();
	}

	receive(recv_msg);
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

	client_info.MY_ADDRESS = line_p;
	std::cout << "Found: " << client_info.MY_ADDRESS;
	client_info.MY_ADDRESS.erase(client_info.MY_ADDRESS.find('\n'));

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
	if (client_info.SERVER_ADDRESS == "" || client_info.SERVER_PORT == -1) {
		printf("Error reading server addr/port\n");
		exit(EXIT_FAILURE);
	}

	//setup address structure
	memset((char *)&si_send, 0, sizeof(si_send));
	si_send.sin_family = AF_INET;
	si_send.sin_port = htons(client_info.SERVER_PORT);
	si_send.sin_addr.S_un.S_addr = inet_addr(client_info.SERVER_ADDRESS.c_str());

	//Prepare the sockaddr_in structure
	client.sin_family = AF_INET;
	client.sin_addr.s_addr = INADDR_ANY;

	int i = 0;
	int err = SOCKET_ERROR;

	std::cout << "Binding...";
	do {
		client.sin_port = htons(client_info.MY_PORT + i);

		//Bind
		if (err = bind(s, (struct sockaddr *)&client, sizeof(client)))
		{
			printf("Bind failed with error code : %d\n", WSAGetLastError());
			i++;
		}
		
	} while (err == SOCKET_ERROR);
	client_info.MY_PORT = client_info.MY_PORT + i;
	std::cout<< "Bind done.\n";

	std::cout << "Client loaded on port: " << client_info.MY_PORT << "\n";
	std::cout << "Target server: " << client_info.SERVER_ADDRESS << ":" << client_info.SERVER_PORT << "\n";

	//int iTimeout = 3000;
	//setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&iTimeout, sizeof(iTimeout));
}


//read server to use from "serverconfig.txt"
void loadServersList() {

	std::ifstream input_file("serverconfig.txt");

	std::string line;
	if (input_file.is_open()) {

		getline(input_file, line);

		client_info.SERVER_PORT = std::stoi(line.substr(line.find("port:") + std::strlen("port:"), -1));
		client_info.SERVER_ADDRESS = line.substr(line.find("ip:") + std::strlen("ip:"), line.find(",port") - line.find("ip:") - std::strlen("ip:"));

		input_file.close();
	}
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


//resends messages that haven't been replied to for over 10 seconds
void resend_old_messages() {
	std::vector<my_MSG> timed_out;

	auto start = std::chrono::system_clock::now();

	while (1) {
		
		timed_out = protocol_manager->timed_out_msgs();

		if (timed_out.size() > 0) {
			std::cout << "\nResending (" << timed_out.size() << ") following timed out messages:\n";
		}
		for (int i = 0; i < timed_out.size(); i++) {
			send(timed_out[i]);
		}

		Sleep(10000);
		start = std::chrono::system_clock::now();
	}
}


//cout my_MSG
void printMsg(my_MSG msgPacket)
{
	if (!chat_mode) {
		std::cout << "type::" << msgPacket.type << std::endl;
		std::cout << "id::" << msgPacket.id << std::endl;
		std::cout << "port::" << msgPacket.port << std::endl;
		std::cout << "addr::" << msgPacket.addr << std::endl;
		std::cout << "name::" << msgPacket.name << std::endl;
		std::cout << "msg::" << msgPacket.message << std::endl;
		std::cout << "sever_msg::" << msgPacket.SERVER_MSG << std::endl << std::endl;
	}
}


//deserialize given buffer into a my_MSG
//expected format: ^^^type(str)^^^id(int)^^^port(int)^^^addr(str)^^^name(str)^^^message(str)^^^server_msg(int)^^^
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
	std::regex r("\\^\\^\\^(.*)\\^\\^\\^(.*)\\^\\^\\^(.*)\\^\\^\\^(.*)\\^\\^\\^(.*)\\^\\^\\^(.*)\\^\\^\\^(.*)\\^\\^\\^");
	std::smatch match_result;
	std::regex_match(to_deserial, match_result, r);

	if (!match_result.empty() && match_result[0].length() > 0 && match_result.size() == 8) {
		result->type = match_result[1];
		result->id = stoi(match_result[2]);
		result->port = stoi(match_result[3]);
		result->addr = match_result[4];
		result->name = match_result[5];
		result->message = match_result[6];
		result->SERVER_MSG = stoi(match_result[7]);
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
	memcpy(result + i, pattern, strlen(pattern)); i += strlen(pattern); //finish
}


//asks user for status and friends
void requestStatusAndFriends(bool* status, bool* update_friends) {

	std::string input = "";
	friend_data my_friend;

	while (input != "off" && input != "on") {
		std::cout << "Enter your status (on/off):\n";
		try {
			std::cin >> input;

			if (input == "on") {
				*status = true;
				break;
			}
			else if (input == "off") {
				*status = false;
				break;
			}
			else {
				std::cout << "Choose either off or on.\n";
			}
		}
		catch (...) {
			std::cout << "Wrong input!\n";
			input = "";
		}
	}
	input = "";

	while (input != "done") {
		std::cout << "\nEnter your friend's names (or \"done\"): \n";
		try {
			std::cin >> input;

			if (input.find_first_of('^') != std::string::npos) {
				std::cout << "Invalid character.\n";
			}
			else if (input != "done" && input != "") {
				my_friend.name = input;
				client_info.friends.push_back(my_friend);
				*update_friends = true;
			}
		}
		catch (...) {
			std::cout << "Wrong input!\n";
			input = "";
		}
	}

	return;
}


std::string requestFindFriend() {
	std::string input = "";

	while (input == "") {
		std::cout << "Enter friend to find: \n";
		try {
			std::cin >> input;

			if (input.find_first_of('^') != std::string::npos) {
				std::cout << "Invalid character.\n";
				input = "";
			}
		}
		catch (...) {
			std::cout << "Wrong input!\n";
			input = "";
		}
	}

	return input;
}


//send bye to friends I was chantting with
//send publish status off to server
void closeClient() {

	mut_send.lock();

	if (registered) {
		my_MSG publish_status_off = protocol_manager->publish(false, false, false);

		serialize(message, &publish_status_off);

		std::wstring stemp = std::wstring(publish_status_off.addr.begin(), publish_status_off.addr.end());
		LPCWSTR sw = stemp.c_str();

		InetPton(AF_INET, sw, &si_send.sin_addr);
		si_send.sin_addr.S_un.S_addr = inet_addr(publish_status_off.addr.c_str());
		si_send.sin_port = htons(publish_status_off.port);

		if (sendto(s, message, BUFLEN, 0, (struct sockaddr*) &si_send, slen) == SOCKET_ERROR)
		{
			printf("sendto() failed with error code : %d\n", WSAGetLastError());
		}
	}

	for (int i = 0; i < friends_available.size(); i++) {

		my_MSG bye = protocol_manager->bye(friends_available[i]);
		serialize(message, &bye);

		std::wstring stemp = std::wstring(bye.addr.begin(), bye.addr.end());
		LPCWSTR sw = stemp.c_str();

		InetPton(AF_INET, sw, &si_send.sin_addr);
		si_send.sin_addr.S_un.S_addr = inet_addr(bye.addr.c_str());
		si_send.sin_port = htons(bye.port);

		if (sendto(s, message, BUFLEN, 0, (struct sockaddr*) &si_send, slen) == SOCKET_ERROR)
		{
			printf("sendto() failed with error code : %d\n", WSAGetLastError());
		}

	}

	mut_send.unlock();
}


void cleanup() {
	closesocket(s);
	WSACleanup();
}


BOOL WINAPI ConsoleCtrlEventHandler(DWORD dwCtrlType)
{
	switch (dwCtrlType)
	{
	case CTRL_C_EVENT:
		closeClient();
		cleanup();
		return FALSE;
	case CTRL_BREAK_EVENT:
		closeClient();
		cleanup();
		// Do nothing.
		// To prevent other potential handlers from
		// doing anything, return TRUE instead.
		return FALSE;

	case CTRL_CLOSE_EVENT:
		closeClient();
		cleanup();
		//MessageBox(NULL, L"You clicked the 'X' in the console window! Ack!", L"I'm melting!", MB_OK | MB_ICONINFORMATION);
		return FALSE;

	case CTRL_LOGOFF_EVENT:
		closeClient();
		cleanup();
		return FALSE;
	case CTRL_SHUTDOWN_EVENT:
		// Please be careful to read the implications of using
		// each one of these, and the applicability to your
		// code. Unless you are writing a Windows Service,
		// chances are you only need to pay attention to the
		// CTRL_CLOSE_EVENT type.
		closeClient();
		cleanup();
		return FALSE;
	}
	closeClient();
	cleanup();
	// If it gets this far (it shouldn't), do nothing.
	return FALSE;
}