#include "protocol.h"

my_MSG protocol::register_me() {

	my_MSG answer;

	answer.type = "REGISTER";
	answer.id = getId();
	answer.name = client_info->MY_NAME;
	answer.addr = client_info->SERVER_ADDRESS;
	answer.port = client_info->SERVER_PORT;
	answer.SERVER_MSG = 0;

	new_msg(answer);

	return answer;
}


//try to register to next server
my_MSG protocol::register_me(my_MSG msg) {

	my_MSG reply_to = replied_to(msg);

	if (reply_to.id != 0) {

		if (msg.type == "REGISTER-DENIED") {	
		
			client_info->SERVER_ADDRESS = msg.message.substr(0, msg.message.find(":"));
			client_info->SERVER_PORT = stoi(msg.message.substr(msg.message.find(":") + 1, -1));

			return register_me();
		}
	}
	//shouldn't get here
	return register_me();
}


//uses client_info->friends and given status to
//format message as: status:{on/off}friends:{friend1,friend2,friendx,}
my_MSG protocol::publish(bool my_status, bool update_friends, bool expect_reply /*= true*/) {

	my_MSG answer;

	answer.type = "PUBLISH";
	answer.id = getId();
	answer.name = client_info->MY_NAME;
	answer.addr = client_info->SERVER_ADDRESS;
	answer.port = client_info->SERVER_PORT;

	std::string status = "";
	if (my_status) {status = "status:{on}";}
	else {
		status = "status:{off}";
	}

	if (update_friends) {
		std::string friends = "friends:{";
		for (int i = 0; i < client_info->friends.size(); i++) {
			friends += client_info->friends[i].name + ",";
		}
		//status:{on/off}friends:{friend1,friend2,friendx,}
		answer.message = status + friends + "}";
	}
	else {
		answer.message = status;
	}

	if (expect_reply) {
		new_msg(answer);
	}

	return answer;
}


my_MSG protocol::inform_req() {

	my_MSG answer;

	answer.type = "INFORMReq";
	answer.id = getId();
	answer.name = client_info->MY_NAME;
	answer.addr = client_info->SERVER_ADDRESS;
	answer.port = client_info->SERVER_PORT;
	answer.SERVER_MSG = 0;

	new_msg(answer);

	return answer;
}


my_MSG protocol::find_req(std::string name) {

	my_MSG answer;

	answer.type = "FINDReq";
	answer.id = getId();
	answer.name = client_info->MY_NAME;
	answer.addr = client_info->SERVER_ADDRESS;
	answer.port = client_info->SERVER_PORT;
	answer.SERVER_MSG = 0;
	answer.message = name;

	new_msg(answer);

	return answer;
}


my_MSG protocol::find_req(std::string name, my_MSG msg) {

	my_MSG reply_to = replied_to(msg);

	if (reply_to.id != 0) {

		if (msg.type == "REFER") {

			my_MSG answer;
			answer.type = "FINDReq";
			answer.id = getId();
			answer.addr = msg.message.substr(0, msg.message.find(":"));
			answer.port = stoi(msg.message.substr(msg.message.find(":") + 1, -1));
			answer.message = name;
			answer.SERVER_MSG = 0;

			if (answer.addr == client_info->SERVER_ADDRESS && answer.port == client_info->SERVER_PORT) {
				answer.type = "FINDDenied";
			}
			else {
				new_msg(answer);
			}

			return answer;
		}
	}
	//shouldn't get here
	return find_req(name);
}


friend_data protocol::extract_friend_data(my_MSG msg, std::string name) {

	std::string addr = msg.message.substr(0, msg.message.find(':'));
	std::string port = msg.message.substr(msg.message.find(':') + 1, std::string::npos);

	friend_data friend_found;
	friend_found.name = name;
	friend_found.addr = addr;
	friend_found.port = stoi(port);

	return friend_found;
}


client_status protocol::extract_my_info(my_MSG msg) {

	std::regex r("status:\\{(.*)\\}addr:\\{(.*)\\}port:\\{(.*)\\}friends:\\{(.*)\\}");
	std::smatch result;
	std::regex_match(msg.message, result, r);

	std::string status, addr, port, friends = "";
	status = result[1];
	addr = result[2];
	port = result[3];
	friends = result[4];

	if (friends.size() > 0) {
		friends.replace(friends.end() - 1, friends.end(), "");
	}

	client_status my_info;
	my_info.MY_NAME = status;
	my_info.MY_ADDRESS = addr;
	my_info.MY_PORT = stoi(port);

	friend_data temp_friend;
	std::string temp = "";
	for (int i = 0; i < friends.size(); i++) {
		if (friends[i] != ',') {
			temp += friends[i];
		}
		else {
			temp_friend.name = temp;
			my_info.friends.push_back(temp_friend);
			temp = "";
		}
	}
	if (temp != "") { 
		temp_friend.name = temp;
		my_info.friends.push_back(temp_friend);
	}

	return my_info;
}


my_MSG protocol::chat(friend_data to_friend, std::string message) {

	my_MSG answer;

	answer.type = "CHAT";
	answer.addr = to_friend.addr;
	answer.port = to_friend.port;
	answer.name = client_info->MY_NAME;
	answer.id = getId();
	answer.message = message;
	answer.SERVER_MSG = 0;

	new_msg(answer);
	
	return answer;
}


