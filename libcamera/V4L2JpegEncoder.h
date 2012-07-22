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
#ifndef _V4L2JPEGENCODER_H
#define _V4L2JPEGENCODER_H

#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#include "V4L2Device.h"
#include "Exif.h"

#define JPEG_MAX_SIZE		(4096)
#define JPEG_MAX_QUALITY	(3)

class V4L2Buffer;

#define EXIF_TYPE_SHIFT		(5)
#define EXIF_TYPE_BASE(type)	((EXIF_TYPE_ ## type) << EXIF_TYPE_SHIFT)
#define EXIF_TYPE(id)		((id) >> EXIF_TYPE_SHIFT)
#define EXIF_IDX_MASK		((1 << EXIF_TYPE_SHIFT) - 1)
#define EXIF_INDEX(id)		((id) & EXIF_IDX_MASK)

#define EXIF_STRING_LENGTH	(128)

enum {
	EXIF_STRING_BASE = EXIF_TYPE_BASE(ASCII),
	EXIF_STRING_MAKER,
	EXIF_STRING_MODEL,
	EXIF_STRING_SOFTWARE,
	EXIF_STRING_EXIF_VERSION,
	EXIF_STRING_DATE_TIME,
	EXIF_STRING_USER_COMMENT,
	EXIF_STRING_MAX,

	EXIF_SHORT_BASE = EXIF_TYPE_BASE(SHORT),
	EXIF_SHORT_ORIENTATION,
	EXIF_SHORT_YCBCR_POSITIONING,
	EXIF_SHORT_EXPOSURE_PROGRAM,
	EXIF_SHORT_ISO_SPEED_RATING,
	EXIF_SHORT_METERING_MODE,
	EXIF_SHORT_FLASH,
	EXIF_SHORT_COLOR_SPACE,
	EXIF_SHORT_EXPOSURE_MODE,
	EXIF_SHORT_WHITE_BALANCE,
	EXIF_SHORT_SCENE_CAPTURE_TYPE,
	EXIF_SHORT_RESOLUTION_UNIT,
	EXIF_SHORT_MAX,

	EXIF_LONG_BASE = EXIF_TYPE_BASE(LONG),
	EXIF_LONG_PIXEL_X_DIMENSION,
	EXIF_LONG_PIXEL_Y_DIMENSION,
	EXIF_LONG_MAX,

	EXIF_RATIONAL_BASE = EXIF_TYPE_BASE(RATIONAL),
	EXIF_RATIONAL_EXPOSURE_TIME,
	EXIF_RATIONAL_FNUMBER,
	EXIF_RATIONAL_APERTURE,
	EXIF_RATIONAL_MAX_APERTURE,
	EXIF_RATIONAL_FOCAL_LENGTH,
	EXIF_RATIONAL_X_RESOLUTION,
	EXIF_RATIONAL_Y_RESOLUTION,
	EXIF_RATIONAL_MAX,

	EXIF_SRATIONAL_BASE = EXIF_TYPE_BASE(SRATIONAL),
	EXIF_SRATIONAL_SHUTTER_SPEED,
	EXIF_SRATIONAL_BRIGHTNESS,
	EXIF_SRATIONAL_EXPOSURE_BIAS,
	EXIF_SRATIONAL_MAX,
};

struct JpegGpsData {
	char versionId[4];
	char latitudeRef[2];
	rational_t latitude[3];
	char longitudeRef[2];
	rational_t longitude[3];
	char altitudeRef;
	rational_t altitude;
	rational_t timestamp[3];
	char datestamp[11];
};

#define EXIF_COUNT(type) (EXIF_ ## type ## _MAX - EXIF_ ## type ## _BASE - 1)

class V4L2JpegEncoder {
	static const char APP1_MARKER[];
	static const char EXIF_HEADER[];
	static const char TIFF_HEADER[];

	static const uint32_t EXIF_SIZE = 32*1024;

	char exifStrings[EXIF_COUNT(STRING)][EXIF_STRING_LENGTH];
	uint32_t exifLongs[EXIF_COUNT(LONG)];
	uint16_t exifShorts[EXIF_COUNT(SHORT)];
	rational_t exifRationals[EXIF_COUNT(RATIONAL)];
	srational_t exifSrationals[EXIF_COUNT(SRATIONAL)];

