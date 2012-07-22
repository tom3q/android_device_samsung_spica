/*
 * Copyright Samsung Electronics Co.,LTD.
 * Copyright (C) 2010 The Android Open Source Project
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
#ifndef ANDROID_HARDWARE_EXIF_H
#define ANDROID_HARDWARE_EXIF_H

#include <math.h>

#define EXIF_LOG2(x)                    (log((double)(x)) / log(2.0))
#define APEX_FNUM_TO_APERTURE(x)        ((int)(EXIF_LOG2((double)(x)) * 2 + 0.5))
#define APEX_EXPOSURE_TO_SHUTTER(x)     ((x) >= 1 ?                                 \
                                        (int)(-(EXIF_LOG2((double)(x)) + 0.5)) :    \
                                        (int)(-(EXIF_LOG2((double)(x)) - 0.5)))
#define APEX_ISO_TO_FILMSENSITIVITY(x)  ((int)(EXIF_LOG2((x) / 3.125) + 0.5))

#define NUM_SIZE                    2
#define IFD_SIZE                    12
#define OFFSET_SIZE                 4

#define NUM_0TH_IFD_TIFF            10
#define NUM_0TH_IFD_EXIF            22
#define NUM_0TH_IFD_GPS             10
#define NUM_1TH_IFD_TIFF            9

/* Type */
enum {
	EXIF_TYPE_BYTE = 1,
	EXIF_TYPE_ASCII,
	EXIF_TYPE_SHORT,
	EXIF_TYPE_LONG,
	EXIF_TYPE_RATIONAL,
	EXIF_TYPE_RESERVED_6,
	EXIF_TYPE_UNDEFINED,
	EXIF_TYPE_RESERVED_8,
	EXIF_TYPE_SLONG,
	EXIF_TYPE_SRATIONAL,

	EXIF_TYPE_NUM
};

#define EXIF_FILE_SIZE              28800

/* 0th IFD TIFF Tags */
#define EXIF_TAG_IMAGE_WIDTH                    0x0100
#define EXIF_TAG_IMAGE_HEIGHT                   0x0101
#define EXIF_TAG_MAKE                           0x010f
#define EXIF_TAG_MODEL                          0x0110
#define EXIF_TAG_ORIENTATION                    0x0112
#define EXIF_TAG_SOFTWARE                       0x0131
#define EXIF_TAG_DATE_TIME                      0x0132
#define EXIF_TAG_YCBCR_POSITIONING              0x0213
#define EXIF_TAG_EXIF_IFD_POINTER               0x8769
#define EXIF_TAG_GPS_IFD_POINTER                0x8825

/* 0th IFD Exif Private Tags */
#define EXIF_TAG_EXPOSURE_TIME                  0x829A
#define EXIF_TAG_FNUMBER                        0x829D
#define EXIF_TAG_EXPOSURE_PROGRAM               0x8822
#define EXIF_TAG_ISO_SPEED_RATING               0x8827
#define EXIF_TAG_EXIF_VERSION                   0x9000
#define EXIF_TAG_DATE_TIME_ORG                  0x9003
#define EXIF_TAG_DATE_TIME_DIGITIZE             0x9004
#define EXIF_TAG_SHUTTER_SPEED                  0x9201
#define EXIF_TAG_APERTURE                       0x9202
#define EXIF_TAG_BRIGHTNESS                     0x9203
#define EXIF_TAG_EXPOSURE_BIAS                  0x9204
#define EXIF_TAG_MAX_APERTURE                   0x9205
#define EXIF_TAG_METERING_MODE                  0x9207
#define EXIF_TAG_FLASH                          0x9209
#define EXIF_TAG_FOCAL_LENGTH                   0x920A
#define EXIF_TAG_USER_COMMENT                   0x9286
#define EXIF_TAG_COLOR_SPACE                    0xA001
#define EXIF_TAG_PIXEL_X_DIMENSION              0xA002
#define EXIF_TAG_PIXEL_Y_DIMENSION              0xA003
#define EXIF_TAG_EXPOSURE_MODE                  0xA402
#define EXIF_TAG_WHITE_BALANCE                  0xA403
#define EXIF_TAG_SCENCE_CAPTURE_TYPE            0xA406