std::vector<my_MSG> protocol::send_fragmented_chat(friend_data to_friend, std::string message) {

	std::vector<my_MSG> messages;

	my_MSG temp_msg;
	temp_msg.type = "CHAT";
	temp_msg.addr = to_friend.addr;
	temp_msg.port = to_friend.port;
	temp_msg.name = client_info->MY_NAME;
	temp_msg.id = getId();
	temp_msg.SERVER_MSG = 0;

	if (message.size() > MAX_MESSAGE_LENGTH) {
		
		std::vector<std::string> message_fragments;
		std::string temp_str; temp_str += message[0];
		for (int j = 1; j < message.size(); j++) {
			temp_str += message[j];
			if ((j % MAX_MESSAGE_LENGTH) == 0) {
				message_fragments.push_back(temp_str);
				temp_str = "";
			}
			else if ((message.size() - j) <= MAX_MESSAGE_LENGTH) {
				message_fragments.push_back(message.substr(j, -1));
				break;
			}
		}
		for (int k = 0; k < message_fragments.size(); k++) {
			temp_msg.MORE_BIT = 1;
			temp_msg.OFFSET = k;
			temp_msg.message = message_fragments[k];

			messages.push_back(temp_msg);
		}

		messages.back().MORE_BIT = messages.size();

		for (int i = 0; i < messages.size(); i++) {
			new_msg(messages[i]);
		}
	}
	else {
		messages.push_back(chat(to_friend, message));
	}

	return messages;
}



my_MSG protocol::receive_fragmented_chat(my_MSG msg) {

	if (msg.MORE_BIT == 0 && msg.OFFSET == 0) {
		return msg;
	}
	else if (msg.MORE_BIT == 1) {
		msg.type = "FRAGMENT";
		new_fragment(msg);
		return msg;
	}
	//last of series of fragments, assemble them:
	my_MSG combined_message = msg; 
	combined_message.message = "";

	mut_defrag.lock();
	std::vector<my_MSG>::iterator it = messages_to_defragment.begin();
	int index = 0;
	int last_offset = msg.OFFSET;

	while (index < last_offset) {
		it = messages_to_defragment.begin();
		while (it != messages_to_defragment.end()) {
			if (it->id == msg.id) {
				if (it->OFFSET == index) {
					combined_message.message += it->message;
					it = messages_to_defragment.erase(it);
					index++;
					break;
				}
			}
			it++;
		}
	}
	mut_defrag.unlock();

	combined_message.message += msg.message;
	return combined_message;
}



my_MSG protocol::bye(friend_data bye_friend) {

	my_MSG msg;

	msg.type = "BYE";
	msg.id = getId();
	msg.name = client_info->MY_NAME;
	msg.addr = bye_friend.addr;
	msg.port = bye_friend.port;
	msg.SERVER_MSG = 0;

	return msg;
}


my_MSG protocol::ack(my_MSG msg) {

	msg.type = "ACK";
	return msg;
}


//push message for which we expect reply and defragmenting
void protocol::new_msg(my_MSG msg) {
	mut_msgs.lock();

	messages_pending_reply.push_back(msg);
	last_message = getId();

	mut_msgs.unlock();
}


void protocol::new_fragment(my_MSG msg) {

	mut_defrag.lock();
	messages_to_defragment.push_back(msg);
	mut_defrag.unlock();
}


//remove messages older than 30 seconds.
bool protocol::cleanup() {

	mut_msgs.lock();
	bool something_erased = false;
	std::vector<my_MSG>::iterator it = messages_pending_reply.begin();

	while (it != messages_pending_reply.end()) {

		if ((last_message - it->id) > 30000) {

			it = messages_pending_reply.erase(it);
			something_erased = true;
		}
		else { ++it; }
	}
	mut_msgs.unlock();

	return something_erased;
}



//returns messages that are 5 seconds old
std::vector<my_MSG> protocol::timed_out_msgs() {
	mut_msgs.lock();

	std::vector<my_MSG>::iterator it = messages_pending_reply.begin();
	std::vector<my_MSG> to_resend;

	int current_time = getId();

	while (it != messages_pending_reply.end()) {

		if ((current_time - it->id) > 5000) {
			to_resend.push_back(*it);
		}
		it++;
	}
	mut_msgs.unlock();

	return to_resend;
}


//get message replied to
//cleans up
//returns msg with ID = 0 if no replies found
my_MSG protocol::replied_to(my_MSG msg) {
	my_MSG reply_to = msg;
	reply_to.id = 0;
	mut_msgs.lock();
	for (int i = 0; i < messages_pending_reply.size(); i++) {
		if (msg.id == messages_pending_reply[i].id) {

			reply_to = messages_pending_reply[i];
			messages_pending_reply.erase(messages_pending_reply.begin() + i);
			break;
		}
	}
	mut_msgs.unlock();

	return reply_to;
}


bool protocol::replied(my_MSG msg) {
	my_MSG temp = replied_to(msg);

	if (temp.id != 0) {
		return true;
	}
	return false;
}


my_MSG protocol::error(my_MSG msg , std::string message) {

	my_MSG error_msg;

	error_msg.type = "ERROR";
	error_msg.id = msg.id;
	error_msg.name = client_info->MY_NAME;
	error_msg.addr = msg.addr;
	error_msg.port = msg.port;
	error_msg.message = message;
	error_msg.SERVER_MSG = 0;

	return error_msg;
}