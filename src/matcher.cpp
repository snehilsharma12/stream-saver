#include "matcher.h"

namespace stream_saver {

namespace {

std::string trim(const std::string &value)
{
	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return {};

	const auto last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

float clamp01(float value)
{
	return std::max(0.0f, std::min(1.0f, value));
}

} // namespace

std::string normalize_text(const std::string &input)
{
	std::string output;
	output.reserve(input.size());

	bool previous_space = false;
	for (unsigned char ch : input) {
		if (std::isalnum(ch)) {
			output.push_back(static_cast<char>(std::tolower(ch)));
			previous_space = false;
		} else if (std::isspace(ch) || std::ispunct(ch)) {
			if (!previous_space && !output.empty()) {
				output.push_back(' ');
				previous_space = true;
			}
		}
	}

	if (!output.empty() && output.back() == ' ')
		output.pop_back();

	return output;
}

std::vector<std::string> parse_phrase_list(const std::string &phrases)
{
	std::vector<std::string> parsed;
	std::stringstream stream(phrases);
	std::string line;

	while (std::getline(stream, line)) {
		const auto normalized = normalize_text(trim(line));
		if (!normalized.empty() &&
		    std::find(parsed.begin(), parsed.end(), normalized) == parsed.end()) {
			parsed.push_back(normalized);
		}
	}

	return parsed;
}

void PhraseMatcher::set_phrases(const std::string &phrases)
{
	phrases_ = parse_phrase_list(phrases);
}

std::vector<RedactionRegion> PhraseMatcher::match(const std::vector<OcrDetection> &detections,
						   float confidence_threshold,
						   int padding_px, int frame_width,
						   int frame_height) const
{
	std::vector<RedactionRegion> regions;
	if (frame_width <= 0 || frame_height <= 0 || phrases_.empty())
		return regions;

	for (const auto &detection : detections) {
		if (detection.confidence < confidence_threshold)
			continue;

		const auto normalized = normalize_text(detection.text);
		if (normalized.empty())
			continue;

		bool matched = false;
		for (const auto &phrase : phrases_) {
			if (normalized.find(phrase) != std::string::npos) {
				matched = true;
				break;
			}
		}

		if (!matched)
			continue;

		const float left = std::min(detection.box.x1, detection.box.x2) - padding_px;
		const float right = std::max(detection.box.x1, detection.box.x2) + padding_px;
		const float top = std::min(detection.box.y1, detection.box.y2) - padding_px;
		const float bottom = std::max(detection.box.y1, detection.box.y2) + padding_px;

		regions.push_back({
			clamp01(left / static_cast<float>(frame_width)),
			clamp01(top / static_cast<float>(frame_height)),
			clamp01(right / static_cast<float>(frame_width)),
			clamp01(bottom / static_cast<float>(frame_height)),
		});
	}

	return regions;
}

} // namespace stream_saver
