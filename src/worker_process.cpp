#include "worker_process.h"

#include <obs-module.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#ifdef _WIN32
#include <shellapi.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace stream_saver {

#ifdef _WIN32
namespace {

std::string worker_log_path(const std::string &path)
{
	const size_t slash = path.find_last_of("\\/");
	const std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
	return dir + "\\stream-saver-worker.log";
}

std::string dirname(const std::string &path)
{
	const size_t slash = path.find_last_of("\\/");
	return slash == std::string::npos ? "." : path.substr(0, slash);
}

bool file_exists(const std::string &path)
{
	return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string quote_arg(const std::string &value)
{
	std::string quoted = "\"";
	for (const char ch : value) {
		if (ch == '\\' || ch == '"')
			quoted.push_back('\\');
		quoted.push_back(ch);
	}
	quoted.push_back('"');
	return quoted;
}

} // namespace
#endif

WorkerProcess::~WorkerProcess()
{
	stop();
}

bool WorkerProcess::running() const
{
#ifdef _WIN32
	if (!process_.hProcess)
		return false;

	DWORD exit_code = 0;
	if (!GetExitCodeProcess(process_.hProcess, &exit_code))
		return false;

	return exit_code == STILL_ACTIVE;
#else
	return pid_ > 0;
#endif
}

bool WorkerProcess::start(const std::string &path, int port, const std::string &backend,
			  const std::string &device, const std::string &model_path, int image_size)
{
	if (path.empty())
		return false;

	if (running())
		return true;

#ifdef _WIN32
	if (process_.hProcess) {
		CloseHandle(process_.hThread);
		CloseHandle(process_.hProcess);
		process_ = {};
	}
#endif

	const std::string port_arg = std::to_string(port);
	const std::string backend_arg = backend.empty() ? "onnxruntime" : backend;
	const std::string device_arg = device.empty() ? "cpu" : device;
	const std::string model_arg = model_path.empty() ? "yolo11n-text.onnx" : model_path;
	const std::string image_size_arg = std::to_string(std::max(64, image_size));

#ifdef _WIN32
	std::string lower_path = path;
	std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(),
		       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

	const bool python_script = lower_path.size() >= 3 && lower_path.substr(lower_path.size() - 3) == ".py";
	const bool python_interpreter =
		lower_path.size() >= 10 && lower_path.substr(lower_path.size() - 10) == "python.exe";

	std::string command;
	if (python_script) {
		command = "python.exe " + quote_arg(path) + " --port " + port_arg;
	} else if (python_interpreter) {
		const std::string worker_dir = dirname(dirname(path));
		const std::string script_path = worker_dir + "\\stream_saver_ocr.py";
		if (file_exists(script_path))
			command = quote_arg(path) + " " + quote_arg(script_path) + " --port " + port_arg;
		else
			command = quote_arg(path) + " --port " + port_arg;
	} else {
		command = quote_arg(path) + " --port " + port_arg;
	}
	command += " --backend " + quote_arg(backend_arg) + " --device " + quote_arg(device_arg) +
		   " --model " + quote_arg(model_arg) + " --imgsz " + image_size_arg;
	const std::string log_path = worker_log_path(path);
	SECURITY_ATTRIBUTES security = {};
	security.nLength = sizeof(security);
	security.bInheritHandle = TRUE;

	HANDLE log_file = CreateFileA(log_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, &security,
				      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

	STARTUPINFOA startup = {};
	startup.cb = sizeof(startup);
	if (log_file != INVALID_HANDLE_VALUE) {
		startup.dwFlags |= STARTF_USESTDHANDLES;
		startup.hStdOutput = log_file;
		startup.hStdError = log_file;
		startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	}

	const BOOL inherit_handles = log_file != INVALID_HANDLE_VALUE ? TRUE : FALSE;
	if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, inherit_handles,
			    CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process_)) {
		blog(LOG_WARNING, "[stream-saver] failed to start OCR worker: %s", path.c_str());
		if (log_file != INVALID_HANDLE_VALUE)
			CloseHandle(log_file);
		process_ = {};
		return false;
	}

	if (log_file != INVALID_HANDLE_VALUE)
		CloseHandle(log_file);

	blog(LOG_INFO, "[stream-saver] started YOLO worker: %s device=%s model=%s imgsz=%s",
	     path.c_str(), device_arg.c_str(), model_arg.c_str(), image_size_arg.c_str());
	return true;
#else
	pid_ = fork();
	if (pid_ == 0) {
		execl(path.c_str(), path.c_str(), "--port", port_arg.c_str(), "--device",
		      device_arg.c_str(), "--backend", backend_arg.c_str(), "--model", model_arg.c_str(),
		      "--imgsz", image_size_arg.c_str(), static_cast<char *>(nullptr));
		_exit(127);
	}

	if (pid_ < 0) {
		blog(LOG_WARNING, "[stream-saver] failed to fork OCR worker: %s", path.c_str());
		pid_ = -1;
		return false;
	}

	blog(LOG_INFO, "[stream-saver] started YOLO worker: %s device=%s model=%s imgsz=%s",
	     path.c_str(), device_arg.c_str(), model_arg.c_str(), image_size_arg.c_str());
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
