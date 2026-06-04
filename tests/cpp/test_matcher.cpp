#include "matcher.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace stream_saver;

namespace {

void expect(bool condition, const std::string &message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		std::exit(1);
	}
}

void test_normalize_text()
{
	expect(normalize_text("  123 Main St., Apt #4 ") == "123 main st apt 4",
	       "normalizes punctuation, spaces, and case");
	expect(normalize_text("SneHiL\tSHARMA") == "snehil sharma",
	       "normalizes mixed whitespace");
}

void test_parse_phrase_list()
{
	const auto phrases = parse_phrase_list("123 Main St.\n\n123 main st\nEmail@example.com\n");
	expect(phrases.size() == 2, "deduplicates normalized phrases");
	expect(phrases[0] == "123 main st", "keeps first normalized phrase");
	expect(phrases[1] == "email example com", "normalizes punctuation in phrase list");
}

void test_match()
{
	PhraseMatcher matcher;
	matcher.set_phrases("123 Main St\nemail@example.com");

	std::vector<OcrDetection> detections = {
		{"Ship to: 123 MAIN ST.", 0.91f, {100, 50, 220, 80}},
		{"Not sensitive", 0.99f, {10, 10, 50, 20}},
		{"email@example.com", 0.25f, {300, 100, 450, 120}},
	};

	const auto regions = matcher.match(detections, 0.75f, 10, 500, 250);
	expect(regions.size() == 1, "matches only sensitive text above threshold");
	expect(regions[0].left == 0.18f, "applies left padding and normalizes x");
	expect(regions[0].top == 0.16f, "applies top padding and normalizes y");
	expect(regions[0].right == 0.46f, "applies right padding and normalizes x");
	expect(regions[0].bottom == 0.36f, "applies bottom padding and normalizes y");
}

void test_clamping()
{
	PhraseMatcher matcher;
	matcher.set_phrases("secret");

	std::vector<OcrDetection> detections = {
		{"secret", 1.0f, {2, 2, 98, 48}},
	};

	const auto regions = matcher.match(detections, 0.5f, 20, 100, 50);
	expect(regions.size() == 1, "matches clamped region");
	expect(regions[0].left == 0.0f, "clamps left");
	expect(regions[0].top == 0.0f, "clamps top");
	expect(regions[0].right == 1.0f, "clamps right");
	expect(regions[0].bottom == 1.0f, "clamps bottom");
}

void test_split_phrase_line_match()
{
	PhraseMatcher matcher;
	matcher.set_phrases("123 Dump Drive\nSteam");

	std::vector<OcrDetection> detections = {
		{"123", 0.95f, {300, 100, 340, 125}},
		{"Dump", 0.95f, {350, 101, 420, 126}},
		{"Drive", 0.95f, {430, 100, 500, 125}},
		{"Steamed", 0.99f, {20, 200, 90, 225}},
	};

	const auto regions = matcher.match(detections, 0.75f, 5, 600, 300);
	expect(regions.size() == 1, "matches split multi-word phrase without substring false positive");
	expect(regions[0].left < 0.51f && regions[0].right > 0.83f,
	       "split phrase region covers combined words");
}

} // namespace

int main()
{
	test_normalize_text();
	test_parse_phrase_list();
	test_match();
	test_clamping();
	test_split_phrase_line_match();
	std::cout << "matcher tests passed\n";
	return 0;
}
