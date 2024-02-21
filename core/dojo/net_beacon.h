#pragma once

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#define _WINSOCK_DEPRECATED_NO_WARNINGS 1

#include <Winsock2.h> // before Windows.h, else Winsock 1 conflict
#include <Ws2tcpip.h> // needed for ip_mreq definition for multicast
#include <Windows.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>
#endif

#define SOCKET_ERROR -1

#include <map>

#include <cstring>
#include <cstdio>
#include <cstdlib>

#include "stdclass.h"
#include "cfg/option.h"

#include <iostream>
#include <sstream>

#include <thread>
#include <chrono>

#include "deps/base64.h"

#define MSGBUFSIZE 256

class NetBeacon
{
public:
	void BeaconThread();
	void ListenerThread();

	uint64_t unix_timestamp();

	std::map<std::string, std::string> active_beacons;
	std::map<std::string, uint64_t> last_seen;

	bool beacon_active;
	bool lobby_active;

	void Close();

	std::string client_seed;

private:
	int beacon_sock;
	int Init();
	void CloseSocket(int sock);
	int BeaconLoop(sockaddr_in addr, int delay_secs);
	sockaddr_in SetDestination(char *group, short port);
	int beacon(char *group, int port, int delay_secs);

	int listener_sock;
	int ListenerLoop(sockaddr_in addr);
	int listener(char *group, int port);

	unsigned long mix(unsigned long a, unsigned long b, unsigned long c);
	std::string random_hex_string(int length, int seed);
};
