#include "network/net_platform.h"

#include "dojo.h"

class TcpClient
{
private:
	std::string host;
	int port;

	sock_t sock = INVALID_SOCKET;
	sockaddr_in host_addr;

	unsigned char to_send[4096];

	bool Init();
	bool CreateSocket();
	void Connect();
	void Disconnect();

	bool isLoopStarted;
	void TransmissionLoop();

	bool endSession;

	void ReceivingInit();
	void ReceivingLoop();

	void CloseSocket(sock_t &socket) const
	{
		closesocket(socket);
		socket = INVALID_SOCKET;
	}

public:
	TcpClient();

	void TransmissionThread();
	void ReceivingThread();
	void Stop();

	bool transmitter_started;
	bool receiver_started;

	std::queue<std::string> outgoing_msgs;
	std::queue<std::string> incoming_msgs;
};
