#include "protocol.h"


my_MSG protocol::register_me() {

	my_MSG answer;

	answer.type = "REGISTER";
	answer.id = getId();
	answer.name = client_info->MY_NAME;
	answer.addr = client_info->SERVER_ADDRESS;
	answer.port = client_info->SERVER_PORT;
	answer.SERVER_MSG = 0;

	newmsg(answer);

	return answer;
}

//try to register to next server
my_MSG protocol::register_me(my_MSG msg) {

	my_MSG reply_to = replied_to(msg);

	if (reply_to.id != 0) {

		if (msg.type == "REGISTER-DENIED") {	
		
			client_info->SERVER_ADDRESS = msg.message.substr(0, msg.message.find(":"));
			client_info->MY_PORT = stoi(msg.message.substr(msg.message.find(":") + 1, -1));

			return register_me();
		}
	}
	return reply_to;
}

my_MSG protocol::publish() {

	my_MSG answer;

	answer.type = "PUBLISH";
	answer.id = getId();
	answer.name = client_info->MY_NAME;
	answer.addr = client_info->SERVER_ADDRESS;
	answer.port = client_info->SERVER_PORT;

	std::string friends = "";

	for (int i = 0; i < client_info->friends.size(); i++) {
		friends += client_info->friends[i].name + ",";
	}
	friends = friends.substr(0, friends.find_last_of(","));

	answer.message = friends;

	newmsg(answer);

	return answer;
}

//push message for which we expect reply
void protocol::newmsg(my_MSG msg) {
	mut_msgs.lock();

	messages_pending_reply.push_back(msg);
	last_message = getId();

	mut_msgs.unlock();
}

//remove messages older than 1 minute.
void protocol::cleanup() {
	mut_msgs.lock();
	std::vector<my_MSG>::iterator it = messages_pending_reply.begin();

	while (it != messages_pending_reply.end()) {

		if ((last_message - it->id) > 60000) {

			it = messages_pending_reply.erase(it);
		}
		else ++it;
	}
	mut_msgs.unlock();
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

	cleanup();

	return reply_to;
}