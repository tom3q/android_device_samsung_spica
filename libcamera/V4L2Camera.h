/*
 * Generic V4L2 libcamera implementation
 *
 * Copyright 2008, The Android Open Source Project
 * Copyright 2010, Samsung Electronics Co. LTD
 * Copyright 2012, Tomasz Figa <tomasz.figa at gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_HARDWARE_CAMERA_V4L2_H
#define ANDROID_HARDWARE_CAMERA_V4L2_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/stat.h>

#include <linux/videodev2.h>

#include <camera/CameraHardwareInterface.h>

#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <binder/MemoryHeapPmem.h>

#include "V4L2Device.h"
#include "V4L2JpegEncoder.h"
#include "Exif.h"

namespace android {

#define JOIN(x, y)			JOIN_AGAIN(x, y)
#define JOIN_AGAIN(x, y)		x ## y

#define S5K4CAGX_PREVIEW_WIDTH		640
#define S5K4CAGX_PREVIEW_HEIGHT		480
#define S5K4CAGX_SNAPSHOT_WIDTH		2048
#define S5K4CAGX_SNAPSHOT_HEIGHT	1536

#define S5K4CAGX_THUMBNAIL_WIDTH	320
#define S5K4CAGX_THUMBNAIL_HEIGHT	240
#define S5K4CAGX_THUMBNAIL_BPP		16

#define S5K4CAGX_FOCAL_LENGTH		343

#define BACK_CAM S5K4CAGX

#if !defined(BACK_CAM)
#error "Please define the Camera module"
#endif

#define MAX_BACK_CAMERA_PREVIEW_WIDTH	JOIN(BACK_CAM,_PREVIEW_WIDTH)
#define MAX_BACK_CAMERA_PREVIEW_HEIGHT	JOIN(BACK_CAM,_PREVIEW_HEIGHT)
#define MAX_BACK_CAMERA_SNAPSHOT_WIDTH	JOIN(BACK_CAM,_SNAPSHOT_WIDTH)
#define MAX_BACK_CAMERA_SNAPSHOT_HEIGHT	JOIN(BACK_CAM,_SNAPSHOT_HEIGHT)
#define BACK_CAMERA_THUMBNAIL_WIDTH	JOIN(BACK_CAM,_THUMBNAIL_WIDTH)
#define BACK_CAMERA_THUMBNAIL_HEIGHT	JOIN(BACK_CAM,_THUMBNAIL_HEIGHT)
#define BACK_CAMERA_THUMBNAIL_BPP	JOIN(BACK_CAM,_THUMBNAIL_BPP)
#define BACK_CAMERA_FOCAL_LENGTH	JOIN(BACK_CAM,_FOCAL_LENGTH)

#define JPEG_DEV_NAME			"s3c-jpeg.enc"
#define CAMERA_DEV_NAME			"s3c-fimc.0.capture"

#define PMEM_DEV_NAME			"/dev/pmem_gpu1"

/* One currently being processed and four for FIMC */
#define REC_BUFFERS			5

/*
 * S5K4CA private controls
 */
#define V4L2_CID_S5K4CA_WB_PRESET	(V4L2_CTRL_CLASS_CAMERA | 0x1001)
enum {
	S5K4CA_WB_SUNNY = 0,
	S5K4CA_WB_CLOUDY,
	S5K4CA_WB_TUNGSTEN,
	S5K4CA_WB_FLUORESCENT
};

#define V4L2_CID_S5K4CA_ISO		(V4L2_CTRL_CLASS_CAMERA | 0x1002)
enum {
	S5K4CA_ISO_AUTO = 0,
	S5K4CA_ISO_50,
	S5K4CA_ISO_100,
	S5K4CA_ISO_200,
	S5K4CA_ISO_400,
	S5K4CA_ISO_NIGHT,
	S5K4CA_ISO_SPORT
};

#define V4L2_CID_S5K4CA_METERING	(V4L2_CTRL_CLASS_CAMERA | 0x1003)
enum {
	S5K4CA_METERING_CENTER = 0,
	S5K4CA_METERING_SPOT,
	S5K4CA_METERING_MATRIX
};

#define V4L2_CID_S5K4CA_FRAME_RATE	(V4L2_CTRL_CLASS_CAMERA | 0x1004)
#define V4L2_CID_S5K4CA_CAPTURE		(V4L2_CTRL_CLASS_CAMERA | 0x1005)
#define V4L2_CID_S5K4CA_GLAMOUR		(V4L2_CTRL_CLASS_CAMERA | 0x1006)
#define V4L2_CID_S5K4CA_NIGHTSHOT	(V4L2_CTRL_CLASS_CAMERA | 0x1007)

/*
 * Internal data structures
 */
struct SceneControl {
	unsigned int control;
	int value;
};

