#include <cstdint>
#include <vector>

#include "image.h"

static Image::Format channelsToFormat(uint32_t channels) {
	switch (channels) {
		case 1:
			return Image::Format::R8;
		case 2:
			return Image::Format::RG8;
		case 3:
			return Image::Format::RGB8;
		case 4:
			return Image::Format::RGBA8;
	}

	return Image::Format::R8;
}

static uint32_t formatToChannels(Image::Format format) {
	switch (format) {
		case Image::Format::R8:
		case Image::Format::L8:
			return 1;
		case Image::Format::RG8:
		case Image::Format::LA8:
			return 2;
		case Image::Format::RGB8:
			return 3;
		case Image::Format::RGBA8:
			return 4;
	}

	return 1;
}

Image::Color Image::_getPixelAtOffset(size_t offset) const {
	uint32_t i = offset * formatToChannels(_format);

	Color color = {};

	switch (_format) {
		case Format::R8:
		case Format::L8:
			color.r = _data[i + 0];
			break;
		case Format::RG8:
			color.r = _data[i + 0];
			color.g = _data[i + 1];
			break;
		case Format::LA8:
			color.r = _data[i + 0];
			color.a = _data[i + 1];
			break;
		case Format::RGB8:
			color.r = _data[i + 0];
			color.g = _data[i + 1];
			color.b = _data[i + 2];
			break;
		case Format::RGBA8:
			color.r = _data[i + 0];
			color.g = _data[i + 1];
			color.b = _data[i + 2];
			color.a = _data[i + 3];
			break;
	}

	return color;
}

void Image::_setPixelAtOffset(size_t offset, const Color &color) {
	uint32_t i = offset * formatToChannels(_format);

	switch (_format) {
		case Format::R8:
		case Format::L8:
			_data[i + 0] = color.r;
			break;
		case Format::RG8:
			_data[i + 0] = color.r;
			_data[i + 1] = color.g;
			break;
		case Format::LA8:
			_data[i + 0] = color.r;
			_data[i + 1] = color.a;
			break;
		case Format::RGB8:
			_data[i + 0] = color.r;
			_data[i + 1] = color.g;
			_data[i + 2] = color.b;
			break;
		case Format::RGBA8:
			_data[i + 0] = color.r;
			_data[i + 1] = color.g;
			_data[i + 2] = color.b;
			_data[i + 3] = color.a;
			break;
	}
}

Image *Image::getColorMap() const {
	Format format = Format::RGBA8;
	size_t pixelCount = _width * _height;
	uint32_t channels = formatToChannels(format);

	std::vector<uint8_t> newData(pixelCount * channels);
	Image *pColorMap = new Image(_width, _height, format, newData);

	for (size_t offset = 0; offset < pixelCount; offset++) {
		Color src = _getPixelAtOffset(offset);
		pColorMap->_setPixelAtOffset(offset, src);
	}

	return pColorMap;
}

Image *Image::getNormalMap() const {
	Format format = Format::RG8;
	size_t pixelCount = _width * _height;
	uint32_t channels = formatToChannels(format);

	std::vector<uint8_t> newData(pixelCount * channels);
	Image *pNormalMap = new Image(_width, _height, format, newData);

	for (size_t offset = 0; offset < pixelCount; offset++) {
		Color src = _getPixelAtOffset(offset);
		pNormalMap->_setPixelAtOffset(offset, src);
	}

	return pNormalMap;
}

Image *Image::getMetallicMap(Channel channel) const {
	Format format = Format::R8;
	size_t pixelCount = _width * _height;
	uint32_t channels = formatToChannels(format);

	std::vector<uint8_t> newData(pixelCount * channels);
	Image *pMetallicMap = new Image(_width, _height, format, newData);

	for (size_t offset = 0; offset < pixelCount; offset++) {
		Color src = _getPixelAtOffset(offset);
		Color dst;

		// swizzle
		switch (channel) {
			case Channel::R:
				dst.r = src.r;
				break;
			case Channel::G:
				dst.r = src.g;
				break;
			case Channel::B:
				dst.r = src.b;
				break;
			case Channel::A:
				dst.r = src.a;
				break;
		}

		pMetallicMap->_setPixelAtOffset(offset, dst);
	}

	return pMetallicMap;
}

Image *Image::getRoughnessMap(Channel channel) const {
	Format format = Format::R8;
	size_t pixelCount = _width * _height;
	uint32_t channels = formatToChannels(format);

	std::vector<uint8_t> newData(pixelCount * channels);
	Image *pRoughnessMap = new Image(_width, _height, format, newData);

	for (size_t offset = 0; offset < pixelCount; offset++) {
		Color src = _getPixelAtOffset(offset);
		Color dst;

		// swizzle
		switch (channel) {
			case Channel::R:
				dst.r = src.r;
				break;
			case Channel::G:
				dst.r = src.g;
				break;
			case Channel::B:
				dst.r = src.b;
				break;
			case Channel::A:
				dst.r = src.a;
				break;
		}

		pRoughnessMap->_setPixelAtOffset(offset, dst);
	}

	return pRoughnessMap;
}

uint32_t Image::getWidth() const {
	return _width;
}

uint32_t Image::getHeight() const {
	return _height;
}

Image::Format Image::getFormat() const {
	return _format;
}

std::vector<uint8_t> Image::getData() const {
	return _data;
}

uint32_t Image::getPixelSize() const {
	return formatToChannels(_format);
}

Image::Image(uint32_t width, uint32_t height, Format format, const std::vector<uint8_t> &data) {
	_width = width;
	_height = height;
	_format = format;
	_data = data;
}

Image::Image(uint32_t width, uint32_t height, uint32_t channels, const std::vector<uint8_t> &data) {
	_width = width;
	_height = height;
	_format = channelsToFormat(channels);
	_data = data;
}
