#include "image_encoder.h"

#include <array>
#include <cstring>
#include <vector>

namespace stream_saver {

namespace {

constexpr std::array<char, 64> BASE64 = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
	'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/',
};

uint32_t crc32(const uint8_t *data, size_t size)
{
	uint32_t crc = 0xffffffff;
	for (size_t i = 0; i < size; ++i) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
	}
	return crc ^ 0xffffffff;
}

uint32_t adler32(const std::vector<uint8_t> &data)
{
	uint32_t a = 1;
	uint32_t b = 0;
	for (const uint8_t byte : data) {
		a = (a + byte) % 65521u;
		b = (b + a) % 65521u;
	}
	return (b << 16) | a;
}

void append_u32(std::vector<uint8_t> &out, uint32_t value)
{
	out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
	out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
	out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
	out.push_back(static_cast<uint8_t>(value & 0xff));
}

void append_chunk(std::vector<uint8_t> &png, const char type[4], const std::vector<uint8_t> &payload)
{
	append_u32(png, static_cast<uint32_t>(payload.size()));
	const size_t type_offset = png.size();
	png.insert(png.end(), type, type + 4);
	png.insert(png.end(), payload.begin(), payload.end());
	append_u32(png, crc32(png.data() + type_offset, png.size() - type_offset));
}

std::vector<uint8_t> zlib_store(const std::vector<uint8_t> &data)
{
	std::vector<uint8_t> out;
	out.reserve(data.size() + (data.size() / 65535u + 1u) * 5u + 6u);

	out.push_back(0x78);
	out.push_back(0x01);

	size_t offset = 0;
	while (offset < data.size()) {
		const uint16_t block_size = static_cast<uint16_t>(std::min<size_t>(65535u, data.size() - offset));
		const bool final_block = offset + block_size == data.size();

		out.push_back(final_block ? 0x01 : 0x00);
		out.push_back(static_cast<uint8_t>(block_size & 0xff));
		out.push_back(static_cast<uint8_t>((block_size >> 8) & 0xff));
		const uint16_t nlen = static_cast<uint16_t>(~block_size);
		out.push_back(static_cast<uint8_t>(nlen & 0xff));
		out.push_back(static_cast<uint8_t>((nlen >> 8) & 0xff));
		out.insert(out.end(), data.begin() + static_cast<ptrdiff_t>(offset),
			   data.begin() + static_cast<ptrdiff_t>(offset + block_size));
		offset += block_size;
	}

	append_u32(out, adler32(data));
	return out;
}

std::string base64_encode(const std::vector<uint8_t> &data)
{
	std::string out;
	out.reserve(((data.size() + 2) / 3) * 4);

	for (size_t i = 0; i < data.size(); i += 3) {
		const uint32_t a = data[i];
		const uint32_t b = i + 1 < data.size() ? data[i + 1] : 0;
		const uint32_t c = i + 2 < data.size() ? data[i + 2] : 0;
		const uint32_t triple = (a << 16) | (b << 8) | c;

		out.push_back(BASE64[(triple >> 18) & 0x3f]);
		out.push_back(BASE64[(triple >> 12) & 0x3f]);
		out.push_back(i + 1 < data.size() ? BASE64[(triple >> 6) & 0x3f] : '=');
		out.push_back(i + 2 < data.size() ? BASE64[triple & 0x3f] : '=');
	}

	return out;
}

} // namespace

std::string rgba_to_png_base64(const uint8_t *rgba, uint32_t width, uint32_t height, uint32_t stride)
{
	if (!rgba || width == 0 || height == 0 || stride < width * 4)
		return {};

	std::vector<uint8_t> raw;
	raw.reserve(static_cast<size_t>(height) * (static_cast<size_t>(width) * 4u + 1u));

	for (uint32_t y = 0; y < height; ++y) {
		raw.push_back(0);
		const uint8_t *row = rgba + static_cast<size_t>(y) * stride;
		raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 4u);
	}

	std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

	std::vector<uint8_t> ihdr;
	append_u32(ihdr, width);
	append_u32(ihdr, height);
	ihdr.push_back(8);
	ihdr.push_back(6);
	ihdr.push_back(0);
	ihdr.push_back(0);
	ihdr.push_back(0);
	append_chunk(png, "IHDR", ihdr);

	append_chunk(png, "IDAT", zlib_store(raw));
	append_chunk(png, "IEND", {});

	return base64_encode(png);
}

} // namespace stream_saver
