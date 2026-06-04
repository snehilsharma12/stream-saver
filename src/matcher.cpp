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

bool contains_phrase(const std::string &text, const std::string &phrase)
{
	if (text == phrase)
		return true;

	const auto pos = text.find(phrase);
	if (pos == std::string::npos)
		return false;

	const bool left_ok = pos == 0 || text[pos - 1] == ' ';
	const size_t end = pos + phrase.size();
	const bool right_ok = end >= text.size() || text[end] == ' ';
	return left_ok && right_ok;
}

size_t word_count(const std::string &text)
{
	if (text.empty())
		return 0;

	size_t count = 1;
	for (char ch : text) {
		if (ch == ' ')
			++count;
	}
	return count;
}

RedactionRegion make_region(float left, float top, float right, float bottom, int padding_px,
			     int frame_width, int frame_height)
{
	left -= padding_px;
	right += padding_px;
	top -= padding_px;
	bottom += padding_px;

	return {
		clamp01(left / static_cast<float>(frame_width)),
		clamp01(top / static_cast<float>(frame_height)),
		clamp01(right / static_cast<float>(frame_width)),
		clamp01(bottom / static_cast<float>(frame_height)),
	};
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

bool PhraseMatcher::has_phrases() const
{
	return !phrases_.empty();
}

std::vector<RedactionRegion> PhraseMatcher::match(const std::vector<OcrDetection> &detections,
						   float confidence_threshold,
						   int padding_px, int frame_width,
						   int frame_height) const
{
	std::vector<RedactionRegion> regions;
	if (frame_width <= 0 || frame_height <= 0 || phrases_.empty())
		return regions;

	struct Candidate {
		std::string text;
		float left = 0.0f;
		float top = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;
	};

	std::vector<Candidate> candidates;
	for (const auto &detection : detections) {
		if (detection.confidence < confidence_threshold)
			continue;

		const auto normalized = normalize_text(detection.text);
		if (normalized.empty())
			continue;

		candidates.push_back({
			normalized,
			std::min(detection.box.x1, detection.box.x2),
			std::min(detection.box.y1, detection.box.y2),
			std::max(detection.box.x1, detection.box.x2),
			std::max(detection.box.y1, detection.box.y2),
		});

		bool matched = false;
		for (const auto &phrase : phrases_) {
			if (contains_phrase(normalized, phrase)) {
				matched = true;
				break;
			}
		}

		if (!matched)
			continue;

		regions.push_back(make_region(std::min(detection.box.x1, detection.box.x2),
					      std::min(detection.box.y1, detection.box.y2),
					      std::max(detection.box.x1, detection.box.x2),
					      std::max(detection.box.y1, detection.box.y2),
					      padding_px, frame_width, frame_height));
	}

	std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
		if (std::abs(a.top - b.top) > 8.0f)
			return a.top < b.top;
		return a.left < b.left;
	});

	for (size_t i = 0; i < candidates.size(); ++i) {
		std::string line_text = candidates[i].text;
		float left = candidates[i].left;
		float top = candidates[i].top;
		float right = candidates[i].right;
		float bottom = candidates[i].bottom;
		size_t words = word_count(line_text);

		for (size_t j = i + 1; j < candidates.size(); ++j) {
			const float line_height = std::max(1.0f, bottom - top);
			const float candidate_center = (candidates[j].top + candidates[j].bottom) * 0.5f;
			const float line_center = (top + bottom) * 0.5f;
			if (std::abs(candidate_center - line_center) > std::max(10.0f, line_height * 0.8f))
				break;

			if (candidates[j].left < right - 2.0f)
				continue;

			const float gap = candidates[j].left - right;
			if (gap > std::max(36.0f, line_height * 4.0f))
				break;

			line_text += " " + candidates[j].text;
			words += word_count(candidates[j].text);
			left = std::min(left, candidates[j].left);
			top = std::min(top, candidates[j].top);
			right = std::max(right, candidates[j].right);
			bottom = std::max(bottom, candidates[j].bottom);

			for (const auto &phrase : phrases_) {
				const size_t phrase_words = word_count(phrase);
				if (phrase_words > 1 && words <= phrase_words + 1 &&
				    contains_phrase(line_text, phrase)) {
					regions.push_back(make_region(left, top, right, bottom, padding_px,
								      frame_width, frame_height));
					break;
				}
			}

			size_t max_phrase_words = 1;
			for (const auto &phrase : phrases_)
				max_phrase_words = std::max(max_phrase_words, word_count(phrase));
			if (words > max_phrase_words + 1)
				break;
		}
	}

	return regions;
}

} // namespace stream_saver