/* 0th IFD GPS Info Tags */
#define EXIF_TAG_GPS_VERSION_ID                 0x0000
#define EXIF_TAG_GPS_LATITUDE_REF               0x0001
#define EXIF_TAG_GPS_LATITUDE                   0x0002
#define EXIF_TAG_GPS_LONGITUDE_REF              0x0003
#define EXIF_TAG_GPS_LONGITUDE                  0x0004
#define EXIF_TAG_GPS_ALTITUDE_REF               0x0005
#define EXIF_TAG_GPS_ALTITUDE                   0x0006
#define EXIF_TAG_GPS_TIMESTAMP                  0x0007
#define EXIF_TAG_GPS_PROCESSING_METHOD          0x001B
#define EXIF_TAG_GPS_DATESTAMP                  0x001D

/* 1th IFD TIFF Tags */
#define EXIF_TAG_COMPRESSION_SCHEME             0x0103
#define EXIF_TAG_X_RESOLUTION                   0x011A
#define EXIF_TAG_Y_RESOLUTION                   0x011B
#define EXIF_TAG_RESOLUTION_UNIT                0x0128
#define EXIF_TAG_JPEG_INTERCHANGE_FORMAT        0x0201
#define EXIF_TAG_JPEG_INTERCHANGE_FORMAT_LEN    0x0202


typedef enum {
	EXIF_ORIENTATION_UP     = 1,
	EXIF_ORIENTATION_90     = 6,
	EXIF_ORIENTATION_180    = 3,
	EXIF_ORIENTATION_270    = 8,
} ExifOrientationType;

typedef enum {
	EXIF_SCENE_STANDARD,
	EXIF_SCENE_LANDSCAPE,
	EXIF_SCENE_PORTRAIT,
	EXIF_SCENE_NIGHT,
} CamExifSceneCaptureType;

typedef enum {
	EXIF_METERING_UNKNOWN,
	EXIF_METERING_AVERAGE,
	EXIF_METERING_CENTER,
	EXIF_METERING_SPOT,
	EXIF_METERING_MULTISPOT,
	EXIF_METERING_PATTERN,
	EXIF_METERING_PARTIAL,
	EXIF_METERING_OTHER     = 255,
} CamExifMeteringModeType;

typedef enum {
	EXIF_EXPOSURE_AUTO,
	EXIF_EXPOSURE_MANUAL,
	EXIF_EXPOSURE_AUTO_BRACKET,
} CamExifExposureModeType;

typedef enum {
	EXIF_WB_AUTO,
	EXIF_WB_MANUAL,
} CamExifWhiteBalanceType;

/* Values */
#define EXIF_DEF_MAKER          "SAMSUNG"
#define EXIF_DEF_MODEL          "GT-I5700"
#define EXIF_DEF_SOFTWARE       "SPICACM"
#define EXIF_DEF_EXIF_VERSION   "0220"
#define EXIF_DEF_USERCOMMENTS   "ASCII\0\0\0User comments"

#define EXIF_DEF_YCBCR_POSITIONING  1   /* centered */
#define EXIF_DEF_FNUMBER_NUM        26  /* 2.6 */
#define EXIF_DEF_FNUMBER_DEN        10
#define EXIF_DEF_EXPOSURE_PROGRAM   3   /* aperture priority */
#define EXIF_DEF_FOCAL_LEN_NUM      278 /* 2.78mm */
#define EXIF_DEF_FOCAL_LEN_DEN      100
#define EXIF_DEF_FLASH              0   /* O: off, 1: on*/
#define EXIF_DEF_COLOR_SPACE        1
#define EXIF_DEF_EXPOSURE_MODE      EXIF_EXPOSURE_AUTO
#define EXIF_DEF_APEX_DEN           10

#define EXIF_DEF_COMPRESSION        6
#define EXIF_DEF_RESOLUTION_NUM     72
#define EXIF_DEF_RESOLUTION_DEN     1
#define EXIF_DEF_RESOLUTION_UNIT    2   /* inches */

typedef struct {
	uint32_t num;
	uint32_t den;
} rational_t;

typedef struct {
	int32_t num;
	int32_t den;
} srational_t;

#endif /* ANDROID_HARDWARE_EXIF_H */
