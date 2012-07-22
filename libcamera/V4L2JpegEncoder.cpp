/*
 * Copyright 2012 Tomsaz Figa <tomasz.figa at gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define LOG_TAG "V4L2JpegEncoder"
#define MAIN_DUMP  0
#define THUMB_DUMP 0

#include <utils/Log.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "V4L2JpegEncoder.h"
#include "utils.h"

/*
 * V4L2JpegEncoder::Buffer utlity class
 */

V4L2JpegEncoder::Buffer::Buffer(uint32_t size, void *source) :
	data(0),
	size(0)
{
	TRACE();

	data = new uint8_t[size];
	if (source && data)
		memcpy(data, source, size);
}

V4L2JpegEncoder::Buffer::~Buffer(void)
{
	TRACE();

	delete[] data;
}

/*
 * V4L2JpegEncoder::ExifIfd utlity class
 */

V4L2JpegEncoder::ExifIfd::ExifIfd(uint8_t *start,
					uint32_t space, uint32_t base) :
	start(start), space(space), tags(0), base(base)
{
	TRACE();

	data = start + 2 + space*sizeof(ExifIfdEntry) + 4;

	*(uint16_t *)start = space;
	*(uint32_t *)(start + space*sizeof(ExifIfdEntry)) = 0;
}

void V4L2JpegEncoder::ExifIfd::push(uint16_t key, const char *value)
{
	uint32_t length;

	TRACE();

	if (tags >= space)
		return;

	length = strnlen(value, 256);

	ExifIfdEntry *entry = getEntry(tags++);
	entry->tag = key;
	entry->type = EXIF_TYPE_ASCII;
	entry->size = length;

	if (length <= 4) {
		entry->data = 0;
		strncpy((char *)&entry->data, value, length);
		return;
	}

	entry->data = base + (data - start);
	memset(data, 0, length);
	strncpy((char *)data, value, length);
	data += length;
}

void V4L2JpegEncoder::ExifIfd::push(uint16_t key, uint8_t value)
{
	TRACE();

	if (tags >= space)
		return;

	ExifIfdEntry *entry = getEntry(tags++);
	entry->tag = key;
	entry->type = EXIF_TYPE_BYTE;
	entry->size = 1;
	entry->data = value;
}

void V4L2JpegEncoder::ExifIfd::push(uint16_t key, uint16_t value)
{
	TRACE();

	if (tags >= space)
		return;

	ExifIfdEntry *entry = getEntry(tags++);
	entry->tag = key;
	entry->type = EXIF_TYPE_SHORT;
	entry->size = 1;
	entry->data = value;
}

void V4L2JpegEncoder::ExifIfd::push(uint16_t key, uint32_t value)
{
	TRACE();

	if (tags >= space)
		return;

	ExifIfdEntry *entry = getEntry(tags++);
	entry->tag = key;
	entry->type = EXIF_TYPE_LONG;
	entry->size = 1;
	entry->data = value;
}

void V4L2JpegEncoder::ExifIfd::push(uint16_t key, const rational_t *value)
{
	TRACE();

	if (tags >= space)
		return;

	ExifIfdEntry *entry = getEntry(tags++);
	entry->tag = key;
	entry->type = EXIF_TYPE_RATIONAL;
	entry->size = 1;
	entry->data = base + (data - start);
	memcpy(data, value, sizeof(*value));
	data += sizeof(*value);
}

void V4L2JpegEncoder::ExifIfd::push(uint16_t key, const srational_t *value)
{
	TRACE();

	if (tags >= space)
		return;

	ExifIfdEntry *entry = getEntry(tags++);
	entry->tag = key;
	entry->type = EXIF_TYPE_SRATIONAL;
	entry->size = 1;
	entry->data = base + (data - start);
	memcpy(data, value, sizeof(*value));
	data += sizeof(*value);
}

