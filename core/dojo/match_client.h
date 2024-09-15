#pragma once

#include "types.h"

#include "dojo.h"
#include "network/net_platform.h"

class MatchClient
{
public:
    void ClientThread();
    void ConnectMMServer();

private:
    sockaddr_in host_addr;
    sockaddr_in opponent_addr;

    sock_t local_socket = INVALID_SOCKET;
    void CloseSocket(sock_t &socket) const
    {
        closesocket(socket);
        socket = INVALID_SOCKET;
    }

    sock_t CreateAndBind(int port);
    bool CreateLocalSocket(int port);

    bool Init(bool hosting);
    void ClientLoop();

    bool isLoopStarted;
    bool request_repeat;

    int packets_per_frame = 5;
};
