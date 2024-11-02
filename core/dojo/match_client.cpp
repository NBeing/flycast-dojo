#include "dojo.h"

// connects to matchmaking server
void MatchClient::ConnectMMServer()
{
	struct hostent *mm_host;
	mm_host = gethostbyname(config::MatchCodeServer.get().data());

	sockaddr_in mms_addr;
	mms_addr.sin_family = AF_INET;
	mms_addr.sin_port = htons((u16)std::stoul(config::MatchCodePort));
	memcpy(&mms_addr.sin_addr, mm_host->h_addr_list[0], mm_host->h_length);
	// inet_pton(AF_INET, config::MatchCodeServer.data(), &mms_addr.sin_addr);

	std::string mm_msg;
	if (dojo.hosting)
		mm_msg = "host:cmd:";
	else
	{
		while (dojo.match_code.empty() && !dojo.disconnect_toggle)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));

		if (dojo.disconnect_toggle)
			return;

		mm_msg = "guest:cmd:" + dojo.match_code;
	}

	for (int i = 0; i < packets_per_frame; i++)
	{
		sendto(local_socket, (const char *)mm_msg.data(), strlen(mm_msg.data()), 0, (const struct sockaddr *)&mms_addr, sizeof(mms_addr));
	}
	INFO_LOG(NETWORK, "Connecting to Matchmaking Relay");
}

sock_t MatchClient::CreateAndBind(int port)
{
	sock_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (!VALID(sock))
	{
		ERROR_LOG(NETWORK, "Cannot create server socket");
		return sock;
	}
	int option = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&option, sizeof(option));

	struct sockaddr_in serveraddr;
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons((u16)port);

	if (::bind(sock, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
	{
		ERROR_LOG(NETWORK, "DojoSession UDP Server: bind() failed. errno=%d", get_last_error());
		CloseSocket(sock);
	}
	else
		set_non_blocking(sock);

	return sock;
}

bool MatchClient::CreateLocalSocket(int port)
{
	if (!VALID(local_socket))
		local_socket = CreateAndBind(port);

	return VALID(local_socket);
}

bool MatchClient::Init(bool hosting)
{
#ifdef _WIN32
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0)
	{
		ERROR_LOG(NETWORK, "WSAStartup failed. errno=%d", get_last_error());
		return false;
	}
#endif

	if (hosting)
	{
		return CreateLocalSocket(stoi(config::DojoServerPort));
	}
	else
	{
		config::DojoServerIP = "";
		opponent_addr = host_addr;
		return CreateLocalSocket(0);
	}
}

void MatchClient::ClientLoop()
{
	isLoopStarted = true;

	while (!dojo.disconnect_toggle)
	{
		struct sockaddr_in sender;
		socklen_t senderlen = sizeof(sender);
		char buffer[256];
		memset(buffer, 0, 256);
		int bytes_read = recvfrom(local_socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&sender, &senderlen);
		if (bytes_read)
		{
			if (memcmp("CODE", buffer, 4) == 0)
			{
				dojo.match_code = std::string(buffer + 5, strlen(buffer + 5));
				if (quick_match.start_game)
				{
					quick_match.SendKeyMsg("match_code", config::MatchCodeServer.get(), std::stoi(config::MatchCodePort.get()), dojo.match_code);
					//std::this_thread::sleep_for(std::chrono::seconds(5));
					//quick_match.StopThread();
				}
			}

			if (memcmp("OPPADDR", buffer, 7) == 0)
			{
				std::string data = std::string(buffer + 8, strlen(buffer + 5));
				std::vector<std::string> opp;
				dojo.Split(data, ':', opp);

				config::NetworkServer = opp[0];

				opponent_addr.sin_family = AF_INET;
				opponent_addr.sin_port = htons((u16)std::stol(opp[1]));
				inet_pton(AF_INET, opp[0].data(), &opponent_addr.sin_addr);

				if (!config::ActAsServer.get())
				{
					host_addr.sin_port = htons((u16)std::stol(opp[1]));
					inet_pton(AF_INET, opp[0].data(), &host_addr.sin_addr);
				}
			}

			if (memcmp("START", buffer, 5) == 0)
			{
				return;
			}
		}
	}
}

void MatchClient::ClientThread()
{
	Init(dojo.hosting);

	if (!config::ActAsServer)
		while (dojo.match_code.empty() && !dojo.disconnect_toggle)
			;

	ConnectMMServer();
	ClientLoop();
	CloseSocket(local_socket);
}