void V4L2JpegEncoder::ExifIfd::push(uint16_t key, uint16_t type,
					uint32_t size, const void *value)
{
	uint32_t length = 0;

	TRACE();

	if (tags >= space)
		return;

	ExifIfdEntry *entry = getEntry(tags++);
	entry->tag = key;
	entry->type = type;
	entry->size = size;

	switch (type) {
	case EXIF_TYPE_ASCII:
	case EXIF_TYPE_BYTE:
		length = size;
		break;
	case EXIF_TYPE_SHORT:
		length = 2*size;
		break;
	case EXIF_TYPE_LONG:
		length = 4*size;
		break;
	case EXIF_TYPE_RATIONAL:
	case EXIF_TYPE_SRATIONAL:
		length = 8*size;
		break;
	}

	if (length <= 4) {
		entry->data = 0;
		memcpy(&entry->data, data, length);
		return;
	}

	entry->data = base + (data - start);
	memcpy(data, value, length);
	data += length;
}

void V4L2JpegEncoder::ExifIfd::link(uint32_t next)
{
	TRACE();
	*(uint32_t *)((char *)start + space*sizeof(ExifIfdEntry)) = next;
}

uint32_t V4L2JpegEncoder::ExifIfd::size(void) const
{
	TRACE();
	return data - start;
}

/*
 * V4L2JpegEncoder class
 */

const char V4L2JpegEncoder::APP1_MARKER[] = {
	0xff, 0xe1
};

const char V4L2JpegEncoder::EXIF_HEADER[] = {
	'E', 'x', 'i', 'f', 0x00, 0x00
};

const char V4L2JpegEncoder::TIFF_HEADER[] = {
	'I', 'I', 0x2a, 0x00, 0x08, 0x00, 0x00, 0x00
};

const char *const V4L2JpegEncoder::defaultStrings[EXIF_COUNT(STRING)] = {
	"Maker",	/* EXIF_STRING_MAKER */
	"Model",	/* EXIF_STRING_MODEL */
	"Software",	/* EXIF_STRING_SOFTWARE */
	"Exif Version",	/* EXIF_STRING_EXIF_VERSION */
	"Date Time",	/* EXIF_STRING_DATE_TIME */
	"User Comment"	/* EXIF_STRING_USER_COMMENT */
};

const uint32_t V4L2JpegEncoder::defaultLongs[EXIF_COUNT(LONG)] = {
	0,		/* EXIF_LONG_PIXEL_X_DIMENSION */
	0,		/* EXIF_LONG_PIXEL_Y_DIMENSION */
};

const uint16_t V4L2JpegEncoder::defaultShorts[EXIF_COUNT(SHORT)] = {
	0,		/* EXIF_SHORT_ORIENTATION */
	0,		/* EXIF_SHORT_YCBCR_POSITIONING */
	0,		/* EXIF_SHORT_EXPOSURE_PROGRAM */
	0,		/* EXIF_SHORT_ISO_SPEED_RATING */
	0,		/* EXIF_SHORT_METERING_MODE */
	0,		/* EXIF_SHORT_FLASH */
	0,		/* EXIF_SHORT_COLOR_SPACE */
	0,		/* EXIF_SHORT_EXPOSURE_MODE */
	0,		/* EXIF_SHORT_WHITE_BALANCE */
	0,		/* EXIF_SHORT_SCENE_CAPTURE_TYPE */
	0,		/* EXIF_SHORT_RESOLUTION_UNIT */
};

const rational_t V4L2JpegEncoder::defaultRationals[EXIF_COUNT(RATIONAL)] = {
	{ 0, 1 },	/* EXIF_RATIONAL_EXPOSURE_TIME */
	{ 0, 1 },	/* EXIF_RATIONAL_FNUMBER */
	{ 0, 1 },	/* EXIF_RATIONAL_APERTURE */
	{ 0, 1 },	/* EXIF_RATIONAL_MAX_APERTURE */
	{ 0, 1 },	/* EXIF_RATIONAL_FOCAL_LENGTH */
	{ 0, 1 },	/* EXIF_RATIONAL_X_RESOLUTION */
	{ 0, 1 }	/* EXIF_RATIONAL_Y_RESOLUTION */
};

