/*
**
** Copyright 2008, The Android Open Source Project
** Copyright 2010, Samsung Electronics Co. LTD
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**		http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef ANDROID_HARDWARE_CAMERA_SEC_H
#define ANDROID_HARDWARE_CAMERA_SEC_H

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
#include <videodev2_samsung.h>

#include "JpegEncoder.h"

#include <camera/CameraHardwareInterface.h>

#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <binder/MemoryHeapPmem.h>

namespace android
{

#if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
#define LOG_CAMERA LOGD
#define LOG_CAMERA_PREVIEW LOGD

#if 0
#define LOG_TIME_DEFINE(n) \
		struct timeval time_start_##n, time_stop_##n; unsigned long log_time_##n = 0;

#define LOG_TIME_START(n) \
		gettimeofday(&time_start_##n, NULL);

#define LOG_TIME_END(n) \
		gettimeofday(&time_stop_##n, NULL); log_time_##n = measure_time(&time_start_##n, &time_stop_##n);

#define LOG_TIME(n) \
		log_time_##n
#endif

#else
#define LOG_CAMERA(...)
#define LOG_CAMERA_PREVIEW(...)
#define LOG_TIME_DEFINE(n)
#define LOG_TIME_START(n)
#define LOG_TIME_END(n)
#define LOG_TIME(n)
#endif

#define JOIN(x, y) JOIN_AGAIN(x, y)
#define JOIN_AGAIN(x, y) x ## y

#define S5K4CAGX_PREVIEW_WIDTH		1024
#define S5K4CAGX_PREVIEW_HEIGHT		768
#define S5K4CAGX_SNAPSHOT_WIDTH		2048
#define S5K4CAGX_SNAPSHOT_HEIGHT	1536

#define S5K4CAGX_POSTVIEW_WIDTH		2048
#define S5K4CAGX_POSTVIEW_WIDE_WIDTH	2048
#define S5K4CAGX_POSTVIEW_HEIGHT	1536
#define S5K4CAGX_POSTVIEW_BPP		16

#define S5K4CAGX_THUMBNAIL_WIDTH	320
#define S5K4CAGX_THUMBNAIL_HEIGHT	240
#define S5K4CAGX_THUMBNAIL_BPP		16

#define S5K4CAGX_FOCAL_LENGTH		343

#define BACK_CAM S5K4CAGX

#if !defined(BACK_CAM)
#error "Please define the Camera module"
#endif

#define MAX_BACK_CAMERA_PREVIEW_WIDTH		JOIN(BACK_CAM,_PREVIEW_WIDTH)
#define MAX_BACK_CAMERA_PREVIEW_HEIGHT		JOIN(BACK_CAM,_PREVIEW_HEIGHT)
#define MAX_BACK_CAMERA_SNAPSHOT_WIDTH		JOIN(BACK_CAM,_SNAPSHOT_WIDTH)
#define MAX_BACK_CAMERA_SNAPSHOT_HEIGHT		JOIN(BACK_CAM,_SNAPSHOT_HEIGHT)
#define BACK_CAMERA_POSTVIEW_WIDTH		JOIN(BACK_CAM,_POSTVIEW_WIDTH)
#define BACK_CAMERA_POSTVIEW_WIDE_WIDTH		JOIN(BACK_CAM,_POSTVIEW_WIDE_WIDTH)
#define BACK_CAMERA_POSTVIEW_HEIGHT		JOIN(BACK_CAM,_POSTVIEW_HEIGHT)
#define BACK_CAMERA_POSTVIEW_BPP		JOIN(BACK_CAM,_POSTVIEW_BPP)
#define BACK_CAMERA_THUMBNAIL_WIDTH		JOIN(BACK_CAM,_THUMBNAIL_WIDTH)
#define BACK_CAMERA_THUMBNAIL_HEIGHT		JOIN(BACK_CAM,_THUMBNAIL_HEIGHT)
#define BACK_CAMERA_THUMBNAIL_BPP		JOIN(BACK_CAM,_THUMBNAIL_BPP)
#define BACK_CAMERA_FOCAL_LENGTH		JOIN(BACK_CAM,_FOCAL_LENGTH)

#define DEFAULT_JPEG_THUMBNAIL_WIDTH		256
#define DEFAULT_JPEG_THUMBNAIL_HEIGHT		192

#define CAMERA_DEV_NAME		"/dev/video1"

#define PMEM_DEV_NAME		"/dev/pmem_gpu1"

#define BPP		2
#define MIN(x, y)		(((x) < (y)) ? (x) : (y))
#define MAX_BUFFERS		6

/*
 * V 4 L 2		F I M C		E X T E N S I O N S
 *
 */
