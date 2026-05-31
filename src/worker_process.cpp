#include "worker_process.h"

#include <obs-module.h>

#include <algorithm>
#include <cctype>
#include <string>

#ifdef _WIN32
#include <shellapi.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace stream_saver {

WorkerProcess::~WorkerProcess()
{
	stop();
}

bool WorkerProcess::running() const
{
#ifdef _WIN32
	return process_.hProcess != nullptr;
#else
	return pid_ > 0;
#endif
}

bool WorkerProcess::start(const std::string &path, int port)
{
	if (path.empty())
		return false;

	if (running())
		return true;

	const std::string port_arg = std::to_string(port);

#ifdef _WIN32
	std::string lower_path = path;
	std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(),
		       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

	const bool python_script = lower_path.size() >= 3 && lower_path.substr(lower_path.size() - 3) == ".py";
	std::string command = python_script ? "python.exe \"" + path + "\" --port " + port_arg
					    : "\"" + path + "\" --port " + port_arg;
	STARTUPINFOA startup = {};
	startup.cb = sizeof(startup);

	if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE,
			    CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process_)) {
		blog(LOG_WARNING, "[stream-saver] failed to start OCR worker: %s", path.c_str());
		process_ = {};
		return false;
	}

	return true;
#else
	pid_ = fork();
	if (pid_ == 0) {
		execl(path.c_str(), path.c_str(), "--port", port_arg.c_str(), static_cast<char *>(nullptr));
		_exit(127);
	}

	if (pid_ < 0) {
		blog(LOG_WARNING, "[stream-saver] failed to fork OCR worker: %s", path.c_str());
		pid_ = -1;
		return false;
	}

	return true;
#endif
}

void WorkerProcess::stop()
{
#ifdef _WIN32
	if (!process_.hProcess)
		return;

	TerminateProcess(process_.hProcess, 0);
	WaitForSingleObject(process_.hProcess, 2000);
	CloseHandle(process_.hThread);
	CloseHandle(process_.hProcess);
	process_ = {};
#else
	if (pid_ <= 0)
		return;

	kill(pid_, SIGTERM);
	waitpid(pid_, nullptr, 0);
	pid_ = -1;
#endif
}

} // namespace stream_saver
