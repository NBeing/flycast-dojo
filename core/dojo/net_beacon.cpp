#include "net_beacon.h"

uint64_t NetBeacon::unix_timestamp()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// http://www.concentric.net/~Ttwang/tech/inthash.htm
unsigned long NetBeacon::mix(unsigned long a, unsigned long b, unsigned long c)
{
	a=a-b;  a=a-c;  a=a^(c >> 13);
	b=b-c;  b=b-a;  b=b^(a << 8);
	c=c-a;  c=c-b;  c=c^(b >> 13);
	a=a-b;  a=a-c;  a=a^(c >> 12);
	b=b-c;  b=b-a;  b=b^(a << 16);
	c=c-a;  c=c-b;  c=c^(b >> 5);
	a=a-b;  a=a-c;  a=a^(c >> 3);
	b=b-c;  b=b-a;  b=b^(a << 10);
	c=c-a;  c=c-b;  c=c^(b >> 15);
	return c;
}

std::string NetBeacon::random_hex_string(int length, int seed)
{
	srand(seed);

	char hex_out[1024] = { 0 };
	char hex_chars[] =
		{'0','1','2','3','4','5','6','7',
		 '8','9','A','B','C','D','E','F'};

	for (int i = 0; i < length; i++)
	{
		hex_out[i] = hex_chars[rand() % 16];
	}

	std::string x_out(hex_out, strlen(hex_out));

	return x_out;
}

char *get_ip_str(const struct sockaddr *sa, char *s, size_t maxlen)
{
	switch (sa->sa_family)
	{
	case AF_INET:
		inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr),
				  s, maxlen);
		break;

	case AF_INET6:
		inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),
				  s, maxlen);
		break;

	default:
		memcpy(s, "Unknown AF", strlen("Unknown AF") + 1);
		// strncpy_s(s, strlen("Unknown AF"), "Unknown AF", maxlen);
		return NULL;
	}

	return s;
}

void NetBeacon::BeaconThread()
{
	beacon((char *)config::BeaconMulticastAddress.get().c_str(), std::stoi(config::BeaconMulticastPort.get()), 5);
}

int NetBeacon::Init()
{
#ifdef _WIN32
	// initialize winsock
	WSADATA wsaData;
	if (WSAStartup(0x0101, &wsaData))
	{
		perror("WSAStartup");
		return 1;
	}
#endif

	// create UDP socket
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
	{
		perror("socket");
		return 1;
	}

	return fd;
}

void NetBeacon::CloseSocket(int sock)
{

#ifdef _WIN32
	closesocket(sock);
	// shut down winsock cleanly
	// WSACleanup();
#else
	close(sock);
#endif
}

int NetBeacon::BeaconLoop(sockaddr_in addr, int delay_secs)
{
	std::string status;
	std::string data;

	const char *message;
	std::string message_str = "2001_" + config::PlayerName.get() + "_" + client_seed;
	std::string encoded_message_str = to_base64(message_str);
	message = encoded_message_str.data();

	// sendto() destination
	while (beacon_active)
	{
		int nbytes = sendto(
			beacon_sock,
			message,
			strlen(message),
			0,
			(struct sockaddr *)&addr,
			sizeof(addr));
		if (nbytes < 0)
		{
			perror("sendto");
			return 1;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(delay_secs * 1000));
	}

	return 0;
}

sockaddr_in NetBeacon::SetDestination(char *group, short port)
{
	// set up destination address
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	if (group == 0)
		addr.sin_addr.s_addr = htonl(INADDR_ANY); // differs from sender
	else
		addr.sin_addr.s_addr = inet_addr(group);
	addr.sin_port = htons(port);

	return addr;
}

int NetBeacon::beacon(char *group, int port, int delay_secs)
{
	if (client_seed == "")
	{
		unsigned long seed = mix(clock(), time(NULL), getpid());
		client_seed = random_hex_string(20, seed);
	}

	beacon_sock = Init();

	sockaddr_in addr = SetDestination(group, port);

	BeaconLoop(addr, delay_secs);
	CloseSocket(beacon_sock);

	return 0;
}

void NetBeacon::ListenerThread()
{
	listener((char *)config::BeaconMulticastAddress.get().c_str(), std::stoi(config::BeaconMulticastPort.get()));
}

int NetBeacon::ListenerLoop(sockaddr_in addr)
{
	while (lobby_active)
	{
		char msgbuf[MSGBUFSIZE];
		char ip_str[128];

		int addrlen = sizeof(addr);
		int nbytes = recvfrom(
			listener_sock,
			msgbuf,
			MSGBUFSIZE,
			0,
			(struct sockaddr *)&addr,
			(socklen_t *)&addrlen);
		if (nbytes < 0)
		{
			perror("recvfrom");
			return 1;
		}
		msgbuf[nbytes] = '\0';

		auto incoming_msg = std::string(msgbuf);
		auto decoded_msg = from_base64(incoming_msg);

		get_ip_str((struct sockaddr *)&addr, ip_str, 128);

		if (memcmp(decoded_msg.data(), "2001_", strlen("2001_")) == 0)
		{
			std::string beacon_id = decoded_msg.substr(5);

			if (active_beacons.count(beacon_id) == 0)
			{
				auto new_beacon = std::pair<std::string, std::string>(beacon_id, std::string(ip_str));
				active_beacons.insert(new_beacon);
			}
			else
			{
				try
				{
					if (active_beacons.at(beacon_id) != ip_str)
						active_beacons.at(beacon_id) = ip_str;
				}
				catch (...)
				{
				};
			}

			last_seen[beacon_id] = unix_timestamp();
		}
	}

	return 0;
}

int NetBeacon::listener(char *group, int port)
{
	listener_sock = Init();

	// allow multiple sockets to use the same port number
	u_int yes = 1;
	if (
		setsockopt(
			listener_sock, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, sizeof(yes)) < 0)
	{
		perror("Reusing ADDR failed");
		return 1;
	}

	sockaddr_in addr = SetDestination(0, port);

	// bind to receive address
	if (bind(listener_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		perror("bind");
		return 1;
	}

	// use setsockopt() to request that the kernel join a multicast group
	struct ip_mreq mreq;
	mreq.imr_multiaddr.s_addr = inet_addr(group);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	if (
		setsockopt(
			listener_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0)
	{
		perror("setsockopt");
		return 1;
	}

	ListenerLoop(addr);
	CloseSocket(listener_sock);

	return 0;
}

void NetBeacon::Close()
{
	beacon_active = false;
	lobby_active = false;

	CloseSocket(beacon_sock);
	CloseSocket(listener_sock);
}
