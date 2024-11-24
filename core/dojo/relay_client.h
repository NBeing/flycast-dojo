#pragma once

#include <chrono>
#include <deque>
#include <iostream>
#include <thread>

#include "cfg/cfg.h"
#include "cfg/option.h"
#include "types.h"
#include "network/net_platform.h"

#include "dojo.h"

class RelayClient
{
public:
    void ClientThread();
    bool Init();
    void ConnectRelayServer();

    void AddToRelayAddressHistory(std::string address);
    std::vector<std::string> GetRelayAddressHistory();

    bool connect_started;
    bool disconnect_toggle;
    std::string target_hostname;
    bool start_game = false;
    bool key_shown;

    std::deque<std::string> outgoing_msgs;

    void SendHostMsg();
    void SendGuestMsg();

private:
    sock_t local_socket = INVALID_SOCKET;
    void CloseSocket(sock_t &socket) const
    {
        closesocket(socket);
        socket = INVALID_SOCKET;
    }

    sockaddr_in mms_addr;

    sock_t CreateAndBind(int port);
    bool CreateLocalSocket(int port);

    void ClientLoop();

    bool isLoopStarted;
    bool request_repeat;

    std::map<int, uint64_t> ping_send_ts;
    std::vector<uint64_t> ping_rtt;
    uint64_t avg_ping_ms;

    std::string RandomHexString(int length, int seed);

    std::deque<std::string> ping_msgs;
    uint64_t ping_test_start = 0;
    bool hole_punched = false;

    sockaddr_in opponent_addr;
    std::string opponent_server;
    int opponent_port;

    int PingOpponent(int add_to_seed);
    uint64_t GetOpponentAvgPing(int num_requests);
};
