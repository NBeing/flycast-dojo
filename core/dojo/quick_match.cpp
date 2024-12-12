#pragma once

#include "dojo.h"

#include "rend/boxart/http_client.h"
QuickMatch quick_match;

void QuickMatch::StartThread()
{
	if (thread_stopped)
	{
		thread_started = false;
		thread_stopped = false;
	}

	if (thread_started)
		return;

	std::thread t4(&QuickMatch::quick_match_thread, std::ref(quick_match));
	t4.detach();

	thread_started = true;
}

bool QuickMatch::Active()
{
	return thread_started;
}

void QuickMatch::StopThread()
{
	thread_stopped = true;
}

void QuickMatch::Clear()
{
	pending.clear();
	requests.clear();
	rejects.clear();
	outgoing_msgs.clear();
	players.clear();
	log.clear();
	quick_match.client_uuid = "";
	current_match_status_idx = 1;
	thread_started = false;
	thread_stopped = false;
}

void QuickMatch::SendKeyMsg(std::string cxn_method, std::string server, int port, std::string key)
{
	auto key_msg = nlohmann::json{
		{"type", "key"},
		{"uuid", quick_match.target_player},
		{"cxn_method", cxn_method},
		{"server", server},
		{"port", std::to_string(port)},
		{"key", key}};

	outgoing_msgs.push_back(key_msg.dump());
}

std::string QuickMatch::GetGravatarUrl(std::string email_sha)
{
	std::string url = "https://gravatar.com/avatar/" + email_sha + "?d=identicon";
	return url;
}

bool QuickMatch::downloadImage(const std::string &url, const std::string &localName)
{
	DEBUG_LOG(COMMON, "downloading %s", url.c_str());
	std::vector<u8> content;
	std::string contentType;
	if (!http::success(http::get(url, content, contentType)))
	{
		WARN_LOG(COMMON, "downloadImage http error: %s", url.c_str());
		return false;
	}
	if (contentType.substr(0, 6) != "image/")
	{
		WARN_LOG(COMMON, "downloadImage bad content type %s", contentType.c_str());
		return false;
	}
	if (content.empty())
	{
		WARN_LOG(COMMON, "downloadImage: empty content");
		return false;
	}
	FILE *f = nowide::fopen(localName.c_str(), "wb");
	if (f == nullptr)
	{
		WARN_LOG(COMMON, "can't create local file %s: error %d", localName.c_str(), errno);
		return false;
	}
	fwrite(&content[0], 1, content.size(), f);
	fclose(f);

	return true;
}

void QuickMatch::quick_match_thread()
{
	using easywsclient::WebSocket;

	std::string quick_match_server = "ws://" + config::QuickMatchServer.get() + ":" + config::QuickMatchPort.get() + "/";
	std::unique_ptr<WebSocket> ws(WebSocket::from_url(quick_match_server));
	assert(ws);

	while (ws->getReadyState() != WebSocket::CLOSED)
	{
		if (current_match_status_idx != last_match_status_idx)
		{
			if (current_match_status_idx == 0)
			{
				if (quick_match.client_uuid == "")
				{
					quick_match.client_uuid = UuidGen::generate_uuid();

					std::string avatar_sha;
					if (config::PlayerEmail.get().empty())
						picosha2::hash256_hex_string(config::PlayerName.get(), avatar_sha);
					else
						picosha2::hash256_hex_string(config::PlayerEmail.get(), avatar_sha);

					auto player_msg = nlohmann::json{
						{"type", "player"},
						{"game_name", dojo.game_name},
						{"player_name", config::PlayerName.get()},
						{"cxn_method", config::QMCxnMethod.get()},
						{"server", config::RelayServer.get()},
						{"port", std::to_string(config::RelayPort.get())},
						{"uuid", quick_match.client_uuid},
						{"status", "active"},
						{"email_sha", avatar_sha}};

					ws->send(player_msg.dump());
					NOTICE_LOG(NETWORK, "%s", player_msg.dump().data());
				}
				else
				{
					SendStatusMsg("active");
				}
			}
			else if (current_match_status_idx == 1)
			{
				SendStatusMsg("away");
			}
			last_match_status_idx = current_match_status_idx;
		}

		WebSocket::pointer wsp = &*ws; // unique_ptr cannot be copied into a lambda
		ws->poll();
		ws->dispatch([wsp](const std::string &message)
					 { quick_match.ProcessMsg(message); });

		while (outgoing_msgs.size() > 0)
		{
			std::string out_msg = outgoing_msgs.front();
			ws->send(out_msg);
			outgoing_msgs.pop_front();
		}

		if (thread_stopped)
		{
			wsp->close();
			Clear();
		}
	}

	return;
}