const srational_t V4L2JpegEncoder::defaultSrationals[EXIF_COUNT(SRATIONAL)] = {
	{ 0, 1 },	/* EXIF_SRATIONAL_SHUTTER_SPEED */
	{ 0, 1 },	/* EXIF_SRATIONAL_BRIGHTNESS */
	{ 0, 1 }	/* EXIF_SRATIONAL_EXPOSURE_BIAS */
};

const V4L2JpegEncoder::ExifTagMapEntry V4L2JpegEncoder::exifIfd0TagMap[] = {
	{ EXIF_STRING_MAKER, EXIF_TAG_MAKE },
	{ EXIF_STRING_MODEL, EXIF_TAG_MODEL },
	{ EXIF_SHORT_ORIENTATION, EXIF_TAG_ORIENTATION },
	{ EXIF_STRING_SOFTWARE, EXIF_TAG_SOFTWARE },
	{ EXIF_STRING_DATE_TIME, EXIF_TAG_DATE_TIME },
	{ EXIF_SHORT_YCBCR_POSITIONING, EXIF_TAG_YCBCR_POSITIONING }
};

const V4L2JpegEncoder::ExifTagMapEntry V4L2JpegEncoder::exifIfdExifTagMap[] = {
	{ EXIF_RATIONAL_EXPOSURE_TIME, EXIF_TAG_EXPOSURE_TIME },
	{ EXIF_RATIONAL_FNUMBER, EXIF_TAG_FNUMBER },
	{ EXIF_SHORT_EXPOSURE_PROGRAM, EXIF_TAG_EXPOSURE_PROGRAM },
	{ EXIF_SHORT_ISO_SPEED_RATING, EXIF_TAG_ISO_SPEED_RATING },
	{ EXIF_STRING_EXIF_VERSION, EXIF_TAG_EXIF_VERSION },
	{ EXIF_STRING_DATE_TIME, EXIF_TAG_DATE_TIME_ORG },
	{ EXIF_STRING_DATE_TIME, EXIF_TAG_DATE_TIME_DIGITIZE },
	{ EXIF_SRATIONAL_SHUTTER_SPEED, EXIF_TAG_SHUTTER_SPEED },
	{ EXIF_RATIONAL_APERTURE, EXIF_TAG_APERTURE },
	{ EXIF_SRATIONAL_BRIGHTNESS, EXIF_TAG_BRIGHTNESS },
	{ EXIF_SRATIONAL_EXPOSURE_BIAS, EXIF_TAG_EXPOSURE_BIAS },
	{ EXIF_RATIONAL_MAX_APERTURE, EXIF_TAG_MAX_APERTURE },
	{ EXIF_SHORT_METERING_MODE, EXIF_TAG_METERING_MODE },
	{ EXIF_SHORT_FLASH, EXIF_TAG_FLASH },
	{ EXIF_RATIONAL_FOCAL_LENGTH, EXIF_TAG_FOCAL_LENGTH },
	{ EXIF_STRING_USER_COMMENT, EXIF_TAG_USER_COMMENT },
	{ EXIF_SHORT_COLOR_SPACE, EXIF_TAG_COLOR_SPACE },
	{ EXIF_LONG_PIXEL_X_DIMENSION, EXIF_TAG_PIXEL_X_DIMENSION },
	{ EXIF_LONG_PIXEL_Y_DIMENSION, EXIF_TAG_PIXEL_Y_DIMENSION },
	{ EXIF_SHORT_EXPOSURE_MODE, EXIF_TAG_EXPOSURE_MODE },
	{ EXIF_SHORT_WHITE_BALANCE, EXIF_TAG_WHITE_BALANCE },
	{ EXIF_SHORT_SCENE_CAPTURE_TYPE, EXIF_TAG_SCENCE_CAPTURE_TYPE }
};

