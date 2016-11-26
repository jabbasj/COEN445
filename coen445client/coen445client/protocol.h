#include "allheaders.h"

#define MAX_MESSAGE_LENGTH 140

//structure to be serialized
struct my_MSG
{
	std::string type = "";
	int			id = -1;
	int			port = -1;
	std::string addr = "";
	std::string name = "";
	std::string message = "";
	int			SERVER_MSG = 0;
	int			MORE_BIT = 0;
	int			OFFSET = 0;
};

struct friend_data {
	std::string name = "";
	std::string addr = "";
	int port = -1;
};

struct client_status
{
	std::string MY_NAME = "";
	std::string SERVER_ADDRESS = "";
	int			SERVER_PORT = -1;
	std::string MY_ADDRESS = "";
	int			MY_PORT = -1;			// port attempted, increment by 1 until bind works
	std::vector<friend_data> friends;
};


//wrapper for my_MSG
class protocol {

public:
	protocol(client_status * info) {
		client_info = info;
		last_message = getId();
	}

	my_MSG receive_fragmented_chat(my_MSG msg);
	std::vector<my_MSG> send_fragmented_chat(friend_data to_friend, std::string message);
	my_MSG chat(friend_data to_friend, std::string message);
	my_MSG ack(my_MSG msg);
	my_MSG bye(friend_data bye_friend);
	my_MSG register_me();
	my_MSG register_me(my_MSG);
	my_MSG publish(bool status, bool update_friends, bool expect_reply = true);
	my_MSG inform_req();
	my_MSG find_req(std::string name);
	my_MSG find_req(std::string name, my_MSG);
	friend_data extract_friend_data(my_MSG, std::string name);
	client_status extract_my_info(my_MSG);

	bool replied(my_MSG);
	my_MSG error(my_MSG msg, std::string message);
	std::vector<my_MSG> timed_out_msgs();
	bool cleanup();
	void erase_all();

private:
	std::mutex mut_msgs;
	std::mutex mut_defrag;
	int last_message;
	client_status* client_info;
	std::vector<my_MSG> messages_pending_reply;
	std::vector<my_MSG> messages_to_defragment;

	my_MSG replied_to(my_MSG);
	void new_msg(my_MSG);
	void new_fragment(my_MSG msg);

	// returns unique id: time_since_epoch in milliseconds
	int getId() {
		return abs(int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
	}

};