	static const char *const defaultStrings[EXIF_COUNT(STRING)];
	static const uint32_t defaultLongs[EXIF_COUNT(LONG)];
	static const uint16_t defaultShorts[EXIF_COUNT(SHORT)];
	static const rational_t defaultRationals[EXIF_COUNT(RATIONAL)];
	static const srational_t defaultSrationals[EXIF_COUNT(SRATIONAL)];

	struct ExifTagMapEntry {
		uint32_t key;
		uint16_t tag;
	};

	static const ExifTagMapEntry exifIfd0TagMap[];
	static const ExifTagMapEntry exifIfdExifTagMap[];
	static const ExifTagMapEntry exifIfd1TagMap[];

	const JpegGpsData *gpsData;

	class Buffer : public RefBase {
		uint8_t *data;
		uint32_t size;
	public:
		Buffer(uint32_t size, void *source = 0);
		~Buffer(void);

		inline uint8_t *getData(void) { return data; }
		inline uint32_t getSize(void) { return size; }
		inline bool initCheck(void) { return data != 0; }
		inline void zero(void) { memset(data, 0, size); }
	};

	struct ImageConfig {
		uint32_t width;
		uint32_t height;
		int format;
		sp<V4L2Allocation> allocation;

		ImageConfig() :
			width(0),
			height(0),
			format(V4L2_PIX_FMT_YUYV),
			allocation(0) {}
	};

	struct ExifIfdEntry {
		uint16_t tag;
		uint16_t type;
		uint32_t size;
		uint32_t data;
	};

	class ExifIfd {
		uint8_t *start;
		uint8_t *data;
		uint32_t space;
		uint32_t tags;
		uint32_t base;

		inline ExifIfdEntry *getEntry(uint32_t idx)
		{
			return (ExifIfdEntry *)(start + 2
						+ idx*sizeof(ExifIfdEntry));
		}
	public:
		ExifIfd(uint8_t *start, uint32_t space, uint32_t base);

		void push(uint16_t key, const char *value);
		void push(uint16_t key, uint8_t value);
		void push(uint16_t key, uint16_t value);
		void push(uint16_t key, uint32_t value);
		void push(uint16_t key, const rational_t *value);
		void push(uint16_t key, const srational_t *value);
		void push(uint16_t key, uint16_t type,
					uint32_t size, const void *value);

		void link(uint32_t next);
		uint32_t size(void) const;
	};

	ImageConfig input;
	ImageConfig thumbnail;
	ImageConfig output;

	int jpegQuality;
	int jpegSubsampling;

	const char *path;
	V4L2Device *device;

	int openDevice(void);
	int initDevice(unsigned direction, const ImageConfig *config);
	int cleanupDevice(unsigned direction);
	void closeDevice(void);

	int encodeImage(const ImageConfig *input);

	void pushIfdTag(ExifIfd &ifd, uint32_t key, uint16_t tag);
	int buildExif(sp<Buffer> exif, sp<Buffer> thumbnail);

public:
	V4L2JpegEncoder(const char *path);
	~V4L2JpegEncoder();

	int setGpsData(const JpegGpsData *data);

	int setExifTag(uint32_t id, const char *value);
	int setExifTag(uint32_t id, uint32_t value);
	int setExifTag(uint32_t id, uint16_t value);
	int setExifTag(uint32_t id,
			uint32_t numerator, uint32_t denominator);
	int setExifTag(uint32_t id,
			int32_t numerator, int32_t denominator);

	int setInput(sp<V4L2Allocation> allocation, uint32_t width,
			uint32_t height, unsigned int format);
	int setThumbnail(sp<V4L2Allocation> allocation,
			uint32_t width, uint32_t height, bool ycbcr422);
	int setOutput(sp<V4L2Allocation> allocation,
			uint32_t quality, bool ycbcr422);

	int run(void);
	void cleanup(void);
};

#endif /* _V4L2JPEGENCODER_H */