const V4L2JpegEncoder::ExifTagMapEntry V4L2JpegEncoder::exifIfd1TagMap[] = {
	{ EXIF_SHORT_ORIENTATION, EXIF_TAG_ORIENTATION },
	{ EXIF_RATIONAL_X_RESOLUTION, EXIF_TAG_X_RESOLUTION },
	{ EXIF_RATIONAL_Y_RESOLUTION, EXIF_TAG_Y_RESOLUTION },
	{ EXIF_SHORT_RESOLUTION_UNIT, EXIF_TAG_RESOLUTION_UNIT },
};

V4L2JpegEncoder::V4L2JpegEncoder(const char *path) :
	gpsData(0),
	jpegQuality(JPEG_MAX_QUALITY),
	jpegSubsampling(V4L2_JPEG_CHROMA_SUBSAMPLING_422),
	path(path),
	device(0)
{
	TRACE();

	for (uint32_t i = 0; i < ARRAY_SIZE(defaultStrings); ++i)
		strncpy(exifStrings[i], defaultStrings[i], EXIF_STRING_LENGTH);

	memcpy(exifLongs, defaultLongs, sizeof(defaultLongs));
	memcpy(exifShorts, defaultShorts, sizeof(defaultShorts));
	memcpy(exifRationals, defaultRationals, sizeof(defaultRationals));
	memcpy(exifSrationals, defaultSrationals, sizeof(defaultSrationals));

	output.format = V4L2_PIX_FMT_JPEG;
}

V4L2JpegEncoder::~V4L2JpegEncoder()
{
	TRACE();
	/* Nothing to do here */
}

int V4L2JpegEncoder::setGpsData(const JpegGpsData *data)
{
	TRACE();

	gpsData = data;

	return 0;
}

int V4L2JpegEncoder::setExifTag(uint32_t id, const char *value)
{
	TRACE();

	if (id <= EXIF_STRING_BASE || id >= EXIF_STRING_MAX)
		return -1;

	strncpy(exifStrings[id - EXIF_STRING_BASE - 1],
						value, EXIF_STRING_LENGTH);

	return 0;
}

int V4L2JpegEncoder::setExifTag(uint32_t id, uint32_t value)
{
	TRACE();

	if (id <= EXIF_LONG_BASE || id >= EXIF_LONG_MAX)
		return -1;

	exifLongs[id - EXIF_LONG_BASE - 1] = value;

	return 0;
}

int V4L2JpegEncoder::setExifTag(uint32_t id, uint16_t value)
{
	TRACE();

	if (id <= EXIF_SHORT_BASE || id >= EXIF_SHORT_MAX)
		return -1;

	exifShorts[id - EXIF_SHORT_BASE - 1] = value;

	return 0;
}

int V4L2JpegEncoder::setExifTag(uint32_t id,
			uint32_t numerator, uint32_t denominator)
{
	TRACE();

	if (id <= EXIF_RATIONAL_BASE || id >= EXIF_RATIONAL_MAX)
		return -1;

	if (!denominator)
		return -1;

	exifRationals[id - EXIF_RATIONAL_BASE - 1].num = numerator;
	exifRationals[id - EXIF_RATIONAL_BASE - 1].den = denominator;

	return 0;
}

int V4L2JpegEncoder::setExifTag(uint32_t id,
			int32_t numerator, int32_t denominator)
{
	TRACE();

	if (id <= EXIF_SRATIONAL_BASE || id >= EXIF_SRATIONAL_MAX)
		return -1;

	if (!denominator)
		return -1;

	exifSrationals[id - EXIF_SRATIONAL_BASE - 1].num = numerator;
	exifSrationals[id - EXIF_SRATIONAL_BASE - 1].den = denominator;

	return 0;
}

