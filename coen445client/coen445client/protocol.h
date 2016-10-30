#include "allheaders.h"

//structure sent down the wire, cast to a char*
struct my_MSG
{
	std::string type = "";
	int			id = -1;
	int			port = -1;
	std::string addr = "";
	std::string name = "";
	std::string message = "";
	int			SERVER_MSG = 0;
};

//wrapper for my_MSG
class protocol {

public:
	my_MSG register_me(std::string my_name, std::string IP, int port);
	my_MSG protocol::register_me(my_MSG);
	my_MSG bye();

private:
	std::mutex mut_msgs;
	int last_message;
	std::vector<my_MSG> messages_pending_reply;

	my_MSG replied_to(my_MSG);
	void newmsg(my_MSG);
	void cleanup();

	int getId() {
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

};