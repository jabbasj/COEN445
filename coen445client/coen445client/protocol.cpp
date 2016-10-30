#include "protocol.h"


my_MSG protocol::register_me(std::string my_name, std::string IP, int port) {

	my_MSG answer;

	answer.type = "REGISTER";
	answer.id = getId();
	answer.name = my_name;
	answer.addr = IP;
	answer.port = port;
	answer.SERVER_MSG = 0;

	newmsg(answer);

	return answer;
}

//try to register to next server
//todo: fix "return msg"
my_MSG protocol::register_me(my_MSG msg) {

	my_MSG reply_to = replied_to(msg);

	if (reply_to.id != 0) {

		if (msg.type == "REGISTER-DENIED") {	
		
			std::string next_serv = msg.message.substr(0, msg.message.find(":"));
			std::string next_port = msg.message.substr(msg.message.find(":") + 1, -1);

			return register_me(msg.name, next_serv, stoi(next_port));
		}
	}

	return reply_to;
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