int V4L2JpegEncoder::setInput(sp<V4L2Allocation> allocation,
			uint32_t width, uint32_t height, unsigned int format)
{
	TRACE();

	if (width > JPEG_MAX_SIZE || height > JPEG_MAX_SIZE)
		return -1;

	switch (format) {
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_RGB565:
		break;
	default:
		ERR("unsupported input format %d", format);
		return -1;
	}

	input.width = width;
	input.height = height;
	input.format = format;
	input.allocation = allocation;

	return 0;
}

int V4L2JpegEncoder::setThumbnail(sp<V4L2Allocation> allocation,
			uint32_t width, uint32_t height, bool ycbcr422)
{
	TRACE();

	if (width > JPEG_MAX_SIZE || height > JPEG_MAX_SIZE)
		return -1;

	thumbnail.width = width;
	thumbnail.height = height;
	thumbnail.format = (ycbcr422) ? V4L2_PIX_FMT_YUYV : V4L2_PIX_FMT_RGB565;
	thumbnail.allocation = allocation;

	return 0;
}

int V4L2JpegEncoder::setOutput(sp<V4L2Allocation> allocation,
					uint32_t quality, bool ycbcr422)
{
	TRACE();

	if (quality > JPEG_MAX_QUALITY)
		return -1;

	output.allocation = allocation;

	jpegQuality = quality;
	jpegSubsampling = (ycbcr422) ? V4L2_JPEG_CHROMA_SUBSAMPLING_422
					: V4L2_JPEG_CHROMA_SUBSAMPLING_420;

	return 0;
}

int V4L2JpegEncoder::initDevice(unsigned direction,
						const ImageConfig *config)
{
	int ret;

	TRACE();

	ret = device->enumFormat(direction, config->format);
	if (ret < 0) {
		ERR("failed to enum formats");
		return -1;
	}

	ret = device->setFormat(direction,
				config->width, config->height, config->format);
	if (ret < 0) {
		ERR("failed to set format");
		return -1;
	}

	ret = device->reqBufs(direction, config->allocation);
	if (ret < 0) {
		ERR("failed to request buffers");
		return -1;
	}

	ret = device->queueBuf(direction, 0);
	if (ret < 0) {
		ERR("failed to queue buffer");
		return -1;
	}

	ret = device->setStream(direction, true);
	if (ret < 0) {
		ERR("Failed to enable streaming");
		return -1;
	}

	return 0;
}

int V4L2JpegEncoder::cleanupDevice(unsigned direction)
{
	int ret;

	TRACE();

	ret = device->dequeueBuf(direction);
	if (ret < 0) {
		ERR("failed to dequeue buffer");
		return -1;
	}

	ret = device->setStream(direction, false);
	if (ret < 0) {
		ERR("Failed to stop output stream");
		return -1;
	}

	ret = device->reqBufs(direction, 0);
	if (ret < 0) {
		ERR("failed to free buffers");
		return -1;
	}

	return 0;
}


int V4L2JpegEncoder::encodeImage(const ImageConfig *input)
{
	int ret;

	TRACE();

	output.width = input->width;
	output.height = input->height;

	ret = initDevice(V4L2_OUTPUT, input);
	if (ret < 0) {
		ERR("Failed to configure output stream");
		return ret;
	}

	ret = initDevice(V4L2_CAPTURE, &output);
	if (ret < 0) {
		ERR("Failed to configure capture stream");
		cleanupDevice(V4L2_OUTPUT);
		return ret;
	}

	ret = device->pollDevice(POLLIN | POLLERR, 1000);

	cleanupDevice(V4L2_OUTPUT);
	cleanupDevice(V4L2_CAPTURE);

	if (!(ret & POLLIN)) {
		ERR("No frames received before timeout");
		return -1;
	}

	return 0;
}

