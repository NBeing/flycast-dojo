#pragma once

#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "cfg/option.h"

#ifndef ANDROID
#include <curl/curl.h>
#include <curl/easy.h>
#endif

class DojoFile
{
public:
	DojoFile();
	void Reset();

	std::string DownloadFile(std::string download_url, std::string dest_folder, std::string target_filename, std::string append);
	std::string DownloadFile(std::string download_url, std::string dest_folder, size_t download_size, std::string target_filename, std::string append);

	std::string DownloadNetSave(std::string rom_name);
	std::string DownloadNetSave(std::string rom_name, std::string commit);

	void DownloadCurrentNetSave();
	bool NetSaveExists(std::string path);

	std::string status_text = "Idle";
	bool start_save_download;
	bool save_download_started;
	bool save_download_ended;
	std::tuple<std::string, std::string> tag_download;

	bool download_skipped = false;
	bool download_only = false;

	size_t total_size;
	size_t downloaded_size;

	std::string entry_name;
	std::ofstream of;
	std::string game_path;
	bool post_save_launch;

	std::string source_url;
	std::string dest_path;
	std::string state_commit;

	bool not_found;
	bool no_save_launch;
};

extern DojoFile dojo_file;

