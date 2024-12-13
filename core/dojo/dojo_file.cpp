#include "dojo.h"

DojoFile dojo_file;

DojoFile::DojoFile()
{
	Reset();
}

void DojoFile::Reset()
{
	post_save_launch = false;
	not_found = false;
	no_save_launch = false;
}

std::string DojoFile::DownloadFile(std::string download_url, std::string dest_folder, std::string append)
{
	return DownloadFile(download_url, dest_folder, 0, append);
}

#ifndef ANDROID
static int xferinfo(void *p,
					curl_off_t dltotal, curl_off_t dlnow,
					curl_off_t ultotal, curl_off_t ulnow)
{
	std::stringstream s;

	if (dltotal == 0)
		dltotal = dojo_file.total_size;

	dojo_file.total_size = dltotal;
	dojo_file.downloaded_size = dlnow;

	s << "\r" << dlnow << " of " << dltotal
	  << " bytes received (" << int(dltotal ? dlnow * 100. / dltotal : 0) << "%)" << std::flush;
	INFO_LOG(NETWORK, "DOJO: %s", s.str().data());

	return 0;
}

size_t writeFileFunction(const char *p, size_t size, size_t nmemb)
{
	dojo_file.of.write(p, size * nmemb);
	return size * nmemb;
}
#endif

std::string DojoFile::DownloadNetSave(std::string rom_name)
{
	auto const now = std::chrono::system_clock::now();
	std::time_t now_t = std::chrono::system_clock::to_time_t(now);

	return DownloadNetSave(rom_name, "");
}

std::string DojoFile::DownloadNetSave(std::string rom_name, std::string commit)
{
	auto net_state_base = config::NetSaveBase.get();
	if (!commit.empty())
	{
		commit.erase(std::remove_if(commit.begin(), commit.end(),
									[](char c)
									{
										return (c == ' ' || c == '\n' || c == '\r' ||
												c == '\t' || c == '\v' || c == '\f');
									}),
					 commit.end());
		dojo.Replace(net_state_base, "main", commit);
	}
	auto state_file = rom_name + ".state";
	auto net_state_file = state_file + ".net";
	auto net_state_url = net_state_base + net_state_file;
	dojo.Replace(net_state_url, " ", "%20");

	NOTICE_LOG(NETWORK, "save url: %s", net_state_url.data());

	status_text = "Downloading netplay savestate for " + rom_name + ".";

	auto filename = DownloadFile(net_state_url, "data", commit);
	if (filename.empty())
		return filename;

	save_download_ended = true;

	if (!commit.empty())
	{
		std::string commit_net_state_path = filename + "." + commit;

		// keep local copy named with commit string as backup and for replays
		if (!std::filesystem::exists(commit_net_state_path))
		{
			std::filesystem::copy(filename, commit_net_state_path);
		}
	}

	return filename;
}

std::string DojoFile::DownloadFile(std::string download_url, std::string dest_folder, size_t download_size, std::string append)
{
	dojo_file.source_url = download_url;

	std::vector<std::string> path_elements;
	dojo.Split(download_url, '/', path_elements);
	std::string filename = path_elements.back();

	// remove GET parameters
	if (filename.find("?") != std::string::npos)
	{
		path_elements.clear();
		dojo.Split(filename, '?', path_elements);
		filename = path_elements.front();
	}

	std::string path = filename;
	if (dest_folder == "data")
	{
		path = get_writable_data_path("") + "//" + filename;
		dojo_file.dest_path = get_writable_data_path("");
	}
	else if (!dest_folder.empty())
	{
		path = get_writable_data_path("") + "//" + dest_folder + "//" + filename;
		dojo_file.dest_path = get_writable_data_path("") + "//" + dest_folder;
	}

	if (dest_folder == "avatar")
	{
		if (std::filesystem::exists(path))
			return path;

		if (!std::filesystem::exists(get_writable_data_path("avatar")))
			std::filesystem::create_directory(get_writable_data_path("avatar"));
	}

	if (!append.empty())
	{
		path = path + "." + append;
	}

	std::string clean_path = path;
	dojo.Replace(clean_path, "%20", " ");
	std::string commit_path = clean_path + ".commit";

	// if file already exists, delete before starting new download
	if (file_exists(clean_path.c_str()))
		remove(clean_path.c_str());

	if (file_exists(commit_path.c_str()))
		remove(commit_path.c_str());

	std::string final_path = path;
	path = path + ".download";

	of = std::ofstream(path, std::ofstream::out | std::ofstream::binary);

	total_size = download_size;

	long response_code = -1;
#ifndef ANDROID
	auto curl = curl_easy_init();
	CURLcode res = CURLE_OK;
	if (curl)
	{
		curl_easy_setopt(curl, CURLOPT_URL, download_url.data());

		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

		curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFileFunction);
		curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

		res = curl_easy_perform(curl);

		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

		if (response_code == 404)
		{
			res = CURLE_REMOTE_FILE_NOT_FOUND;
		}

		curl_easy_cleanup(curl);
	}

	dojo.Replace(filename, "%20", " ");

	if (res != CURLE_OK)
	{
		fprintf(stderr, "%s\n", curl_easy_strerror(res));
		if (res == CURLE_REMOTE_FILE_NOT_FOUND || response_code == 404)
		{
			not_found = true;
			status_text = filename + " not found. ";
			if (std::filesystem::path(filename).extension().string() == ".net")
				status_text += "\n\nIt is recommended that you create a savestate\nto share with your opponent.";
		}
		else
			status_text = "Unable to retrieve " + filename + ".";
		if (file_exists(path.c_str()))
			remove(path.c_str());
	}
#endif
	of.close();

	if (file_exists(path.c_str()))
	{
		std::string old_path = path;
		dojo.Replace(final_path, "%20", " ");
		bool copied = std::filesystem::copy_file(
			std::filesystem::path(old_path),
			std::filesystem::path(final_path),
			std::filesystem::copy_options::overwrite_existing);
		if (copied)
		{
			std::filesystem::remove(
				std::filesystem::path(old_path));
		}
	}

	if (response_code == 404 || (file_exists(final_path.c_str()) && std::filesystem::file_size(final_path) == 0) || !file_exists(final_path.c_str()))
	{
		remove(final_path.c_str());
		final_path = "";
	}
	else
		status_text = filename + " successfully downloaded.";

	return final_path;
}

void DojoFile::DownloadCurrentNetSave()
{
	// dojo_file.not_found = false;
	// dojo_file.save_download_started = false;
	// dojo_file.save_download_ended = false;
	if (dojo_file.state_commit.empty())
		dojo_file.DownloadNetSave(dojo.game_name, "");
	else
		dojo_file.DownloadNetSave(dojo.game_name, dojo_file.state_commit);
}

bool DojoFile::NetSaveExists(std::string path)
{
	auto game_path = std::filesystem::path(path);
	auto game_name = game_path.filename().stem().string();
	std::string net_state_path = get_writable_data_path(game_name + ".state.net");
	return std::filesystem::exists(net_state_path);
}