void V4L2JpegEncoder::pushIfdTag(ExifIfd &ifd,
					uint32_t key, uint16_t tag)
{
	uint32_t idx = EXIF_INDEX(key);

	TRACE();

	if (idx < 1)
		return;

	switch (EXIF_TYPE(key)) {
	case EXIF_TYPE_ASCII:
		ifd.push(tag, exifStrings[idx - 1]);
		break;
	case EXIF_TYPE_SHORT:
		ifd.push(tag, exifShorts[idx - 1]);
		break;
	case EXIF_TYPE_LONG:
		ifd.push(tag, exifLongs[idx - 1]);
		break;
	case EXIF_TYPE_RATIONAL:
		ifd.push(tag, &exifRationals[idx - 1]);
		break;
	case EXIF_TYPE_SRATIONAL:
		ifd.push(tag, &exifSrationals[idx - 1]);
		break;
	}
}

int V4L2JpegEncoder::buildExif(sp<Buffer> exifData, sp<Buffer> thumbData)
{
	uint8_t *ptr = exifData->getData();
	uint8_t *tiffHeader;
	uint8_t *app1size;
	uint16_t size;

	TRACE();

	memcpy(ptr, APP1_MARKER, sizeof(APP1_MARKER));
	ptr += sizeof(APP1_MARKER);
	app1size = ptr;
	ptr += 2;
	memcpy(ptr, EXIF_HEADER, sizeof(EXIF_HEADER));
	ptr += sizeof(EXIF_HEADER);
	tiffHeader = ptr;
	memcpy(ptr, TIFF_HEADER, sizeof(TIFF_HEADER));
	ptr += sizeof(TIFF_HEADER);

	ExifIfd ifd0(ptr, ARRAY_SIZE(exifIfd0TagMap) + 3 + !!gpsData,
							ptr - tiffHeader);
	for (uint32_t i = 0; i < ARRAY_SIZE(exifIfd0TagMap); ++i)
		pushIfdTag(ifd0, exifIfd0TagMap[i].key, exifIfd0TagMap[i].tag);
	ptr += ifd0.size();
	ifd0.push(EXIF_TAG_IMAGE_WIDTH, input.width);
	ifd0.push(EXIF_TAG_IMAGE_HEIGHT, input.height);
	ifd0.push(EXIF_TAG_EXIF_IFD_POINTER, (uint32_t)(ptr - tiffHeader));

	ExifIfd ifdExif(ptr, ARRAY_SIZE(exifIfdExifTagMap), ptr - tiffHeader);
	for (uint32_t i = 0; i < ARRAY_SIZE(exifIfdExifTagMap); ++i)
		pushIfdTag(ifdExif, exifIfdExifTagMap[i].key,
						exifIfdExifTagMap[i].tag);
	ptr += ifdExif.size();

	if (gpsData) {
		ifd0.push(EXIF_TAG_GPS_IFD_POINTER,
						(uint32_t)(ptr - tiffHeader));

		ExifIfd ifdGps(ptr, 9, ptr - tiffHeader);
		ifdGps.push(EXIF_TAG_GPS_VERSION_ID,
				EXIF_TYPE_BYTE, 4, gpsData->versionId);
		ifdGps.push(EXIF_TAG_GPS_LATITUDE_REF,
				EXIF_TYPE_ASCII, 2, gpsData->latitudeRef);
		ifdGps.push(EXIF_TAG_GPS_LATITUDE,
				EXIF_TYPE_RATIONAL, 3, gpsData->latitude);
		ifdGps.push(EXIF_TAG_GPS_LONGITUDE_REF,
				EXIF_TYPE_ASCII, 2, gpsData->longitudeRef);
		ifdGps.push(EXIF_TAG_GPS_LONGITUDE,
				EXIF_TYPE_RATIONAL, 3, gpsData->longitude);
		ifdGps.push(EXIF_TAG_GPS_ALTITUDE_REF,
				(uint8_t)gpsData->altitudeRef);
		ifdGps.push(EXIF_TAG_GPS_ALTITUDE, &gpsData->altitude);
		ifdGps.push(EXIF_TAG_GPS_TIMESTAMP,
				EXIF_TYPE_RATIONAL, 3, gpsData->timestamp);
		ifdGps.push(EXIF_TAG_GPS_DATESTAMP,
				EXIF_TYPE_ASCII, 11, gpsData->datestamp);

		ptr += ifdGps.size();
	}

	if (thumbData != 0) {
		ifd0.link(ptr - tiffHeader);

		ExifIfd ifd1(ptr, ARRAY_SIZE(exifIfd1TagMap) + 5
							, ptr - tiffHeader);

		ifd1.push(EXIF_TAG_IMAGE_WIDTH, (uint32_t)thumbnail.width);
		ifd1.push(EXIF_TAG_IMAGE_HEIGHT, (uint32_t)thumbnail.height);
		ifd1.push(EXIF_TAG_COMPRESSION_SCHEME,
						(uint16_t)EXIF_DEF_COMPRESSION);

		for (uint32_t i = 0; i < ARRAY_SIZE(exifIfd1TagMap); ++i)
			pushIfdTag(ifdExif, exifIfd1TagMap[i].key,
							exifIfd1TagMap[i].tag);

		ptr += ifd1.size();

		ifd1.push(EXIF_TAG_JPEG_INTERCHANGE_FORMAT,
						(uint32_t)(ptr - tiffHeader));
		ifd1.push(EXIF_TAG_JPEG_INTERCHANGE_FORMAT_LEN,
							thumbData->getSize());

		memcpy(ptr, thumbData->getData(), thumbData->getSize());
		ptr += thumbData->getSize();
	}

	size = ptr - app1size;
	app1size[0] = size >> 8;
	app1size[1] = size & 0xff;

	return ptr - exifData->getData();
}

