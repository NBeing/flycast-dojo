#pragma once

#include <chrono>
#include <queue>
#include <deque>
#include <string>
#include <vector>

#include "deps/easywsclient/easywsclient.hpp"
#include "deps/UUID.hpp"
#include "deps/json.hpp"
#include "deps/picosha2.h"
#include "deps/date/date.h"
#include "deps/date/tz.h"

#include "dojo.h"

class QuickMatch
{
private:
	void quick_match_thread();

public:
	bool thread_started;

	void StartThread();
	void ProcessMsg(std::string msg);

	void StopThread();
	void Clear();

	void SendKeyMsg(std::string cxn_method, std::string server, int port, std::string key);

	std::string GetGravatarUrl(std::string email_sha);

	bool downloadImage(const std::string &url, const std::string &localName);

	void AppendToLog(std::string msg);

	int current_match_status_idx = 0;
	int last_match_status_idx = 2;

	std::string client_uuid = "";

	struct QuickMatchMsg
	{
		std::string uuid;
		std::string type;
		std::string game_name;
		std::string player_name;
		std::string cxn_method;
		std::string server;
		std::string port;
		std::string status;
		std::string email_sha;
		std::string location;
		std::string country_code;
	};

	std::vector<QuickMatchMsg> players;
	std::vector<QuickMatchMsg> requests;
	std::vector<QuickMatchMsg> rejects;
	std::deque<std::string> outgoing_msgs;

	std::vector<QuickMatchMsg> pending;

	struct LogEntry
	{
		std::string timestamp;
		std::string msg;
	};

	std::deque<LogEntry> log;

	bool thread_stopped = false;
	bool start_game = false;
	bool host_ready = false;

	std::string target_player;

	std::vector<std::string> requests_to_remove;
	std::vector<std::string> pending_requests_to_remove;
};

extern QuickMatch quick_match;