void QuickMatch::SendStatusMsg(std::string status)
{
	auto status_msg = nlohmann::json{
		{"type", "status"},
		{"status", status}};

	outgoing_msgs.push_back(status_msg.dump());
	NOTICE_LOG(NETWORK, "%s", status_msg.dump().data());
}

void QuickMatch::ProcessMsg(std::string msg)
{
	auto parsed_json = nlohmann::json::parse(msg);
	if (parsed_json["type"] == "player")
	{
		QuickMatchMsg player_entry{
			parsed_json["uuid"].get<std::string>(),
			parsed_json["type"].get<std::string>(),
			parsed_json["game_name"].get<std::string>(),
			parsed_json["player_name"].get<std::string>(),
			parsed_json["cxn_method"].get<std::string>(),
			parsed_json["server"].get<std::string>(),
			parsed_json["port"].get<std::string>(),
			parsed_json["status"].get<std::string>(),
			parsed_json["email_sha"].get<std::string>(),
			parsed_json["location"].get<std::string>(),
			parsed_json["country_code"].get<std::string>()};

		players.push_back(player_entry);

		std::string gravatar_id = player_entry.email_sha;
		std::string profile_fn = dojo_file.DownloadFile(GetGravatarUrl(gravatar_id), "avatar", 0, "");
		std::cout << "Profile: " << profile_fn << std::endl;
	}
	else if (parsed_json["type"] == "status")
	{
		std::string uuid = parsed_json["uuid"];
		std::string status = parsed_json["status"];
		auto player = std::find_if(
			players.begin(), players.end(),
			[uuid](QuickMatchMsg p)
			{ return p.uuid == uuid; });
		if (player != players.end())
			(*player).status = status;
	}
	else if (parsed_json["type"] == "remove")
	{
		std::string target_id = parsed_json["uuid"];
		std::cout << "Removing " << target_id << std::endl;
		players.erase(
			std::remove_if(
				players.begin(), players.end(),
				[target_id](QuickMatchMsg p)
				{ return p.uuid == target_id; }),
			players.end());
	}
	else if (parsed_json["type"] == "request")
	{
		std::string request_msg = parsed_json["player_name"].get<std::string>() + " challenged you";
		AppendToLog(request_msg);

		QuickMatchMsg request_entry{
			parsed_json["uuid"].get<std::string>(),
			parsed_json["type"].get<std::string>(),
			parsed_json["game_name"].get<std::string>(),
			parsed_json["player_name"].get<std::string>(),
			parsed_json["cxn_method"].get<std::string>(),
			parsed_json["server"].get<std::string>(),
			parsed_json["port"].get<std::string>()};

		requests.push_back(request_entry);
	}
	else if (parsed_json["type"] == "accept")
	{
		std::string opponent_name = parsed_json["player_name"].get<std::string>();
		std::string accept_msg = opponent_name + " accepted your challenge";
		AppendToLog(accept_msg);
		target_player = parsed_json["uuid"];

		settings.dojo.OpponentName = opponent_name;

		if (parsed_json["cxn_method"] == "relay")
		{
			// revoke pending requests
			for (auto req : pending)
			{
				if (req.uuid == target_player)
					continue;

				auto cancel_msg = nlohmann::json{
					{"type", "revoke"},
					{"uuid", req.uuid}};

				quick_match.outgoing_msgs.push_back(cancel_msg.dump());
				quick_match.pending_requests_to_remove.push_back(req.uuid);
			}

			// delete remaining pending request
			quick_match.pending_requests_to_remove.push_back(target_player);

			std::string server = config::RelayServer.get();
			std::string port = std::to_string(config::RelayPort.get());

			cfgSetVirtual("network", "server", server);
			cfgSetVirtual("network", "GGPORemotePort", port);

			cfgSetVirtual("dojo", "HideKey", "yes");
			cfgSetVirtual("dojo", "Training", "no");
			cfgSetVirtual("dojo", "Relay", "yes");

			cfgSetVirtual("network", "ActAsServer", "yes");
			cfgSetVirtual("network", "GGPO", "yes");
			cfgSetVirtual("network", "Enable", "no");

			dojo.relay_client.target_hostname = cfgLoadStr("dojo", "RelayServer", "");
			dojo.relay_client.Init();
			dojo.relay_client.ConnectRelayServer();
			dojo.relay_client.connect_started = true;

			try
			{
				dojo.relay_client.disconnect_toggle = false;
				std::thread t2(&RelayClient::ClientThread, std::ref(dojo.relay_client));
				t2.detach();
			}
			catch (std::exception &)
			{
			}

			dojo.relay_client.SendHostMsg();

			start_game = true;

			SendStatusMsg("hidden");
		}

		gui_setState(GuiState::DelaySelect);
	}
	else if (parsed_json["type"] == "reject")
	{
		auto it = std::find_if(quick_match.pending.begin(), quick_match.pending.end(),
							   [parsed_json](QuickMatch::QuickMatchMsg &msg)
							   {
								   return msg.uuid == parsed_json["uuid"].get<std::string>();
							   });
		if (it != quick_match.pending.end())
		{
			std::string reject_msg = (it->player_name) + " rejected your challenge";
			AppendToLog(reject_msg);

			quick_match.pending.erase(it);
		}
	}
	else if (parsed_json["type"] == "revoke")
	{
		auto it = std::find_if(quick_match.requests.begin(), quick_match.requests.end(),
							   [parsed_json](QuickMatch::QuickMatchMsg &msg)
							   {
								   return msg.uuid == parsed_json["uuid"].get<std::string>();
							   });
		if (it != requests.end())
		{
			std::string revoke_msg = (it->player_name) + " cancelled their challenge";
			AppendToLog(revoke_msg);
			requests_to_remove.push_back(it->uuid);
		}
	}
	else if (parsed_json["type"] == "key")
	{
		if (parsed_json["cxn_method"] == "relay")
		{
			std::string rk = parsed_json["key"].get<std::string>();
			std::string server = parsed_json["server"].get<std::string>();
			std::string port = parsed_json["port"].get<std::string>();

			cfgSetVirtual("dojo", "RelayKey", rk);

			cfgSetVirtual("network", "RelayServer", server);
			cfgSetVirtual("network", "RelayPort", port);

			cfgSetVirtual("network", "server", server);
			cfgSetVirtual("network", "GGPORemotePort", port);

			cfgSetVirtual("dojo", "Training", "no");
			cfgSetVirtual("dojo", "Relay", "yes");

			cfgSetVirtual("network", "ActAsServer", "no");
			cfgSetVirtual("network", "GGPO", "yes");
			cfgSetVirtual("network", "Enable", "no");

			dojo.relay_client.target_hostname = server;
			dojo.relay_client.SendGuestMsg();

			host_ready = true;
		}
	}
}

void QuickMatch::AppendToLog(std::string msg)
{
	LogEntry entry;
	auto now = std::chrono::system_clock::now();
	auto local_time = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(now)};
	entry.timestamp = std::format("{:%T}", local_time);

	entry.msg = msg;
	log.push_back(entry);
}