int V4L2JpegEncoder::openDevice(void)
{
	int ret;

	TRACE();

	device = new V4L2Device(path);
	if (!device || !device->initCheck()) {
		ERR("failed to create V4L2 device object");
		goto error;
	}

	ret = device->setBufType(V4L2_CAPTURE, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (ret < 0) {
		ERR("failed to set capture buffer type");
		goto error;
	}

	ret = device->setBufType(V4L2_OUTPUT, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	if (ret < 0) {
		ERR("failed to set output buffer type");
		goto error;
	}

	return 0;

error:
	delete device;
	device = 0;
	return -1;
}

void V4L2JpegEncoder::closeDevice(void)
{
	TRACE();
	delete device;
	device = 0;
}

int V4L2JpegEncoder::run(void)
{
	int ret;
	sp<Buffer> thumbData;
	sp<Buffer> exifData;

	TRACE();

	ret = openDevice();
	if (ret < 0) {
		ERR("Failed to open JPEG encoder device");
		return -1;
	}

	if (thumbnail.allocation != 0) {
		ret = encodeImage(&thumbnail);
		if (ret >= 0) {
			V4L2Buffer *buf =
					output.allocation->getBuffer(0);
			thumbData = new Buffer(buf->getUsed(),
							buf->getAddress());
		}
	}

	ret = encodeImage(&input);
	if (ret < 0) {
		ERR("Failed to encode JPEG image");
		closeDevice();
		return -1;
	}

	closeDevice();

	uint32_t exifSize = EXIF_SIZE;
	if (thumbData != 0)
		exifSize += thumbData->getSize();

	exifData = new Buffer(exifSize);
	if (exifData == 0 || !exifData->initCheck()) {
		ERR("Failed to allocate exif buffer");
		return -1;
	}
	exifData->zero();

	exifSize = buildExif(exifData, thumbData);

	V4L2Buffer *buf = output.allocation->getBuffer(0);
	char *addr = (char *)buf->getAddress();
	memmove(addr + exifSize, addr, buf->getUsed());
	memcpy(addr, addr + exifSize, 2);
	memcpy(addr + 2, exifData->getData(), exifSize);

	return buf->getUsed() + exifSize;
}

void V4L2JpegEncoder::cleanup(void)
{
	TRACE();

	input.allocation.clear();
	thumbnail.allocation.clear();
	output.allocation.clear();
}