enum {
	CAMERA_CTRL_FRAME_RATE = 0,
	CAMERA_CTRL_AUTO_WB,
	CAMERA_CTRL_WB_PRESET,
	CAMERA_CTRL_BRIGHTNESS,
	CAMERA_CTRL_COLORFX,
	CAMERA_CTRL_ISO,
	CAMERA_CTRL_CONTRAST,
	CAMERA_CTRL_SATURATION,
	CAMERA_CTRL_SHARPNESS,
	CAMERA_CTRL_GLAMOUR,
	CAMERA_CTRL_METERING,
	CAMERA_CTRL_NIGHTSHOT,
	CAMERA_CTRL_FOCUS_MODE,
	CAMERA_CTRL_AUTO_EXPOSURE,
	CAMERA_CTRL_JPEG_QUALITY,
	CAMERA_CTRL_EXIF_ORIENTATION,
	CAMERA_CTRL_GPS_LATITUDE,
	CAMERA_CTRL_GPS_LONGITUDE,
	CAMERA_CTRL_GPS_ALTITUDE,
	CAMERA_CTRL_GPS_TIMESTAMP,
	CAMERA_CTRL_SCENE_MODE,
	/* Number of controls */
	CAMERA_CTRL_NUM
};

enum {
	CAMERA_SCENE_NONE = 0,
	CAMERA_SCENE_PORTRAIT,
	CAMERA_SCENE_LANDSCAPE,
	CAMERA_SCENE_SPORTS,
	CAMERA_SCENE_PARTY_INDOOR,
	CAMERA_SCENE_BEACH_SNOW,
	CAMERA_SCENE_SUNSET_CANDLE,
	CAMERA_SCENE_NIGHTSHOT,
	CAMERA_SCENE_FIREWORKS,
	/* Number of scene modes */
	CAMERA_SCENE_NUM
};

class V4L2Camera {
	static const enum v4l2_buf_type BUF_TYPE =
					V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	V4L2Device *device;
	V4L2JpegEncoder *jpegEncoder;

	int cameraId;

	JpegGpsData gpsData;

	bool autoFocusDone;
	bool previewStarted;
	bool recordingStarted;

	int previewFormat;
	int previewTargetFormat;
	int previewWidth;
	int previewHeight;
	sp<V4L2Allocation> previewAllocation;
	void *previewConvBuffer;

	int snapshotFormat;
	int snapshotTargetFormat;
	int snapshotWidth;
	int snapshotHeight;

	int recordingWidth;
	int recordingHeight;
	sp<V4L2Allocation> recordAllocation;

	int jpegThumbnailWidth;
	int jpegThumbnailHeight;

	int prevBufIdx;

	struct V4L2Buffer captureBuf;

	V4L2Camera();
	~V4L2Camera();

	void setExifChangedAttribute();
	void setExifFixedAttribute();
	int previewPoll(void);

	void convertYUV420ToNV21(void *buffer, void *tmp,
							int width, int height);
	int convertFrame(V4L2Buffer *buffer, void *convBuffer,
					int width, int height, int dstFormat);

public:
	enum CAMERA_ID {
		CAMERA_ID_BACK = 0,
	};

	static V4L2Camera *getInstance(void)
	{
		static V4L2Camera *singleton = 0;
		if (!singleton)
			singleton = new V4L2Camera();
		return singleton;
	}

private:
	static const unsigned int ctrlTable[CAMERA_CTRL_NUM];
	int ctrlValues[CAMERA_CTRL_NUM];
	static const SceneControl *sceneTable[CAMERA_SCENE_NUM];

	void initControlValues(void);
	void dumpData(const void *data, size_t size, const char *path);

public:
	status_t dump(int fd, const Vector<String16>& args);

	int isOpened(void) const;
	int openCamera(int index);
	void closeCamera(void);

	int getCameraId(void) { return cameraId; }

	int startPreview(void);
	int stopPreview(void);
	int getPreview(void);
	int setPreviewSize(unsigned int width,
				unsigned int height, int pixel_format);
	int getPreviewSize(unsigned int *width,
				unsigned int *height, unsigned int *frame_size);
	int getPreviewMaxSize(unsigned int *width,
				unsigned int *height);
	int getPreviewPixelFormat(void);
	sp<MemoryHeapBase> getBufferHeap(void);
	sp<MemoryBase> getBuffer(int index);

	int startRecord(void);
	int stopRecord(void);
	int getRecordFrame(void);
	int releaseRecordFrame(int index);
	unsigned int getRecPhyAddrY(int);
	unsigned int getRecPhyAddrC(int);
	int setRecordingSize(int width, int height);

	int setSnapshotSize(unsigned int width, unsigned int height);
	int getSnapshotSize(unsigned int *width,
				unsigned int *height, unsigned int *frame_size);
	int getSnapshotMaxSize(unsigned int *width,
							unsigned int *height);
	int setSnapshotPixelFormat(int pixel_format);
	int getSnapshotPixelFormat(void);
	int getSnapshotAndJpeg(void *yuv_buf, void *jpeg_buf,
						unsigned int *output_size);
	int endSnapshot(void);

	int setAutofocus(void);
	int cancelAutofocus(void);
	int getAutoFocusResult(void);

	int setJpegThumbnailSize(int width, int height);
	int getJpegThumbnailSize(int *width, int *height);

	int getControl(unsigned int ctrl);
	int setControl(unsigned int ctrl, int val);

	int setSceneMode(int scene_mode);
	int getSceneMode(void);

	/* WTF */
	int setGPSProcessingMethod(const char *gps_timestamp);

	const char* getCameraSensorName(void);

	void getThumbnailConfig(unsigned int *width,
				unsigned int *height, unsigned int *size);
};

}; /* namespace android */

#endif /* ANDROID_HARDWARE_CAMERA_V4L2_H */
