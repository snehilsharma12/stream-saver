#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace stream_saver {

struct OcrBox {
	float x1 = 0.0f;
	float y1 = 0.0f;
	float x2 = 0.0f;
	float y2 = 0.0f;
};

struct OcrDetection {
	std::string text;
	float confidence = 0.0f;
	OcrBox box;
};

struct RedactionRegion {
	float left = 0.0f;
	float top = 0.0f;
	float right = 0.0f;
	float bottom = 0.0f;
};

std::string normalize_text(const std::string &input);
std::vector<std::string> parse_phrase_list(const std::string &phrases);

class PhraseMatcher {
public:
	void set_phrases(const std::string &phrases);
	bool has_phrases() const;
	std::vector<RedactionRegion> match(const std::vector<OcrDetection> &detections,
					    float confidence_threshold, int padding_px,
					    int frame_width, int frame_height) const;

private:
	std::vector<std::string> phrases_;
};

} // namespace stream_saver
