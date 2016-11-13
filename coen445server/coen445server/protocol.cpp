
#include "protocol.h"



my_MSG protocol::register_client(my_MSG msg) {	

	msg.type = "REGISTERED";
	msg.SERVER_MSG = 1;

	return msg;
}


my_MSG protocol::deny_register(my_MSG msg) {

	msg.type = "REGISTER-DENIED";
	msg.message = server_info->NEXT_ADDRESS + ":" + std::to_string(server_info->NEXT_PORT);
	msg.SERVER_MSG = 1;

	return msg;
}


//asks next server if this user is registered to it
my_MSG protocol::is_registered_query(my_MSG msg) {

	msg.type = "IS-REGISTERED";
	msg.message = "no";

	newmsg(msg);

	msg.addr = server_info->NEXT_ADDRESS;
	msg.port = server_info->NEXT_PORT;
	msg.SERVER_MSG = 1;

	return msg;
}


my_MSG protocol::published(my_MSG msg) {

	msg.type = "PUBLISHED";
	msg.SERVER_MSG = 1;

	return msg;
}


my_MSG protocol::unpublished(my_MSG msg) {

	msg.type = "UNPUBLISHED";
	msg.SERVER_MSG = 1;

	return msg;
}


my_MSG protocol::inform_resp(my_MSG msg, client_data client) {

	my_MSG answer;

	answer.type = "INFORMResp";
	answer.id = msg.id;
	answer.name = msg.name;
	answer.addr = msg.addr;
	answer.port = msg.port;
	answer.message = "status:{" + client.status + "}addr:{" + client.addr + "}port:{" + std::to_string(client.port) + "}friends:{";
	for (int i = 0; i < client.friends.size(); i++) {
		answer.message += client.friends[i] + ",";
	}
	answer.message += "}";
	answer.SERVER_MSG = 1;

	return answer;
}


my_MSG protocol::find_resp(my_MSG msg, client_data client) {

	my_MSG answer;
	answer.type = "FINDResp";
	answer.id = msg.id;
	answer.name = msg.name;
	answer.addr = msg.addr;
	answer.port = msg.port;
	answer.SERVER_MSG = 1;

	bool on_friends_list = false;
	for (int i = 0; i < client.friends.size(); i++) {
		if (client.friends[i] == msg.name) {
			on_friends_list = true;
			break;
		}
	}

	if (!on_friends_list) { return find_denied(msg); }

	if (client.status == "on") {
		answer.message = client.addr + ":" + std::to_string(client.port);
	}
	else if (client.status == "off") {
		answer.message = "off";
	}

	return answer;
}


my_MSG protocol::refer(my_MSG msg) {
	my_MSG answer = deny_register(msg);
	answer.type = "REFER";

	return answer;
}


my_MSG protocol::find_denied(my_MSG msg) {

	msg.type = "FINDDenied";
	msg.SERVER_MSG = 1;

	return msg;
}


//checks if we got replied to, check if client registered to me, pass to next server (to go in circle)
my_MSG protocol::is_registered_query_answer(my_MSG msg) {

	my_MSG reply_to = replied_to(msg);
	if (reply_to.id != 0) {
		if (msg.message == "yes") {
			return deny_register(reply_to);
		}
		else {
			return register_client(reply_to);
		}
	}

	msg.addr = server_info->NEXT_ADDRESS;
	msg.port = server_info->NEXT_PORT;

	if (msg.message == "no") {
		bool registered = false;
		for (int i = 0; i < server_info->clients_registered.size(); i++) {
			if (msg.name == server_info->clients_registered[i].name) {
				registered = true;
				break;
			}
		}

		if (registered) {
			msg.message = "yes";
		}
	}

	return msg;
}


//get message replied to
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

//push message for which we expect reply
void protocol::newmsg(my_MSG msg) {
	mut_msgs.lock();

	messages_pending_reply.push_back(msg);
	last_message = getId();

	mut_msgs.unlock();
}


my_MSG protocol::error(my_MSG msg, std::string message) {

	my_MSG error_msg;

	error_msg.type = "ERROR";
	error_msg.id = msg.id;
	error_msg.name = server_info->MY_NAME;
	error_msg.addr = msg.addr;
	error_msg.port = msg.port;
	error_msg.message = message;
	error_msg.SERVER_MSG = 1;

	return error_msg;
}