#define V4L2_CID_PADDR_Y			(V4L2_CID_PRIVATE_BASE + 1)
#define V4L2_CID_PADDR_CB			(V4L2_CID_PRIVATE_BASE + 2)
#define V4L2_CID_PADDR_CR			(V4L2_CID_PRIVATE_BASE + 3)
#define V4L2_CID_PADDR_CBCR			(V4L2_CID_PRIVATE_BASE + 4)
#define V4L2_CID_STREAM_PAUSE			(V4L2_CID_PRIVATE_BASE + 53)

#define V4L2_CID_CAM_JPEG_MAIN_SIZE		(V4L2_CID_PRIVATE_BASE + 32)
#define V4L2_CID_CAM_JPEG_MAIN_OFFSET		(V4L2_CID_PRIVATE_BASE + 33)
#define V4L2_CID_CAM_JPEG_THUMB_SIZE		(V4L2_CID_PRIVATE_BASE + 34)
#define V4L2_CID_CAM_JPEG_THUMB_OFFSET		(V4L2_CID_PRIVATE_BASE + 35)
#define V4L2_CID_CAM_JPEG_POSTVIEW_OFFSET	(V4L2_CID_PRIVATE_BASE + 36)
#define V4L2_CID_CAM_JPEG_QUALITY		(V4L2_CID_PRIVATE_BASE + 37)

#define TPATTERN_COLORBAR		1
#define TPATTERN_HORIZONTAL		2
#define TPATTERN_VERTICAL		3

#define V4L2_PIX_FMT_YVYU		v4l2_fourcc('Y', 'V', 'Y', 'U')

/* FOURCC for FIMC specific */
#define V4L2_PIX_FMT_VYUY		v4l2_fourcc('V', 'Y', 'U', 'Y')
/*
 * U S E R		D E F I N E D		T Y P E S
 *
 */

struct fimc_buffer {
	void		*start;
	size_t		length;
};

struct yuv_fmt_list {
	const char	*name;
	const char	*desc;
	unsigned int	fmt;
	int		depth;
	int		planes;
};

//s1 [Apply factory standard]
struct camsensor_date_info {
	unsigned int year;
	unsigned int month;
	unsigned int date;
};

class V4L2Device;

class SecCamera {
public:
	static const enum v4l2_buf_type m_buf_type =
					V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	enum CAMERA_ID {
		CAMERA_ID_BACK = 0,
	};

	enum JPEG_QUALITY {
		JPEG_QUALITY_ECONOMY	= 0,
		JPEG_QUALITY_NORMAL	= 50,
		JPEG_QUALITY_SUPERFINE	= 100,
		JPEG_QUALITY_MAX,
	};

	/*VT call*/
	enum VT_MODE {
		VT_MODE_OFF,
		VT_MODE_ON,
		VT_MODE_MAX,
	};

	/*Camera sensor mode - Camcorder fix fps*/
	enum SENSOR_MODE {
		SENSOR_MODE_CAMERA,
		SENSOR_MODE_MOVIE,
	};

	/*Camera Shot mode*/
	enum SHOT_MODE {
		SHOT_MODE_SINGLE	= 0,
		SHOT_MODE_CONTINUOUS	= 1,
		SHOT_MODE_PANORAMA	= 2,
		SHOT_MODE_SMILE		= 3,
		SHOT_MODE_SELF		= 6,
	};

	int m_touch_af_start_stop;

	struct gps_info_latiude {
		unsigned int	north_south;
		unsigned int	dgree;
		unsigned int	minute;
		unsigned int	second;
	} gpsInfoLatitude;

	struct gps_info_longitude {
		unsigned int	east_west;
		unsigned int	dgree;
		unsigned int	minute;
		unsigned int	second;
	} gpsInfoLongitude;

	struct gps_info_altitude {
		unsigned int	plus_minus;
		unsigned int	dgree;
		unsigned int	minute;
		unsigned int	second;
	} gpsInfoAltitude;

	SecCamera();
	~SecCamera();

	static SecCamera* createInstance(void) {
		static SecCamera singleton;
		return &singleton;
	}

	status_t	dump(int fd, const Vector<String16>& args);

	int		isOpened(void) const;
	int		openCamera(int index);
	void		closeCamera(void);

	int		getCameraId(void) { return m_camera_id; }

	int		startPreview(void);
	int		stopPreview(void);
	int		getPreview(void);
	int		setPreviewSize(int width, int height, int pixel_format);
	int		getPreviewSize(int *width, int *height, int *frame_size);
	int		getPreviewMaxSize(int *width, int *height);
	int		getPreviewPixelFormat(void);
	sp<MemoryHeapBase> getBufferHeap(void);
	sp<MemoryBase>	getBuffer(int index);

