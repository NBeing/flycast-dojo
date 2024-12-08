#include "dojo.h"

TcpClient::TcpClient()
{
	isLoopStarted = false;
	endSession = false;
}

bool TcpClient::Init()
{
	isLoopStarted = false;
	endSession = false;

#ifdef _WIN32
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0)
	{
		ERROR_LOG(NETWORK, "WSAStartup failed. errno=%d", get_last_error());
		return false;
	}
#endif

	return CreateSocket();
}

bool TcpClient::CreateSocket()
{
	if (VALID(sock))
		return true;

	sock = socket(AF_INET, SOCK_STREAM, 0);

	return true;
}

void TcpClient::Connect()
{

	struct sockaddr_in host_addr;
	memset(&host_addr, 0, sizeof(host_addr));

	host_addr.sin_family = AF_INET;
	host_addr.sin_port = htons((u16)port);
	inet_pton(AF_INET, host.data(), &host_addr.sin_addr);

	if (::connect(sock, (struct sockaddr *)&host_addr, sizeof(host_addr)) < 0)
	{
		ERROR_LOG(NETWORK, "Socket connect failed");
		CloseSocket(sock);
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

void TcpClient::Disconnect()
{
	CloseSocket(sock);
#ifdef _WIN32
	WSACleanup();
#endif
}

void TcpClient::Stop()
{
	endSession = true;
}

void TcpClient::TransmissionLoop()
{
	isLoopStarted = true;
	std::string last_sent_msg = "";

	char buf[4096];

	while (!endSession)
	{
		if (!outgoing_msgs.empty())
		{
			last_sent_msg = outgoing_msgs.front();
			memcpy((void *)to_send, last_sent_msg.data(), last_sent_msg.size());

			int sendResult = sendto(sock, (const char *)to_send, last_sent_msg.size(), 0, (const struct sockaddr *)&host_addr, sizeof(host_addr));
			if (VALID(sendResult))
			{
				outgoing_msgs.pop();
				memset(to_send, 0, 4096);
			}
		}
	}
}

void TcpClient::TransmissionThread()
{
	host = config::SpectatorIP;
	port = stoi(config::SpectatorPort);

	Init();
	Connect();
	TransmissionLoop();
	Disconnect();
}

void TcpClient::ReceivingInit()
{
	MessageWriter spectate_request;

	spectate_request.AppendHeader(1, SPECTATE_REQUEST);

	spectate_request.AppendString(config::Quark.get());
	spectate_request.AppendString(config::SpectateKey.get());
	spectate_request.AppendString(dojo.game_name);

	std::vector<unsigned char> message = spectate_request.Msg();

	memcpy((void *)to_send, message.data(), message.size());
	int sendResult = sendto(sock, (const char *)to_send, message.size(), 0, (const struct sockaddr *)&host_addr, sizeof(host_addr));
	if (VALID(sendResult))
	{
		memset(to_send, 0, 256);
	}
}

void TcpClient::ReceivingLoop()
{
	isLoopStarted = true;
	char buf[4096];
	memset(buf, 0, 4096);

	while (!endSession)
	{
		int headerBytesReceived = recv(sock, buf, HEADER_LEN, 0);
		if (headerBytesReceived > 0)
		{
			u32 body_size = HeaderReader::GetSize((u8 *)buf);
			u32 seq = HeaderReader::GetSeq((u8 *)buf);
			u32 cmd = HeaderReader::GetCmd((u8 *)buf);

			memset(buf, 0, 4096);
			int bodyBytesReceived = recv(sock, buf, body_size, 0);

			int offset = 0;
			dojo.ProcessBody(cmd, body_size, (const char *)buf, &offset);
			memset(buf, 0, 4096);
		}
	}
}

void TcpClient::ReceivingThread()
{
	host = cfgLoadStr("dojo", "SpectatorIP", "");
	port = stoi(cfgLoadStr("dojo", "SpectatorPort", ""));

	Init();
	Connect();
	ReceivingInit();
	ReceivingLoop();
	Disconnect();
}
