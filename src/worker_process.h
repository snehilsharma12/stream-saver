#pragma once

#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#endif

namespace stream_saver {

class WorkerProcess {
public:
	WorkerProcess() = default;
	~WorkerProcess();

	WorkerProcess(const WorkerProcess &) = delete;
	WorkerProcess &operator=(const WorkerProcess &) = delete;

	bool start(const std::string &path, int port);
	void stop();
	bool running() const;

private:
#ifdef _WIN32
	PROCESS_INFORMATION process_ = {};
#else
	pid_t pid_ = -1;
#endif
};

} // namespace stream_saver
