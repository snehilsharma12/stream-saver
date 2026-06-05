#pragma once

#include "matcher.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace stream_saver {

struct OcrFrame {
	uint64_t frame_id = 0;
	int width = 0;
	int height = 0;
	int source_width = 0;
	int source_height = 0;
	bool warmup = false;
	bool debug = false;
	std::string png_base64;
	std::string source_png_base64;
};

struct OcrResult {
	uint64_t frame_id = 0;
	std::vector<OcrDetection> detections;
	std::string error;
};

using OcrCallback = std::function<void(OcrResult)>;

class OcrClient {
public:
	OcrClient();
	~OcrClient();

	OcrClient(const OcrClient &) = delete;
	OcrClient &operator=(const OcrClient &) = delete;

	void configure(std::string host, uint16_t port);
	bool submit(OcrFrame frame, OcrCallback callback);
	bool busy() const;

private:
	void worker_thread(OcrFrame frame, OcrCallback callback);
	OcrResult send_request(const OcrFrame &frame);

	std::string host_ = "127.0.0.1";
	uint16_t port_ = 48741;
	std::atomic_bool busy_{false};
	std::mutex thread_mutex_;
	std::thread thread_;
};

} // namespace stream_saver