	int		startRecord(void);
	int		stopRecord(void);
	int		getRecordFrame(void);
	int		releaseRecordFrame(int index);
	unsigned int	getRecPhyAddrY(int);
	unsigned int	getRecPhyAddrC(int);
	int		setRecordingSize(int width, int height);

	int		setSnapshotSize(int width, int height);
	int		getSnapshotSize(int *width, int *height, int *frame_size);
	int		getSnapshotMaxSize(int *width, int *height);
	int		setSnapshotPixelFormat(int pixel_format);
	int		getSnapshotPixelFormat(void);
	int		getSnapshotAndJpeg(unsigned char *yuv_buf,
						unsigned char *jpeg_buf,
						unsigned int *output_size);
	int		getExif(unsigned char *pExifDst, unsigned char *pThumbSrc);
	int		endSnapshot(void);

	int		setJpegThumbnailSize(int width, int height);
	int		getJpegThumbnailSize(int *width, int *height);

	int		setAutofocus(void);
	int		cancelAutofocus(void);
	int		getAutoFocusResult(void);

	int		setVerticalMirror(void);
	int		setHorizontalMirror(void);

	int		setWhiteBalance(int white_balance);
	int		getWhiteBalance(void);

	int		setBrightness(int brightness);
	int		getBrightness(void);

	int		setImageEffect(int image_effect);
	int		getImageEffect(void);

	int		setSceneMode(int scene_mode);
	int		getSceneMode(void);

	int		setMetering(int metering_value);
	int		getMetering(void);

	int		setISO(int iso_value);
	int		getISO(void);

	int		setContrast(int contrast_value);
	int		getContrast(void);

	int		setSaturation(int saturation_value);
	int		getSaturation(void);

	int		setSharpness(int sharpness_value);
	int		getSharpness(void);

	int		setWDR(int wdr_value);
	int		getWDR(void);

	int		setAntiShake(int anti_shake);
	int		getAntiShake(void);

	int		setJpegQuality(int jpeg_qality);
	int		getJpegQuality(void);

	int		setFocusMode(int focus_mode);
	int		getFocusMode(void);

	int		setGPSLatitude(const char *gps_latitude);
	int		setGPSLongitude(const char *gps_longitude);
	int		setGPSAltitude(const char *gps_altitude);
	int		setGPSTimeStamp(const char *gps_timestamp);
	int		setGPSProcessingMethod(const char *gps_timestamp);

	int		setGamma(int gamma);
	int		setSlowAE(int slow_ae);
	int		setExifOrientationInfo(int orientationInfo);

	int		setSensorMode(int sensor_mode); /* Camcorder fix fps */
	int		setShotMode(int shot_mode);		/* Shot mode */

	int		setVTmode(int vtmode);
	int		getVTmode(void);

	int		setBlur(int blur_level);
	int		getBlur(void);

	const __u8*	getCameraSensorName(void);

	int 		setFrameRate(int frame_rate);

	void		getPostViewConfig(int*, int*, int*);
	void		getThumbnailConfig(int *width, int *height, int *size);

private:
	v4l2_streamparm m_streamparm;
	struct sec_cam_parm *m_params;

	V4L2Device	*device;

	int		m_camera_id;

	int		m_flag_record_start;

	int		m_preview_v4lformat;
	int		m_preview_width;
	int		m_preview_height;
	int		m_preview_max_width;
	int		m_preview_max_height;

	int		m_snapshot_v4lformat;
	int		m_snapshot_width;
	int		m_snapshot_height;
	int		m_snapshot_max_width;
	int		m_snapshot_max_height;

	int		m_recording_width;
	int		m_recording_height;

	int		m_wdr;
	int		m_anti_shake;
	long		m_gps_latitude;
	long		m_gps_longitude;
	long		m_gps_altitude;
	long		m_gps_timestamp;
	int		m_vtmode;
	int		m_sensor_mode;
	int		m_shot_mode;
	int		m_exif_orientation;
	int		m_blur_level;
	int		m_video_gamma;
	int		m_slow_ae;
	int		m_camera_af_flag;

	int		m_flag_camera_start;

	int		m_jpeg_fd;
	int		m_jpeg_thumbnail_width;
	int		m_jpeg_thumbnail_height;
	int		m_jpeg_quality;

	exif_attribute_t mExifInfo;

	struct fimc_buffer m_capture_buf;

	void		setExifChangedAttribute();
	void		setExifFixedAttribute();
	int		previewPoll(void);
};

extern unsigned long measure_time(struct timeval *start, struct timeval *stop);

}; // namespace android

#endif // ANDROID_HARDWARE_CAMERA_SEC_H
