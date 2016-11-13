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

struct client_data {
	std::string name = "";
	std::string status = "";
	std::string addr = "";
	int			port = -1;
	std::vector<std::string> friends;
};

struct server_status
{
	std::string MY_NAME = "";
	std::string MY_ADDRESS = "";
	std::string NEXT_ADDRESS = "";
	int			MY_PORT = -1;						//The port on which to listen for incoming data (server 0)
	int			NEXT_PORT = -1;						//The port of the next known server
	std::vector<client_data> clients_registered;
};


//wrapper for my_MSG
class protocol {

public:
	protocol(server_status * info) {
		server_info = info;
		last_message = getId();
	}

public:
	my_MSG register_client(my_MSG msg);
	my_MSG deny_register(my_MSG msg);
	my_MSG is_registered_query(my_MSG msg);
	my_MSG is_registered_query_answer(my_MSG msg);
	my_MSG published(my_MSG msg);
	my_MSG unpublished(my_MSG msg);
	my_MSG inform_resp(my_MSG msg, client_data client);
	my_MSG find_resp(my_MSG msg, client_data client);
	my_MSG find_denied(my_MSG msg);
	my_MSG refer(my_MSG msg);
	my_MSG error(my_MSG msg, std::string message);

private:
	std::mutex mut_msgs;
	int last_message;
	server_status* server_info;
	std::vector<my_MSG> messages_pending_reply;

	void newmsg(my_MSG);
	void cleanup();

	my_MSG replied_to(my_MSG);

	int getId() {
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}
};