#include "ocr_client.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <regex>
#include <sstream>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace stream_saver {

namespace {

#ifdef _WIN32
using socket_handle = SOCKET;
constexpr socket_handle invalid_socket_handle = INVALID_SOCKET;

void close_socket(socket_handle socket)
{
	closesocket(socket);
}
#else
using socket_handle = int;
constexpr socket_handle invalid_socket_handle = -1;

void close_socket(socket_handle socket)
{
	close(socket);
}
#endif

std::string escape_json(const std::string &value)
{
	std::string escaped;
	escaped.reserve(value.size());
	for (const char ch : value) {
		switch (ch) {
		case '\\':
			escaped += "\\\\";
			break;
		case '"':
			escaped += "\\\"";
			break;
		case '\n':
			escaped += "\\n";
			break;
		default:
			escaped.push_back(ch);
			break;
		}
	}
	return escaped;
}

std::vector<OcrDetection> parse_detections(const std::string &json)
{
	std::vector<OcrDetection> detections;
	const std::regex item_regex(
		R"JSON(\{"text":"([^"]*)","confidence":([0-9.]+),"box":\[([0-9.+-]+),([0-9.+-]+),([0-9.+-]+),([0-9.+-]+),([0-9.+-]+),([0-9.+-]+),([0-9.+-]+),([0-9.+-]+)\]\})JSON");

	for (auto it = std::sregex_iterator(json.begin(), json.end(), item_regex);
	     it != std::sregex_iterator(); ++it) {
		const auto &match = *it;
		const float x1 = std::stof(match[3].str());
		const float y1 = std::stof(match[4].str());
		const float x2 = std::stof(match[5].str());
		const float y2 = std::stof(match[6].str());
		const float x3 = std::stof(match[7].str());
		const float y3 = std::stof(match[8].str());
		const float x4 = std::stof(match[9].str());
		const float y4 = std::stof(match[10].str());

		OcrDetection detection;
		detection.text = match[1].str();
		detection.confidence = std::stof(match[2].str());
		detection.box.x1 = std::min(std::min(x1, x2), std::min(x3, x4));
		detection.box.y1 = std::min(std::min(y1, y2), std::min(y3, y4));
		detection.box.x2 = std::max(std::max(x1, x2), std::max(x3, x4));
		detection.box.y2 = std::max(std::max(y1, y2), std::max(y3, y4));
		detections.push_back(detection);
	}

	return detections;
}

bool send_all(socket_handle socket, const std::string &message)
{
	const char *data = message.data();
	size_t remaining = message.size();

	while (remaining > 0) {
#ifdef _WIN32
		const int sent = send(socket, data, static_cast<int>(remaining), 0);
#else
		const ssize_t sent = send(socket, data, remaining, 0);
#endif
		if (sent <= 0)
			return false;

		data += sent;
		remaining -= static_cast<size_t>(sent);
	}

	return true;
}

void set_socket_timeouts(socket_handle socket, int milliseconds)
{
#ifdef _WIN32
	DWORD timeout = static_cast<DWORD>(milliseconds);
	setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
#else
	timeval timeout = {};
	timeout.tv_sec = milliseconds / 1000;
	timeout.tv_usec = (milliseconds % 1000) * 1000;
	setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
}

} // namespace

OcrClient::OcrClient()
{
#ifdef _WIN32
	WSADATA data;
	WSAStartup(MAKEWORD(2, 2), &data);
#endif
}

OcrClient::~OcrClient()
{
	{
		std::lock_guard<std::mutex> lock(thread_mutex_);
		if (thread_.joinable())
			thread_.join();
	}
#ifdef _WIN32
	WSACleanup();
#endif
}

void OcrClient::configure(std::string host, uint16_t port)
{
	host_ = std::move(host);
	port_ = port;
}

bool OcrClient::busy() const
{
	return busy_.load();
}

bool OcrClient::submit(OcrFrame frame, OcrCallback callback)
{
	bool expected = false;
	if (!busy_.compare_exchange_strong(expected, true))
		return false;

	std::lock_guard<std::mutex> lock(thread_mutex_);
	if (thread_.joinable())
		thread_.join();

	thread_ = std::thread(&OcrClient::worker_thread, this, std::move(frame), std::move(callback));
	return true;
}

void OcrClient::worker_thread(OcrFrame frame, OcrCallback callback)
{
	OcrResult result = send_request(frame);

	if (callback)
		callback(std::move(result));

	busy_.store(false);
}

OcrResult OcrClient::send_request(const OcrFrame &frame)
{
	OcrResult result;
	result.frame_id = frame.frame_id;

	addrinfo hints = {};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	addrinfo *addresses = nullptr;
	const auto port = std::to_string(port_);
	if (getaddrinfo(host_.c_str(), port.c_str(), &hints, &addresses) != 0) {
		result.error = "failed to resolve OCR worker";
		return result;
	}

	socket_handle socket = invalid_socket_handle;
	for (int attempt = 0; attempt < 10 && socket == invalid_socket_handle; ++attempt) {
		for (addrinfo *address = addresses; address != nullptr; address = address->ai_next) {
			socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
			if (socket == invalid_socket_handle)
				continue;

			if (connect(socket, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0)
				break;

			close_socket(socket);
			socket = invalid_socket_handle;
		}

		if (socket == invalid_socket_handle)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	freeaddrinfo(addresses);

	if (socket == invalid_socket_handle) {
		result.error = "failed to connect to OCR worker";
		return result;
	}

	set_socket_timeouts(socket, 60000);

	std::ostringstream request;
	request << "{\"type\":\"ocr\",\"frame_id\":" << frame.frame_id << ",\"width\":"
		<< frame.width << ",\"height\":" << frame.height
		<< ",\"source_width\":" << frame.source_width
		<< ",\"source_height\":" << frame.source_height
		<< ",\"warmup\":" << (frame.warmup ? "true" : "false")
		<< ",\"debug\":" << (frame.debug ? "true" : "false")
		<< ",\"format\":\"png-base64\",\"image\":\""
		<< escape_json(frame.png_base64) << "\"";
	if (!frame.source_png_base64.empty()) {
		request << ",\"source_image\":\"" << escape_json(frame.source_png_base64) << "\"";
	}
	request << "}\n";

	if (!send_all(socket, request.str())) {
		result.error = "failed to send OCR request";
		close_socket(socket);
		return result;
	}

	std::string response;
	char buffer[4096];
	while (response.find('\n') == std::string::npos) {
#ifdef _WIN32
		const int received = recv(socket, buffer, sizeof(buffer), 0);
#else
		const ssize_t received = recv(socket, buffer, sizeof(buffer), 0);
#endif
		if (received <= 0)
			break;
		response.append(buffer, buffer + received);
	}

	close_socket(socket);

	if (response.empty()) {
		result.error = "empty OCR response";
		return result;
	}

	result.detections = parse_detections(response);
	return result;
}

} // namespace stream_saver
