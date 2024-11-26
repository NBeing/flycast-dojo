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

    void PingThread();

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

    uint64_t RepeatTargetPing(std::string target, int num_requests);
    std::string GetTargetStr(std::string target);
    uint64_t GetTargetAvgPing(std::string target);

    std::string AssignClosestRelay();
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

    std::map<std::string, std::deque<std::string>> target_ping_msgs;

    struct U64DefaultToZero
    {
        uint64_t i = 0;
    };
    std::map<std::string, U64DefaultToZero> target_ping_test_start;

    std::map<std::string, std::map<int, uint64_t>> target_ping_send_ts;
    std::map<std::string, std::vector<uint64_t>> target_ping_rtt;
    std::map<std::string, uint64_t> target_avg_ping_ms;

    sockaddr_in target_addr;
    std::string target_server;
    int target_port;

    int PingTarget(std::string target, int add_to_seed);
};
