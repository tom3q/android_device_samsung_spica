/*
 * Copyright 2008, The Android Open Source Project
 * Copyright 2010, Samsung Electronics Co. LTD
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

/*
************************************
* Filename: SecCamera.cpp
* Author:   Sachin P. Kamat
* Purpose:  This file interacts with the Camera and JPEG drivers.
*************************************
*/

#define LOG_NDEBUG 0
#define LOG_TAG "SecCamera"

#include <utils/Log.h>

#include <string.h>
#include <stdlib.h>
#include <sys/poll.h>
#include "SecCamera.h"
#include "cutils/properties.h"

using namespace android;

#define ERR(ret)							\
        LOGE("%s::%d fail. ret: %d, errno: %s, m_camera_id = %d\n",	\
             __func__, __LINE__, ret, strerror(errno), m_camera_id);

#define ALIGN_TO_PAGE(x)        (((x) + 4095) & ~4095)

namespace android {

/*
 * Utility functions
 */

static int get_pixel_depth(unsigned int fmt)
{
	int depth = 0;

	switch (fmt) {
	case V4L2_PIX_FMT_YUV420:
		depth = 12;
		break;

	case V4L2_PIX_FMT_RGB565X:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUV422P:
		depth = 16;
		break;

	case V4L2_PIX_FMT_RGB32:
		depth = 32;
		break;
	}

	return depth;
}

static inline size_t get_buffer_size(int width, int height, unsigned int fmt)
{
	return (width * height * get_pixel_depth(fmt)) / 8;
}

static inline size_t get_buffer_size_aligned(int width, int height,
							unsigned int fmt)
{
	return ALIGN_TO_PAGE(get_buffer_size(width, height, fmt));
}

/*
 * Generic V4L2 device implementation
 */

class V4L2Device {
	int fd;
	struct fimc_buffer buffers[MAX_BUFFERS];
	sp<MemoryHeapBase> heap;
	sp<MemoryHeapPmem> pmemHeap;

	int allocateBuffers(int nr, size_t size);

public:
	V4L2Device(const char *device);
	~V4L2Device(void);

	bool initCheck(void)
	{
		return fd != -1;
	}

	int pollDevice(short mask, int timeout);
	int queryCap(int mask);
	const __u8* enumInput(int index);
	int setInput(int index);
	int setFormat(enum v4l2_buf_type type,
				int width, int height, unsigned int fmt);
	int enumFormat(enum v4l2_buf_type type, unsigned int fmt);
	int reqBufs(enum v4l2_buf_type type, int nr_bufs, size_t buf_size);
	int queryBuf(enum v4l2_buf_type type, int index,
						void **addr, size_t *length);
	int setStream(enum v4l2_buf_type type, bool on);
	int queueBuf(enum v4l2_buf_type type, int index);
	int dequeueBuf(enum v4l2_buf_type type);
	int getCtrl(unsigned int id);
	int setCtrl(unsigned int id, unsigned int value);
	int getParam(enum v4l2_buf_type type,
					struct v4l2_streamparm *streamparm);
	int setParam(enum v4l2_buf_type type,
					struct v4l2_streamparm *streamparm);
	sp<MemoryHeapBase> getHeap(void);
	sp<MemoryBase> getMemory(int index);
};

V4L2Device::V4L2Device(const char *device) :
	fd(-1)
{
	fd = open(device, O_RDWR);
}

V4L2Device::~V4L2Device(void)
{
	allocateBuffers(0, 0);
	if (fd >= 0)
		close(fd);
}

int V4L2Device::pollDevice(short mask, int timeout)
{
	struct pollfd events;
	int ret;

	memset(&events, 0, sizeof(events));
	events.fd = fd;
	events.events = mask;

	ret = poll(&events, 1, timeout);
	if (ret < 0) {
		LOGE("ERR(%s):poll error\n", __func__);
		return ret;
	}

	if (ret == 0) {
		LOGE("ERR(%s):No data in %d secs..\n", __func__, timeout / 1000);
		return ret;
	}

	return ret;
}

int V4L2Device::queryCap(int mask)
{
	struct v4l2_capability cap;
	int ret = 0;

	ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);

	if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_QUERYCAP failed\n", __func__);
		return -1;
	}

	if (!(cap.capabilities & mask))
		return -1;

	return ret;
}

const __u8* V4L2Device::enumInput(int index)
{
	static struct v4l2_input input;

	input.index = index;
	if (ioctl(fd, VIDIOC_ENUMINPUT, &input) != 0) {
		LOGE("ERR(%s):No matching index found\n", __func__);
		return NULL;
	}
	LOGI("Name of input channel[%d] is %s\n", input.index, input.name);

	return input.name;
}

int V4L2Device::setInput(int index)
{
	struct v4l2_input input;
	int ret;

	input.index = index;

	ret = ioctl(fd, VIDIOC_S_INPUT, &input);
	if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_S_INPUT failed\n", __func__);
		return ret;
	}

	return ret;
}

int V4L2Device::setFormat(enum v4l2_buf_type type,
					int width, int height, unsigned int fmt)
{
	struct v4l2_format v4l2_fmt;
	struct v4l2_pix_format pixfmt;
	int ret;

	v4l2_fmt.type = type;

	memset(&pixfmt, 0, sizeof(pixfmt));

	pixfmt.width = width;
	pixfmt.height = height;
	pixfmt.pixelformat = fmt;
	if (fmt == V4L2_PIX_FMT_JPEG)
		pixfmt.colorspace = V4L2_COLORSPACE_JPEG;

	pixfmt.sizeimage = get_buffer_size(width, height, fmt);

	pixfmt.field = V4L2_FIELD_NONE;

	v4l2_fmt.fmt.pix = pixfmt;

	/* Set up for capture */
	ret = ioctl(fd, VIDIOC_S_FMT, &v4l2_fmt);
	if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_S_FMT failed\n", __func__);
		return -1;
	}

	return 0;
}

int V4L2Device::enumFormat(enum v4l2_buf_type type, unsigned int fmt)
{
	struct v4l2_fmtdesc fmtdesc;
	int found = 0;

	fmtdesc.type = type;
	fmtdesc.index = 0;

	while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
		if (fmtdesc.pixelformat == fmt) {
			LOGV("passed fmt = %#x found pixel format[%d]: %s\n",
				fmt, fmtdesc.index, fmtdesc.description);
			found = 1;
			break;
		}

		fmtdesc.index++;
	}

	if (!found) {
		LOGE("unsupported pixel format\n");
		return -1;
	}

	return 0;
}

int V4L2Device::allocateBuffers(int nr_bufs, size_t buf_size)
{
	pmemHeap.clear();
	if (heap != NULL) {
		heap->dispose();
		heap.clear();
	}

	if (nr_bufs <= 0)
		return 0;

	buf_size = ALIGN_TO_PAGE(buf_size);
	size_t heap_size = buf_size*nr_bufs;

	heap = new MemoryHeapBase(PMEM_DEV_NAME, heap_size, 0);

	void *vaddr = heap->getBase();
	if (vaddr == MAP_FAILED)
		return -1;

	pmemHeap = new MemoryHeapPmem(heap, 0);

	int i = 0;
	do {
		memset(vaddr, i << 5, buf_size);
		buffers[i].start = vaddr;
		buffers[i].length = buf_size;
		vaddr = (uint8_t *)vaddr + buf_size;
		++i;
	} while (--nr_bufs);

	return 0;
}

int V4L2Device::reqBufs(enum v4l2_buf_type type, int nr_bufs, size_t buf_size)
{
	struct v4l2_requestbuffers req;
	int ret;

	req.count = nr_bufs;
	req.type = type;
	req.memory = V4L2_MEMORY_USERPTR;

	ret = ioctl(fd, VIDIOC_REQBUFS, &req);
	if (ret < 0) {
		LOGE("ERR(%s): VIDIOC_REQBUFS failed\n", __func__);
		return -1;
	}

	ret = allocateBuffers(nr_bufs, buf_size);
	if (ret < 0) {
		LOGE("ERR(%s): allocateBuffers failed\n", __func__);
		return -1;
	}

	return req.count;
}

int V4L2Device::queryBuf(enum v4l2_buf_type type, int index,
						void **addr, size_t *length)
{
	struct v4l2_buffer v4l2_buf;
	int ret;

	LOGI("%s :", __func__);

	if (index >= MAX_BUFFERS) {
		LOGE("%s: invalid buffer index %d", __func__, index);
		return -1;
	}

	*addr = buffers[index].start;
	*length = buffers[index].length;

	LOGI("%s: buffer->start = %p buffer->length = %d",
	     __func__, buffers[index].start, buffers[index].length);

	return 0;
}

int V4L2Device::setStream(enum v4l2_buf_type type, bool on)
{
	int request;
	int ret;

	request = (on) ? VIDIOC_STREAMON : VIDIOC_STREAMOFF;

	ret = ioctl(fd, request, &type);
	if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_STREAM%s failed\n",
						__func__, (on) ? "ON" : "OFF");
		return ret;
	}

	return ret;
}

int V4L2Device::queueBuf(enum v4l2_buf_type type, int index)
{
	struct v4l2_buffer v4l2_buf;
	struct v4l2_plane plane;
	int ret;

	memset(&plane, 0, sizeof(plane));

	plane.m.userptr = (unsigned long)buffers[index].start;
	plane.length = buffers[index].length;

	memset(&v4l2_buf, 0, sizeof(v4l2_buf));

	v4l2_buf.type = type;
	v4l2_buf.memory = V4L2_MEMORY_USERPTR;
	v4l2_buf.index = index;
	v4l2_buf.m.planes = &plane;
	v4l2_buf.length = 1;

	ret = ioctl(fd, VIDIOC_QBUF, &v4l2_buf);
	if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_QBUF failed\n", __func__);
		return ret;
	}

	return 0;
}

int V4L2Device::dequeueBuf(enum v4l2_buf_type type)
{
	struct v4l2_buffer v4l2_buf;
	int ret;

	memset(&v4l2_buf, 0, sizeof(v4l2_buf));

	v4l2_buf.type = type;
	v4l2_buf.memory = V4L2_MEMORY_USERPTR;

	ret = ioctl(fd, VIDIOC_DQBUF, &v4l2_buf);
	if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_DQBUF failed, dropped frame\n", __func__);
		return ret;
	}

	return v4l2_buf.index;
}

int V4L2Device::getCtrl(unsigned int id)
{
	struct v4l2_control ctrl;
	int ret;

	ctrl.id = id;

	ret = ioctl(fd, VIDIOC_G_CTRL, &ctrl);
	if (ret < 0) {
		LOGE("ERR(%s): VIDIOC_G_CTRL(id = 0x%x (%d)) failed, ret = %d\n",
		     __func__, id, id-V4L2_CID_PRIVATE_BASE, ret);
		return ret;
	}

	return ctrl.value;
}

int V4L2Device::setCtrl(unsigned int id, unsigned int value)
{
	struct v4l2_control ctrl;
	int ret;

	ctrl.id = id;
	ctrl.value = value;

	ret = ioctl(fd, VIDIOC_S_CTRL, &ctrl);
	if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_S_CTRL(id = %#x (%d), value = %d) failed ret = %d\n",
		     __func__, id, id-V4L2_CID_PRIVATE_BASE, value, ret);

		return ret;
	}

	return ctrl.value;
}

int V4L2Device::getParam(enum v4l2_buf_type type,
					struct v4l2_streamparm *streamparm)
{
	int ret;

	streamparm->type = type;

	ret = ioctl(fd, VIDIOC_G_PARM, streamparm);
	if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_G_PARM failed\n", __func__);
		return -1;
	}

	LOGV("%s : timeperframe: numerator %d, denominator %d\n", __func__,
	     streamparm->parm.capture.timeperframe.numerator,
	     streamparm->parm.capture.timeperframe.denominator);

	return 0;
}

int V4L2Device::setParam(enum v4l2_buf_type type,
					struct v4l2_streamparm *streamparm)
{
	int ret;

	streamparm->type = type;

	ret = ioctl(fd, VIDIOC_S_PARM, streamparm);
	if (ret < 0) {
		LOGE("ERR(%s):VIDIOC_S_PARM failed\n", __func__);
		return ret;
	}

	return 0;
}

sp<MemoryHeapBase> V4L2Device::getHeap(void)
{
	return pmemHeap;
}

sp<MemoryBase> V4L2Device::getMemory(int index)
{
	intptr_t addr = (intptr_t)buffers[index].start;
	intptr_t base = (intptr_t)heap->getBase();
	return new MemoryBase(pmemHeap, addr - base, buffers[index].length);
}

/******************************************************************************/

/*
 * SecCamera
 */

/* Constructor/destructor */

SecCamera::SecCamera() :
	m_camera_id(CAMERA_ID_BACK),
	m_preview_v4lformat(V4L2_PIX_FMT_RGB565X),
	m_preview_width      (0),
	m_preview_height     (0),
	m_preview_max_width  (MAX_BACK_CAMERA_PREVIEW_WIDTH),
	m_preview_max_height (MAX_BACK_CAMERA_PREVIEW_HEIGHT),
	m_snapshot_v4lformat(-1),
	m_snapshot_width      (0),
	m_snapshot_height     (0),
	m_snapshot_max_width  (MAX_BACK_CAMERA_SNAPSHOT_WIDTH),
	m_snapshot_max_height (MAX_BACK_CAMERA_SNAPSHOT_HEIGHT),
	m_wdr(-1),
	m_anti_shake(-1),
	m_gps_latitude(-1),
	m_gps_longitude(-1),
	m_gps_altitude(-1),
	m_gps_timestamp(-1),
	m_vtmode(0),
	m_sensor_mode(-1),
	m_shot_mode(-1),
	m_exif_orientation(-1),
	m_blur_level(-1),
	m_video_gamma(-1),
	m_slow_ae(-1),
	m_camera_af_flag(-1),
	m_flag_camera_start(0),
	m_jpeg_thumbnail_width (0),
	m_jpeg_thumbnail_height(0),
	m_jpeg_quality(100)
{
	m_params = (struct sec_cam_parm*)&m_streamparm.parm.raw_data;
	struct v4l2_captureparm capture;
	m_params->capture.timeperframe.numerator = 1;
	m_params->capture.timeperframe.denominator = 0;
	m_params->contrast = -1;
	m_params->effects = -1;
	m_params->brightness = -1;
	m_params->flash_mode = -1;
	m_params->focus_mode = -1;
	m_params->iso = -1;
	m_params->metering = -1;
	m_params->saturation = -1;
	m_params->scene_mode = -1;
	m_params->sharpness = -1;
	m_params->white_balance = -1;

	memset(&m_capture_buf, 0, sizeof(m_capture_buf));

	LOGV("%s :", __func__);
}

SecCamera::~SecCamera()
{
	LOGV("%s :", __func__);
}

/* Open/close */

int SecCamera::isOpened(void) const
{
	LOGV("%s : : %d", __func__, device != 0);
	return device != 0;
}

int SecCamera::openCamera(int index)
{
	LOGV("%s :", __func__);
	int ret = 0;

	if (index != 0)
		return -1;

	if (device)
		return 0;

	m_camera_af_flag = -1;

	device = new V4L2Device(CAMERA_DEV_NAME);
	if (!device || !device->initCheck()) {
		delete device;
		device = 0;
		LOGE("ERR(%s): Cannot open %s (error : %s)\n", __func__, CAMERA_DEV_NAME, strerror(errno));
		return -1;
	}

	LOGD("%s: V4L2 device opened.", __func__);

	ret = device->queryCap(V4L2_CAP_VIDEO_CAPTURE);
	if (ret < 0) {
		ERR(ret);
		goto error;
	}

	if (device->enumInput(index) == NULL) {
		ERR(ret);
		goto error;
	}

	ret = device->setInput(index);
	if (ret < 0) {
		ERR(ret);
		goto error;
	}

	m_camera_id = index;

	m_preview_max_width   = MAX_BACK_CAMERA_PREVIEW_WIDTH;
	m_preview_max_height  = MAX_BACK_CAMERA_PREVIEW_HEIGHT;
	m_snapshot_max_width  = MAX_BACK_CAMERA_SNAPSHOT_WIDTH;
	m_snapshot_max_height = MAX_BACK_CAMERA_SNAPSHOT_HEIGHT;

	setExifFixedAttribute();

	return 0;

error:
	delete device;
	device = 0;

	return -1;
}

void SecCamera::closeCamera(void)
{
	LOGV("%s :", __func__);

	if (!device)
		return;

	stopRecord();

	/* close m_cam_fd after stopRecord() because stopRecord()
		* uses m_cam_fd to change frame rate
		*/
	delete device;
	device = 0;
}

/* Preview */

sp<MemoryHeapBase> SecCamera::getBufferHeap(void)
{
	if (!device)
		return NULL;

	return device->getHeap();
}

sp<MemoryBase> SecCamera::getBuffer(int index)
{
	if (!device)
		return NULL;

	return device->getMemory(index);
}

int SecCamera::previewPoll(void)
{
	int ret;

	ret = device->pollDevice(POLLIN | POLLERR, 1000);
	if (ret < 0) {
		LOGE("ERR(%s):poll error\n", __func__);
		return ret;
	}

	if (ret == 0) {
		LOGE("ERR(%s):No data in 1 secs..\n", __func__);
		return ret;
	}

	return ret;
}

int SecCamera::startPreview(void)
{
	int ret;

	LOGV("%s :", __func__);

	// aleady started
	if (m_flag_camera_start > 0) {
		LOGE("ERR(%s):Preview was already started\n", __func__);
		return 0;
	}

	if (!device) {
		LOGE("ERR(%s):Camera was closed\n", __func__);
		return -1;
	}

	ret = device->enumFormat(m_buf_type, m_preview_v4lformat);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	ret = device->setFormat(m_buf_type,
			m_preview_width, m_preview_height, m_preview_v4lformat);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	size_t buf_size = get_buffer_size(m_preview_width,
					m_preview_height, m_preview_v4lformat);
	ret = device->reqBufs(m_buf_type, MAX_BUFFERS, buf_size);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	LOGV("%s : m_preview_width: %d m_preview_height: %d\n",
				__func__, m_preview_width, m_preview_height);

	/* start with all buffers in queue */
	for (int i = 0; i < MAX_BUFFERS; i++) {
		ret = device->queueBuf(m_buf_type, i);
		if (ret < 0) {
			ERR(ret);
			return -1;
		}
	}

	ret = device->setStream(m_buf_type, true);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	/* TODO: Set all sensor parameters here */

	// It is a delay for a new frame, not to show the previous bigger ugly picture frame.
	ret = device->pollDevice(POLLIN | POLLERR, 10000);
	if (ret < 0) {
		device->setStream(m_buf_type, false);
		ERR(ret);
		return -1;
	}

	/* TODO: Is V4L2_CID_CAMERA_RETURN_FOCUS needed here? */

	LOGV("%s: got the first frame of the preview\n", __func__);

	m_flag_camera_start = 1;

	return 0;
}

int SecCamera::stopPreview(void)
{
	int ret;

	LOGV("%s :", __func__);

	if (m_flag_camera_start == 0) {
		LOGW("%s: doing nothing because m_flag_camera_start is zero", __func__);
		return 0;
	}

	if (!device) {
		LOGE("ERR(%s):Camera was closed\n", __func__);
		return -1;
	}

	ret = device->setStream(m_buf_type, false);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	m_flag_camera_start = 0;

	return ret;
}

int SecCamera::getPreview()
{
	int index;
	int ret;

	if (m_flag_camera_start == 0 || previewPoll() == 0) {
		LOGE("ERR(%s):Start Camera Device Reset \n", __func__);

		stopPreview();

		ret = device->queryCap(V4L2_CAP_VIDEO_CAPTURE);
		if (ret < 0) {
			ERR(ret);
			return -1;
		}

		if (!device->enumInput(m_camera_id)) {
			ERR(0);
			return -1;
		}

		ret = startPreview();
		if (ret < 0) {
			LOGE("ERR(%s): startPreview() return %d\n", __func__, ret);
			return 0;
		}
	}

	index = device->dequeueBuf(m_buf_type);
	if (index < 0 || index >= MAX_BUFFERS) {
		LOGE("ERR(%s):wrong index = %d\n", __func__, index);
		return -1;
	}

	ret = device->queueBuf(m_buf_type, index);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	return index;
}

int SecCamera::setPreviewSize(int width, int height, int pixel_format)
{
	LOGV("%s(width(%d), height(%d), format(%d))", __func__, width, height, pixel_format);

	int v4lpixelformat = pixel_format;

#if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
	if (v4lpixelformat == V4L2_PIX_FMT_YUV420)
		LOGV("PreviewFormat:V4L2_PIX_FMT_YUV420");
	else if (v4lpixelformat == V4L2_PIX_FMT_YUV422P)
		LOGV("PreviewFormat:V4L2_PIX_FMT_YUV422P");
	else if (v4lpixelformat == V4L2_PIX_FMT_YUYV)
		LOGV("PreviewFormat:V4L2_PIX_FMT_YUYV");
	else if (v4lpixelformat == V4L2_PIX_FMT_RGB565X)
		LOGV("PreviewFormat:V4L2_PIX_FMT_RGB565X");
	else
		LOGV("PreviewFormat:UnknownFormat");
#endif
	m_preview_width  = width;
	m_preview_height = height;
	m_preview_v4lformat = v4lpixelformat;

	return 0;
}

int SecCamera::getPreviewSize(int *width, int *height, int *frame_size)
{
	*width  = m_preview_width;
	*height = m_preview_height;
	*frame_size = get_buffer_size(m_preview_width,
					m_preview_height, m_preview_v4lformat);

	return 0;
}

int SecCamera::getPreviewMaxSize(int *width, int *height)
{
	*width  = m_preview_max_width;
	*height = m_preview_max_height;

	return 0;
}

int SecCamera::getPreviewPixelFormat(void)
{
	return m_preview_v4lformat;
}

/* Recording */

int SecCamera::startRecord(void)
{
	int ret, i;

	LOGV("%s :", __func__);

	if (m_flag_record_start > 0) {
		LOGE("ERR(%s): Recording was already started\n", __func__);
		return 0;
	}

	if (!device) {
		LOGE("ERR(%s): Camera was closed\n", __func__);
		return -1;
	}

	ret = device->enumFormat(m_buf_type, V4L2_PIX_FMT_YUV420);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	LOGI("%s: m_recording_width = %d, m_recording_height = %d\n",
			__func__, m_recording_width, m_recording_height);

	ret = device->setFormat(m_buf_type, m_recording_width,
				m_recording_height, V4L2_PIX_FMT_YUV420);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	ret = device->setCtrl(V4L2_CID_CAMERA_FRAME_RATE,
			       m_params->capture.timeperframe.denominator);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	size_t buf_size = get_buffer_size(m_recording_width,
				m_recording_height, V4L2_PIX_FMT_YUV420);
	ret = device->reqBufs(m_buf_type, MAX_BUFFERS, buf_size);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	/* start with all buffers in queue */
	for (i = 0; i < MAX_BUFFERS; i++) {
		ret = device->queueBuf(m_buf_type, i);
		if (ret < 0) {
			ERR(ret);
			return -1;
		}
	}

	ret = device->setStream(m_buf_type, true);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	/* Get and throw away the first frame since it is often garbled. */
	ret = device->pollDevice(POLLIN | POLLERR, 10000);
	if (ret < 0) {
		device->setStream(m_buf_type, false);
		ERR(ret);
		return -1;
	}

	m_flag_record_start = 1;

	return 0;
}

int SecCamera::stopRecord(void)
{
	int ret;

	LOGV("%s :", __func__);

	if (m_flag_record_start == 0) {
		LOGW("%s: doing nothing because m_flag_record_start is zero", __func__);
		return 0;
	}

	if (!device) {
		LOGE("ERR(%s): Camera was closed\n", __func__);
		return -1;
	}

	m_flag_record_start = 0;

	ret = device->setStream(m_buf_type, false);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	ret = device->setCtrl(V4L2_CID_CAMERA_FRAME_RATE, FRAME_RATE_AUTO);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	return 0;
}

int SecCamera::getRecordFrame()
{
	if (m_flag_record_start == 0) {
		LOGE("%s: m_flag_record_start is 0", __func__);
		return -1;
	}

	previewPoll();
	return device->dequeueBuf(m_buf_type);
}

int SecCamera::releaseRecordFrame(int index)
{
	if (!m_flag_record_start) {
		/* this can happen when recording frames are returned after
		 * the recording is stopped at the driver level.  we don't
		 * need to return the buffers in this case and we've seen
		 * cases where fimc could crash if we called qbuf and it
		 * wasn't expecting it.
		 */
		LOGI("%s: recording not in progress, ignoring", __func__);
		return 0;
	}

	return device->queueBuf(m_buf_type, index);
}

int SecCamera::getExif(unsigned char *pExifDst, unsigned char *pThumbSrc)
{
	JpegEncoder jpgEnc;

	LOGV("%s : m_jpeg_thumbnail_width = %d, height = %d",
	     __func__, m_jpeg_thumbnail_width, m_jpeg_thumbnail_height);
	if ((m_jpeg_thumbnail_width > 0) && (m_jpeg_thumbnail_height > 0)) {
		int inFormat = JPG_MODESEL_YCBCR;
		int outFormat = JPG_422;
		switch (m_snapshot_v4lformat) {
		case V4L2_PIX_FMT_YUV420:
			outFormat = JPG_420;
			break;
		case V4L2_PIX_FMT_YUYV:
		case V4L2_PIX_FMT_UYVY:
		case V4L2_PIX_FMT_YUV422P:
			outFormat = JPG_422;
			break;
		}

		//if (jpgEnc.setConfig(JPEG_SET_ENCODE_IN_FORMAT, inFormat) != JPG_SUCCESS)
		//	return -1;

		if (jpgEnc.setConfig(JPEG_SET_SAMPING_MODE, outFormat) != JPG_SUCCESS)
			return -1;

		if (jpgEnc.setConfig(JPEG_SET_ENCODE_QUALITY, JPG_QUALITY_LEVEL_2) != JPG_SUCCESS)
			return -1;

		int thumbWidth, thumbHeight, thumbSrcSize;
		getThumbnailConfig(&thumbWidth, &thumbHeight, &thumbSrcSize);
		if (jpgEnc.setConfig(JPEG_SET_ENCODE_WIDTH, thumbWidth) != JPG_SUCCESS)
			return -1;

		if (jpgEnc.setConfig(JPEG_SET_ENCODE_HEIGHT, thumbHeight) != JPG_SUCCESS)
			return -1;

		char *pInBuf = (char *)jpgEnc.getInBuf(thumbSrcSize);
		if (pInBuf == NULL)
			return -1;
		memcpy(pInBuf, pThumbSrc, thumbSrcSize);
		jpgEnc.getOutBuf();

		unsigned int thumbSize;
		unsigned int outbuf_size;

		jpgEnc.encode(&thumbSize, NULL, &outbuf_size);

		LOGV("%s : enableThumb set to true", __func__);
		mExifInfo.enableThumb = true;
	} else {
		LOGV("%s : enableThumb set to false", __func__);
		mExifInfo.enableThumb = false;
	}

	unsigned int exifSize;

	setExifChangedAttribute();

	LOGV("%s: calling jpgEnc.makeExif, mExifInfo.width set to %d, height to %d\n",
	     __func__, mExifInfo.width, mExifInfo.height);

	jpgEnc.makeExif(pExifDst, &mExifInfo, &exifSize, true);

	return exifSize;
}

void SecCamera::getPostViewConfig(int *width, int *height, int *size)
{
	if (m_preview_width == 1024) {
		*width = BACK_CAMERA_POSTVIEW_WIDE_WIDTH;
		*height = BACK_CAMERA_POSTVIEW_HEIGHT;
		*size = BACK_CAMERA_POSTVIEW_WIDE_WIDTH * BACK_CAMERA_POSTVIEW_HEIGHT * BACK_CAMERA_POSTVIEW_BPP / 8;
	} else {
		*width = BACK_CAMERA_POSTVIEW_WIDTH;
		*height = BACK_CAMERA_POSTVIEW_HEIGHT;
		*size = BACK_CAMERA_POSTVIEW_WIDTH * BACK_CAMERA_POSTVIEW_HEIGHT * BACK_CAMERA_POSTVIEW_BPP / 8;
	}
	LOGV("[5B] m_preview_width : %d, mPostViewWidth = %d mPostViewHeight = %d mPostViewSize = %d",
	     m_preview_width, *width, *height, *size);
}

void SecCamera::getThumbnailConfig(int *width, int *height, int *size)
{
	*width  = BACK_CAMERA_THUMBNAIL_WIDTH;
	*height = BACK_CAMERA_THUMBNAIL_HEIGHT;
	*size   = BACK_CAMERA_THUMBNAIL_WIDTH * BACK_CAMERA_THUMBNAIL_HEIGHT
		  * BACK_CAMERA_THUMBNAIL_BPP / 8;
}

/* Snapshot */

int SecCamera::getSnapshotAndJpeg(unsigned char *yuv_buf,
			unsigned char *jpeg_buf, unsigned int *output_size)
{
	int index;
	unsigned char *addr;
	int ret = 0;

	LOGV("%s :", __func__);

	if (!device) {
		LOGE("ERR(%s):Camera was closed\n", __func__);
		return -1;
	}

	if (m_flag_camera_start > 0) {
		LOGW("WARN(%s):Camera was in preview, should have been stopped\n", __func__);
		stopPreview();
	}

#if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
	if (m_snapshot_v4lformat == V4L2_PIX_FMT_YUV420)
		LOGV("SnapshotFormat:V4L2_PIX_FMT_YUV420");
	else if (m_snapshot_v4lformat == V4L2_PIX_FMT_YUV422P)
		LOGV("SnapshotFormat:V4L2_PIX_FMT_YUV422P");
	else if (m_snapshot_v4lformat == V4L2_PIX_FMT_YUYV)
		LOGV("SnapshotFormat:V4L2_PIX_FMT_YUYV");
	else if (m_snapshot_v4lformat == V4L2_PIX_FMT_UYVY)
		LOGV("SnapshotFormat:V4L2_PIX_FMT_UYVY");
	else if (m_snapshot_v4lformat == V4L2_PIX_FMT_RGB565X)
		LOGV("SnapshotFormat:V4L2_PIX_FMT_RGB565X");
	else
		LOGV("SnapshotFormat:UnknownFormat");
#endif

	ret = device->enumFormat(m_buf_type, m_snapshot_v4lformat);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	ret = device->setFormat(m_buf_type, m_snapshot_width,
				m_snapshot_height, m_snapshot_v4lformat);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	size_t buf_size = get_buffer_size(m_snapshot_width,
				m_snapshot_height, m_snapshot_v4lformat);
	ret = device->reqBufs(m_buf_type, 1, buf_size);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	ret = device->queryBuf(m_buf_type, 0,
				&m_capture_buf.start, &m_capture_buf.length);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	ret = device->queueBuf(m_buf_type, 0);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	ret = device->setStream(m_buf_type, true);
	if (ret < 0) {
		ERR(ret);
		return -1;
	}

	ret = device->pollDevice(POLLIN | POLLERR, 10000);
	if (ret <= 0) {
		device->setStream(m_buf_type, false);
		ERR(ret);
		return -1;
	}

	index = device->dequeueBuf(m_buf_type);

	device->setCtrl(V4L2_CID_STREAM_PAUSE, 0);

	LOGV("\nsnapshot dequeued buffer = %d snapshot_width = %d snapshot_height = %d",
				index, m_snapshot_width, m_snapshot_height);

	LOGI("%s: calling memcpy from m_capture_buf", __func__);
	memcpy(yuv_buf, (unsigned char*)m_capture_buf.start, buf_size);

	device->setStream(m_buf_type, false);

	/* JPEG encoding */
	JpegEncoder jpgEnc;
	int inFormat = JPG_MODESEL_YCBCR;
	int outFormat = JPG_422;

	switch (m_snapshot_v4lformat) {
	case V4L2_PIX_FMT_YUV420:
		outFormat = JPG_420;
		break;
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YUV422P:
	default:
		outFormat = JPG_422;
		break;
	}

	//if (jpgEnc.setConfig(JPEG_SET_ENCODE_IN_FORMAT, inFormat) != JPG_SUCCESS)
	//	LOGE("[JPEG_SET_ENCODE_IN_FORMAT] Error\n");

	if (jpgEnc.setConfig(JPEG_SET_SAMPING_MODE, outFormat) != JPG_SUCCESS)
		LOGE("[JPEG_SET_SAMPING_MODE] Error\n");

	image_quality_type_t jpegQuality;
	if (m_jpeg_quality >= 90)
		jpegQuality = JPG_QUALITY_LEVEL_1;
	else if (m_jpeg_quality >= 80)
		jpegQuality = JPG_QUALITY_LEVEL_2;
	else if (m_jpeg_quality >= 70)
		jpegQuality = JPG_QUALITY_LEVEL_3;
	else
		jpegQuality = JPG_QUALITY_LEVEL_4;

	if (jpgEnc.setConfig(JPEG_SET_ENCODE_QUALITY, jpegQuality) != JPG_SUCCESS)
		LOGE("[JPEG_SET_ENCODE_QUALITY] Error\n");
	if (jpgEnc.setConfig(JPEG_SET_ENCODE_WIDTH, m_snapshot_width) != JPG_SUCCESS)
		LOGE("[JPEG_SET_ENCODE_WIDTH] Error\n");

	if (jpgEnc.setConfig(JPEG_SET_ENCODE_HEIGHT, m_snapshot_height) != JPG_SUCCESS)
		LOGE("[JPEG_SET_ENCODE_HEIGHT] Error\n");

	unsigned int snapshot_size = m_snapshot_width * m_snapshot_height * 2;
	unsigned char *pInBuf = (unsigned char *)jpgEnc.getInBuf(snapshot_size);

	if (pInBuf == NULL) {
		LOGE("JPEG input buffer is NULL!!\n");
		return -1;
	}
	memcpy(pInBuf, yuv_buf, snapshot_size);

	unsigned int outbuf_size;
	unsigned char *pOutBuf = (unsigned char *)jpgEnc.getOutBuf();

	setExifChangedAttribute();
	jpgEnc.encode(output_size, NULL, &outbuf_size);

	if (pOutBuf == NULL) {
		LOGE("JPEG output buffer is NULL!!\n");
		return -1;
	}

	memcpy(jpeg_buf, pOutBuf, outbuf_size);

	return 0;
}

int SecCamera::setSnapshotSize(int width, int height)
{
	LOGV("%s(width(%d), height(%d))", __func__, width, height);

	m_snapshot_width  = width;
	m_snapshot_height = height;

	return 0;
}

int SecCamera::getSnapshotSize(int *width, int *height, int *frame_size)
{
	*width  = m_snapshot_width;
	*height = m_snapshot_height;

	*frame_size = get_buffer_size(m_snapshot_width,
				m_snapshot_height, m_snapshot_v4lformat);

	if (*frame_size == 0)
		return -1;

	return 0;
}

int SecCamera::getSnapshotMaxSize(int *width, int *height)
{
	m_snapshot_max_width  = MAX_BACK_CAMERA_SNAPSHOT_WIDTH;
	m_snapshot_max_height = MAX_BACK_CAMERA_SNAPSHOT_HEIGHT;

	*width  = m_snapshot_max_width;
	*height = m_snapshot_max_height;

	return 0;
}

int SecCamera::setSnapshotPixelFormat(int pixel_format)
{
	int v4lpixelformat= pixel_format;

	if (m_snapshot_v4lformat != v4lpixelformat) {
		m_snapshot_v4lformat = v4lpixelformat;
	}

#if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
	if (m_snapshot_v4lformat == V4L2_PIX_FMT_YUV420)
		LOGE("%s : SnapshotFormat:V4L2_PIX_FMT_YUV420", __func__);
	else if (m_snapshot_v4lformat == V4L2_PIX_FMT_YUV422P)
		LOGD("%s : SnapshotFormat:V4L2_PIX_FMT_YUV422P", __func__);
	else if (m_snapshot_v4lformat == V4L2_PIX_FMT_YUYV)
		LOGD("%s : SnapshotFormat:V4L2_PIX_FMT_YUYV", __func__);
	else if (m_snapshot_v4lformat == V4L2_PIX_FMT_UYVY)
		LOGD("%s : SnapshotFormat:V4L2_PIX_FMT_UYVY", __func__);
	else if (m_snapshot_v4lformat == V4L2_PIX_FMT_RGB565X)
		LOGD("%s : SnapshotFormat:V4L2_PIX_FMT_RGB565X", __func__);
	else
		LOGD("SnapshotFormat:UnknownFormat");
#endif
	return 0;
}

int SecCamera::getSnapshotPixelFormat(void)
{
	return m_snapshot_v4lformat;
}

int SecCamera::endSnapshot(void)
{
	int ret;

	LOGI("%s :", __func__);

	if (m_capture_buf.start) {
		munmap(m_capture_buf.start, m_capture_buf.length);

		LOGI("munmap():virt. addr %p size = %d\n",
				m_capture_buf.start, m_capture_buf.length);

		m_capture_buf.start = NULL;
		m_capture_buf.length = 0;
	}

	return 0;
}

/*
 * Controls
 */

/* Auto focus */

int SecCamera::setAutofocus(void)
{
	LOGV("%s :", __func__);

	if (!device) {
		LOGE("ERR(%s):Camera was closed\n", __func__);
		return -1;
	}

	if (device->setCtrl(V4L2_CID_CAMERA_SET_AUTO_FOCUS, AUTO_FOCUS_ON) < 0) {
		LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SET_AUTO_FOCUS", __func__);
		return -1;
	}

	return 0;
}

int SecCamera::getAutoFocusResult(void)
{
	int af_result;

	af_result = device->getCtrl(V4L2_CID_CAMERA_AUTO_FOCUS_RESULT);

	LOGV("%s : returning %d", __func__, af_result);

	return af_result;
}

int SecCamera::cancelAutofocus(void)
{
	LOGV("%s :", __func__);

	if (!device) {
		LOGE("ERR(%s):Camera was closed\n", __func__);
		return -1;
	}

	if (device->setCtrl(V4L2_CID_CAMERA_SET_AUTO_FOCUS, AUTO_FOCUS_OFF) < 0) {
		LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SET_AUTO_FOCUS", __func__);
		return -1;
	}

	return 0;
}

/* Frame rate */

int SecCamera::setFrameRate(int frame_rate)
{
	LOGV("%s(FrameRate(%d))", __func__, frame_rate);

	if (frame_rate < FRAME_RATE_AUTO || FRAME_RATE_MAX < frame_rate )
		LOGE("ERR(%s):Invalid frame_rate(%d)", __func__, frame_rate);

	if (m_params->capture.timeperframe.denominator != (unsigned)frame_rate) {
		m_params->capture.timeperframe.denominator = frame_rate;
		if (m_flag_camera_start) {
			if (device->setCtrl(V4L2_CID_CAMERA_FRAME_RATE, frame_rate) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_FRAME_RATE", __func__);
				return -1;
			}
		}
	}

	return 0;
}

/* Mirror */

int SecCamera::setVerticalMirror(void)
{
	LOGV("%s :", __func__);

	if (!device) {
		LOGE("ERR(%s):Camera was closed\n", __func__);
		return -1;
	}

	if (device->setCtrl(V4L2_CID_VFLIP, 0) < 0) {
		LOGE("ERR(%s):Fail on V4L2_CID_VFLIP", __func__);
		return -1;
	}

	return 0;
}

int SecCamera::setHorizontalMirror(void)
{
	LOGV("%s :", __func__);

	if (!device) {
		LOGE("ERR(%s):Camera was closed\n", __func__);
		return -1;
	}

	if (device->setCtrl(V4L2_CID_HFLIP, 0) < 0) {
		LOGE("ERR(%s):Fail on V4L2_CID_HFLIP", __func__);
		return -1;
	}

	return 0;
}

/* White balance */

int SecCamera::setWhiteBalance(int white_balance)
{
	LOGV("%s(white_balance(%d))", __func__, white_balance);

	if (white_balance <= WHITE_BALANCE_BASE || WHITE_BALANCE_MAX <= white_balance) {
		LOGE("ERR(%s):Invalid white_balance(%d)", __func__, white_balance);
		return -1;
	}

	if (m_params->white_balance != white_balance) {
		m_params->white_balance = white_balance;
		if (m_flag_camera_start) {
			if (device->setCtrl(V4L2_CID_CAMERA_WHITE_BALANCE, white_balance) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_WHITE_BALANCE", __func__);
				return -1;
			}
		}
	}

	return 0;
}

int SecCamera::getWhiteBalance(void)
{
	LOGV("%s : white_balance(%d)", __func__, m_params->white_balance);
	return m_params->white_balance;
}

/* Brightness */

int SecCamera::setBrightness(int brightness)
{
	LOGV("%s(brightness(%d))", __func__, brightness);

	brightness += EV_DEFAULT;

	if (brightness < EV_MINUS_4 || EV_PLUS_4 < brightness) {
		LOGE("ERR(%s):Invalid brightness(%d)", __func__, brightness);
		return -1;
	}

	if (m_params->brightness != brightness) {
		m_params->brightness = brightness;
		if (m_flag_camera_start) {
			if (device->setCtrl(V4L2_CID_CAMERA_BRIGHTNESS, brightness) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_BRIGHTNESS", __func__);
				return -1;
			}
		}
	}

	return 0;
}

int SecCamera::getBrightness(void)
{
	LOGV("%s : brightness(%d)", __func__, m_params->brightness);
	return m_params->brightness;
}

/* Image effect */

int SecCamera::setImageEffect(int image_effect)
{
	LOGV("%s(image_effect(%d))", __func__, image_effect);

	if (image_effect <= IMAGE_EFFECT_BASE || IMAGE_EFFECT_MAX <= image_effect) {
		LOGE("ERR(%s):Invalid image_effect(%d)", __func__, image_effect);
		return -1;
	}

	if (m_params->effects != image_effect) {
		m_params->effects = image_effect;
		if (m_flag_camera_start) {
			if (device->setCtrl(V4L2_CID_CAMERA_EFFECT, image_effect) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_EFFECT", __func__);
				return -1;
			}
		}
	}

	return 0;
}

int SecCamera::getImageEffect(void)
{
	LOGV("%s : image_effect(%d)", __func__, m_params->effects);
	return m_params->effects;
}

/* Scene mode */

int SecCamera::setSceneMode(int scene_mode)
{
	LOGV("%s(scene_mode(%d))", __func__, scene_mode);

	if (scene_mode <= SCENE_MODE_BASE || SCENE_MODE_MAX <= scene_mode) {
		LOGE("ERR(%s):Invalid scene_mode (%d)", __func__, scene_mode);
		return -1;
	}

	if (m_params->scene_mode != scene_mode) {
		m_params->scene_mode = scene_mode;
		if (m_flag_camera_start) {
			if (device->setCtrl(V4L2_CID_CAMERA_SCENE_MODE, scene_mode) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SCENE_MODE", __func__);
				return -1;
			}
		}
	}

	return 0;
}

int SecCamera::getSceneMode(void)
{
	return m_params->scene_mode;
}

/* ISO */

int SecCamera::setISO(int iso_value)
{
	LOGV("%s(iso_value(%d))", __func__, iso_value);
	if (iso_value < ISO_AUTO || ISO_MAX <= iso_value) {
		LOGE("ERR(%s):Invalid iso_value (%d)", __func__, iso_value);
		return -1;
	}

	if (m_params->iso != iso_value) {
		m_params->iso = iso_value;
		if (m_flag_camera_start) {
			if (device->setCtrl(V4L2_CID_CAMERA_ISO, iso_value) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_ISO", __func__);
				return -1;
			}
		}
	}

	return 0;
}

int SecCamera::getISO(void)
{
	return m_params->iso;
}

/* Contrast */

int SecCamera::setContrast(int contrast_value)
{
	LOGV("%s(contrast_value(%d))", __func__, contrast_value);

	if (contrast_value < CONTRAST_MINUS_2 || CONTRAST_MAX <= contrast_value) {
		LOGE("ERR(%s):Invalid contrast_value (%d)", __func__, contrast_value);
		return -1;
	}

	if (m_params->contrast != contrast_value) {
		m_params->contrast = contrast_value;
		if (m_flag_camera_start) {
			if (device->setCtrl(V4L2_CID_CAMERA_CONTRAST, contrast_value) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_CONTRAST", __func__);
				return -1;
			}
		}
	}

	return 0;
}

int SecCamera::getContrast(void)
{
	return m_params->contrast;
}

/* Saturation */

int SecCamera::setSaturation(int saturation_value)
{
	LOGV("%s(saturation_value(%d))", __func__, saturation_value);

	if (saturation_value <SATURATION_MINUS_2 || SATURATION_MAX<= saturation_value) {
		LOGE("ERR(%s):Invalid saturation_value (%d)", __func__, saturation_value);
		return -1;
	}

	if (m_params->saturation != saturation_value) {
		m_params->saturation = saturation_value;
		if (m_flag_camera_start) {
			if (device->setCtrl(V4L2_CID_CAMERA_SATURATION, saturation_value) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SATURATION", __func__);
				return -1;
			}
		}
	}

	return 0;
}

int SecCamera::getSaturation(void)
{
	return m_params->saturation;
}

/* Sharpness */

int SecCamera::setSharpness(int sharpness_value)
{
	LOGV("%s(sharpness_value(%d))", __func__, sharpness_value);

	if (sharpness_value < SHARPNESS_MINUS_2 || SHARPNESS_MAX <= sharpness_value) {
		LOGE("ERR(%s):Invalid sharpness_value (%d)", __func__, sharpness_value);
		return -1;
	}

	if (m_params->sharpness != sharpness_value) {
		m_params->sharpness = sharpness_value;
		if (m_flag_camera_start) {
			if (device->setCtrl(V4L2_CID_CAMERA_SHARPNESS, sharpness_value) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SHARPNESS", __func__);
				return -1;
			}
		}
	}

	return 0;
}

int SecCamera::getSharpness(void)
{
	return m_params->sharpness;
}

/* WDR */

int SecCamera::setWDR(int wdr_value)
{
	LOGV("%s(wdr_value(%d))", __func__, wdr_value);

	if (wdr_value < WDR_OFF || WDR_MAX <= wdr_value) {
		LOGE("ERR(%s):Invalid wdr_value (%d)", __func__, wdr_value);
		return -1;
	}

	if (m_wdr != wdr_value) {
		m_wdr = wdr_value;
		if (m_flag_camera_start) {
			if (device->setCtrl(V4L2_CID_CAMERA_WDR, wdr_value) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_WDR", __func__);
				return -1;
			}
		}
	}

	return 0;
}

int SecCamera::getWDR(void)
{
	return m_wdr;
}

/* Antishake */

int SecCamera::setAntiShake(int anti_shake)
{
	LOGV("%s(anti_shake(%d))", __func__, anti_shake);

	if (anti_shake < ANTI_SHAKE_OFF || ANTI_SHAKE_MAX <= anti_shake) {
		LOGE("ERR(%s):Invalid anti_shake (%d)", __func__, anti_shake);
		return -1;
	}

	if (m_anti_shake != anti_shake) {
		m_anti_shake = anti_shake;
		if (m_flag_camera_start) {
			if (device->setCtrl(V4L2_CID_CAMERA_ANTI_SHAKE, anti_shake) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_ANTI_SHAKE", __func__);
				return -1;
			}
		}
	}

	return 0;
}

int SecCamera::getAntiShake(void)
{
	return m_anti_shake;
}

/* Metering */

int SecCamera::setMetering(int metering_value)
{
	LOGV("%s(metering (%d))", __func__, metering_value);

	if (metering_value <= METERING_BASE || METERING_MAX <= metering_value) {
		LOGE("ERR(%s):Invalid metering_value (%d)", __func__, metering_value);
		return -1;
	}

	if (m_params->metering != metering_value) {
		m_params->metering = metering_value;
		if (m_flag_camera_start) {
			if (device->setCtrl(V4L2_CID_CAMERA_METERING, metering_value) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_METERING", __func__);
				return -1;
			}
		}
	}

	return 0;
}

int SecCamera::getMetering(void)
{
	return m_params->metering;
}

/* JPEG quality */

int SecCamera::setJpegQuality(int jpeg_quality)
{
	LOGV("%s(jpeg_quality (%d))", __func__, jpeg_quality);

	if (jpeg_quality < JPEG_QUALITY_ECONOMY || JPEG_QUALITY_MAX <= jpeg_quality) {
		LOGE("ERR(%s):Invalid jpeg_quality (%d)", __func__, jpeg_quality);
		return -1;
	}

	if (m_jpeg_quality != jpeg_quality) {
		m_jpeg_quality = jpeg_quality;
		if (m_flag_camera_start && (m_camera_id == CAMERA_ID_BACK)) {
			if (device->setCtrl(V4L2_CID_CAM_JPEG_QUALITY, jpeg_quality) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAM_JPEG_QUALITY", __func__);
				return -1;
			}
		}
	}

	return 0;
}

int SecCamera::getJpegQuality(void)
{
	return m_jpeg_quality;
}

/* Focus mode */

int SecCamera::setFocusMode(int focus_mode)
{
	LOGV("%s(focus_mode(%d))", __func__, focus_mode);

	if (FOCUS_MODE_MAX <= focus_mode) {
		LOGE("ERR(%s):Invalid focus_mode (%d)", __func__, focus_mode);
		return -1;
	}

	if (m_params->focus_mode != focus_mode) {
		m_params->focus_mode = focus_mode;

		if (m_flag_camera_start) {
			if (device->setCtrl(V4L2_CID_CAMERA_FOCUS_MODE, focus_mode) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_FOCUS_MODE", __func__);
				return -1;
			}
		}
	}

	return 0;
}

int SecCamera::getFocusMode(void)
{
	return m_params->focus_mode;
}

/* GPS coordinates (for EXIF) */

int SecCamera::setGPSLatitude(const char *gps_latitude)
{
	double conveted_latitude = 0;
	LOGV("%s(gps_latitude(%s))", __func__, gps_latitude);
	if (gps_latitude == NULL)
		m_gps_latitude = 0;
	else {
		conveted_latitude = atof(gps_latitude);
		m_gps_latitude = (long)(conveted_latitude * 10000 / 1);
	}

	LOGV("%s(m_gps_latitude(%ld))", __func__, m_gps_latitude);
	return 0;
}

int SecCamera::setGPSLongitude(const char *gps_longitude)
{
	double conveted_longitude = 0;
	LOGV("%s(gps_longitude(%s))", __func__, gps_longitude);
	if (gps_longitude == NULL)
		m_gps_longitude = 0;
	else {
		conveted_longitude = atof(gps_longitude);
		m_gps_longitude = (long)(conveted_longitude * 10000 / 1);
	}

	LOGV("%s(m_gps_longitude(%ld))", __func__, m_gps_longitude);
	return 0;
}

int SecCamera::setGPSAltitude(const char *gps_altitude)
{
	double conveted_altitude = 0;
	LOGV("%s(gps_altitude(%s))", __func__, gps_altitude);
	if (gps_altitude == NULL)
		m_gps_altitude = 0;
	else {
		conveted_altitude = atof(gps_altitude);
		m_gps_altitude = (long)(conveted_altitude * 100 / 1);
	}

	LOGV("%s(m_gps_altitude(%ld))", __func__, m_gps_altitude);
	return 0;
}

int SecCamera::setGPSTimeStamp(const char *gps_timestamp)
{
	LOGV("%s(gps_timestamp(%s))", __func__, gps_timestamp);
	if (gps_timestamp == NULL)
		m_gps_timestamp = 0;
	else
		m_gps_timestamp = atol(gps_timestamp);

	LOGV("%s(m_gps_timestamp(%ld))", __func__, m_gps_timestamp);
	return 0;
}

int SecCamera::setGPSProcessingMethod(const char *gps_processing_method)
{
	LOGV("%s(gps_processing_method(%s))", __func__, gps_processing_method);
	memset(mExifInfo.gps_processing_method, 0, sizeof(mExifInfo.gps_processing_method));
	if (gps_processing_method != NULL) {
		size_t len = strlen(gps_processing_method);
		if (len > sizeof(mExifInfo.gps_processing_method)) {
			len = sizeof(mExifInfo.gps_processing_method);
		}
		memcpy(mExifInfo.gps_processing_method, gps_processing_method, len);
	}
	return 0;
}

/* Gamma */
// 
int SecCamera::setGamma(int gamma)
{
	LOGV("%s(gamma(%d))", __func__, gamma);

	if (gamma < GAMMA_OFF || GAMMA_MAX <= gamma) {
		LOGE("ERR(%s):Invalid gamma (%d)", __func__, gamma);
		return -1;
	}

	if (m_video_gamma != gamma) {
		m_video_gamma = gamma;
		if (m_flag_camera_start) {
			if (device->setCtrl(V4L2_CID_CAMERA_SET_GAMMA, gamma) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SET_GAMMA", __func__);
				return -1;
			}
		}
	}

	return 0;
}

/* Slow AE */

int SecCamera::setSlowAE(int slow_ae)
{
	LOGV("%s(slow_ae(%d))", __func__, slow_ae);

	if (slow_ae < GAMMA_OFF || GAMMA_MAX <= slow_ae) {
		LOGE("ERR(%s):Invalid slow_ae (%d)", __func__, slow_ae);
		return -1;
	}

	if (m_slow_ae!= slow_ae) {
		m_slow_ae = slow_ae;
		if (m_flag_camera_start) {
			if (device->setCtrl(V4L2_CID_CAMERA_SET_SLOW_AE, slow_ae) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SET_SLOW_AE", __func__);
				return -1;
			}
		}
	}

	return 0;
}

/* Recording size */

int SecCamera::setRecordingSize(int width, int height)
{
	LOGV("%s(width(%d), height(%d))", __func__, width, height);

	m_recording_width  = width;
	m_recording_height = height;

	return 0;
}

/* Orientation (for EXIF) */

int SecCamera::setExifOrientationInfo(int orientationInfo)
{
	LOGV("%s(orientationInfo(%d))", __func__, orientationInfo);

	if (orientationInfo < 0) {
		LOGE("ERR(%s):Invalid orientationInfo (%d)", __func__, orientationInfo);
		return -1;
	}
	m_exif_orientation = orientationInfo;

	return 0;
}

/*Video call*/

int SecCamera::setVTmode(int vtmode)
{
	LOGV("%s(vtmode (%d))", __func__, vtmode);

	if (vtmode < VT_MODE_OFF || VT_MODE_MAX <= vtmode) {
		LOGE("ERR(%s):Invalid vtmode (%d)", __func__, vtmode);
		return -1;
	}

	if (m_vtmode != vtmode) {
		m_vtmode = vtmode;
	}

	return 0;
}

int SecCamera::getVTmode(void)
{
	return m_vtmode;
}

/* Camcorder fix fps */

int SecCamera::setSensorMode(int sensor_mode)
{
	LOGV("%s(sensor_mode (%d))", __func__, sensor_mode);

	if (sensor_mode < SENSOR_MODE_CAMERA || SENSOR_MODE_MOVIE < sensor_mode) {
		LOGE("ERR(%s):Invalid sensor mode (%d)", __func__, sensor_mode);
		return -1;
	}

	if (m_sensor_mode != sensor_mode) {
		m_sensor_mode = sensor_mode;
	}

	return 0;
}

/*  Shot mode   */
/*  SINGLE = 0
*   CONTINUOUS = 1
*   PANORAMA = 2
*   SMILE = 3
*   SELF = 6
*/
int SecCamera::setShotMode(int shot_mode)
{
	LOGV("%s(shot_mode (%d))", __func__, shot_mode);
	if (shot_mode < SHOT_MODE_SINGLE || SHOT_MODE_SELF < shot_mode) {
		LOGE("ERR(%s):Invalid shot_mode (%d)", __func__, shot_mode);
		return -1;
	}
	m_shot_mode = shot_mode;

	return 0;
}

int SecCamera::setBlur(int blur_level)
{
	LOGV("%s(level (%d))", __func__, blur_level);

	if (blur_level < BLUR_LEVEL_0 || BLUR_LEVEL_MAX <= blur_level) {
		LOGE("ERR(%s):Invalid level (%d)", __func__, blur_level);
		return -1;
	}

	if (m_blur_level != blur_level) {
		m_blur_level = blur_level;
		if (m_flag_camera_start) {
			if (device->setCtrl(V4L2_CID_CAMERA_VGA_BLUR, blur_level) < 0) {
				LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_VGA_BLUR", __func__);
				return -1;
			}
		}
	}
	return 0;
}

int SecCamera::getBlur(void)
{
	return m_blur_level;
}

const __u8* SecCamera::getCameraSensorName(void)
{
	LOGV("%s", __func__);

	if (!device)
		return NULL;

	return device->enumInput(getCameraId());
}

/* JPEG */

int SecCamera::setJpegThumbnailSize(int width, int height)
{
	LOGV("%s(width(%d), height(%d))", __func__, width, height);

	m_jpeg_thumbnail_width  = width;
	m_jpeg_thumbnail_height = height;

	return 0;
}

int SecCamera::getJpegThumbnailSize(int *width, int  *height)
{
	if (width)
		*width   = m_jpeg_thumbnail_width;
	if (height)
		*height  = m_jpeg_thumbnail_height;

	return 0;
}

void SecCamera::setExifFixedAttribute()
{
	char property[PROPERTY_VALUE_MAX];

	//2 0th IFD TIFF Tags
	//3 Maker
	property_get("ro.product.brand", property, EXIF_DEF_MAKER);
	strncpy((char *)mExifInfo.maker, property,
		sizeof(mExifInfo.maker) - 1);
	mExifInfo.maker[sizeof(mExifInfo.maker) - 1] = '\0';
	//3 Model
	property_get("ro.product.model", property, EXIF_DEF_MODEL);
	strncpy((char *)mExifInfo.model, property,
		sizeof(mExifInfo.model) - 1);
	mExifInfo.model[sizeof(mExifInfo.model) - 1] = '\0';
	//3 Software
	property_get("ro.build.id", property, EXIF_DEF_SOFTWARE);
	strncpy((char *)mExifInfo.software, property,
		sizeof(mExifInfo.software) - 1);
	mExifInfo.software[sizeof(mExifInfo.software) - 1] = '\0';

	//3 YCbCr Positioning
	mExifInfo.ycbcr_positioning = EXIF_DEF_YCBCR_POSITIONING;

	//2 0th IFD Exif Private Tags
	//3 F Number
	mExifInfo.fnumber.num = EXIF_DEF_FNUMBER_NUM;
	mExifInfo.fnumber.den = EXIF_DEF_FNUMBER_DEN;
	//3 Exposure Program
	mExifInfo.exposure_program = EXIF_DEF_EXPOSURE_PROGRAM;
	//3 Exif Version
	memcpy(mExifInfo.exif_version, EXIF_DEF_EXIF_VERSION, sizeof(mExifInfo.exif_version));
	//3 Aperture
	uint32_t av = APEX_FNUM_TO_APERTURE((double)mExifInfo.fnumber.num/mExifInfo.fnumber.den);
	mExifInfo.aperture.num = av*EXIF_DEF_APEX_DEN;
	mExifInfo.aperture.den = EXIF_DEF_APEX_DEN;
	//3 Maximum lens aperture
	mExifInfo.max_aperture.num = mExifInfo.aperture.num;
	mExifInfo.max_aperture.den = mExifInfo.aperture.den;
	//3 Lens Focal Length

	mExifInfo.focal_length.num = BACK_CAMERA_FOCAL_LENGTH;

	mExifInfo.focal_length.den = EXIF_DEF_FOCAL_LEN_DEN;
	//3 User Comments
	strcpy((char *)mExifInfo.user_comment, EXIF_DEF_USERCOMMENTS);
	//3 Color Space information
	mExifInfo.color_space = EXIF_DEF_COLOR_SPACE;
	//3 Exposure Mode
	mExifInfo.exposure_mode = EXIF_DEF_EXPOSURE_MODE;

	//2 0th IFD GPS Info Tags
	unsigned char gps_version[4] = { 0x02, 0x02, 0x00, 0x00 };
	memcpy(mExifInfo.gps_version_id, gps_version, sizeof(gps_version));

	//2 1th IFD TIFF Tags
	mExifInfo.compression_scheme = EXIF_DEF_COMPRESSION;
	mExifInfo.x_resolution.num = EXIF_DEF_RESOLUTION_NUM;
	mExifInfo.x_resolution.den = EXIF_DEF_RESOLUTION_DEN;
	mExifInfo.y_resolution.num = EXIF_DEF_RESOLUTION_NUM;
	mExifInfo.y_resolution.den = EXIF_DEF_RESOLUTION_DEN;
	mExifInfo.resolution_unit = EXIF_DEF_RESOLUTION_UNIT;
}

void SecCamera::setExifChangedAttribute()
{
	//2 0th IFD TIFF Tags
	//3 Width
	mExifInfo.width = m_snapshot_width;
	//3 Height
	mExifInfo.height = m_snapshot_height;
	//3 Orientation
	switch (m_exif_orientation) {
	case 0:
		mExifInfo.orientation = EXIF_ORIENTATION_UP;
		break;
	case 90:
		mExifInfo.orientation = EXIF_ORIENTATION_90;
		break;
	case 180:
		mExifInfo.orientation = EXIF_ORIENTATION_180;
		break;
	case 270:
		mExifInfo.orientation = EXIF_ORIENTATION_270;
		break;
	default:
		mExifInfo.orientation = EXIF_ORIENTATION_UP;
		break;
	}
	//3 Date time
	time_t rawtime;
	struct tm *timeinfo;
	time(&rawtime);
	timeinfo = localtime(&rawtime);
	strftime((char *)mExifInfo.date_time, 20, "%Y:%m:%d %H:%M:%S", timeinfo);

	//2 0th IFD Exif Private Tags
	//3 Exposure Time
	int shutterSpeed = device->getCtrl(V4L2_CID_CAMERA_GET_SHT_TIME);
	/* TBD - front camera needs to be fixed to support this g_ctrl,
	   it current returns a negative err value, so avoid putting
	   odd value into exif for now */
	if (shutterSpeed < 0) {
		LOGE("%s: error %d getting shutterSpeed, camera_id = %d, using 100",
		     __func__, shutterSpeed, m_camera_id);
		shutterSpeed = 100;
	}
	mExifInfo.exposure_time.num = 1;
	// x us -> 1/x s */
	mExifInfo.exposure_time.den = (uint32_t)(1000000 / shutterSpeed);

	//3 ISO Speed Rating
	int iso = device->getCtrl(V4L2_CID_CAMERA_GET_ISO);
	/* TBD - front camera needs to be fixed to support this g_ctrl,
	   it current returns a negative err value, so avoid putting
	   odd value into exif for now */
	if (iso < 0) {
		LOGE("%s: error %d getting iso, camera_id = %d, using 100",
		     __func__, iso, m_camera_id);
		iso = ISO_100;
	}
	switch(iso) {
	case ISO_50:
		mExifInfo.iso_speed_rating = 50;
		break;
	case ISO_100:
		mExifInfo.iso_speed_rating = 100;
		break;
	case ISO_200:
		mExifInfo.iso_speed_rating = 200;
		break;
	case ISO_400:
		mExifInfo.iso_speed_rating = 400;
		break;
	case ISO_800:
		mExifInfo.iso_speed_rating = 800;
		break;
	case ISO_1600:
		mExifInfo.iso_speed_rating = 1600;
		break;
	default:
		mExifInfo.iso_speed_rating = 100;
		break;
	}

	uint32_t av, tv, bv, sv, ev;
	av = APEX_FNUM_TO_APERTURE((double)mExifInfo.fnumber.num / mExifInfo.fnumber.den);
	tv = APEX_EXPOSURE_TO_SHUTTER((double)mExifInfo.exposure_time.num / mExifInfo.exposure_time.den);
	sv = APEX_ISO_TO_FILMSENSITIVITY(mExifInfo.iso_speed_rating);
	bv = av + tv - sv;
	ev = av + tv;
	LOGD("Shutter speed=%d us, iso=%d\n", shutterSpeed, mExifInfo.iso_speed_rating);
	LOGD("AV=%d, TV=%d, SV=%d\n", av, tv, sv);

	//3 Shutter Speed
	mExifInfo.shutter_speed.num = tv*EXIF_DEF_APEX_DEN;
	mExifInfo.shutter_speed.den = EXIF_DEF_APEX_DEN;
	//3 Brightness
	mExifInfo.brightness.num = bv*EXIF_DEF_APEX_DEN;
	mExifInfo.brightness.den = EXIF_DEF_APEX_DEN;
	//3 Exposure Bias
	if (m_params->scene_mode == SCENE_MODE_BEACH_SNOW) {
		mExifInfo.exposure_bias.num = EXIF_DEF_APEX_DEN;
		mExifInfo.exposure_bias.den = EXIF_DEF_APEX_DEN;
	} else {
		mExifInfo.exposure_bias.num = 0;
		mExifInfo.exposure_bias.den = 0;
	}
	//3 Metering Mode
	switch (m_params->metering) {
	case METERING_SPOT:
		mExifInfo.metering_mode = EXIF_METERING_SPOT;
		break;
	case METERING_MATRIX:
		mExifInfo.metering_mode = EXIF_METERING_AVERAGE;
		break;
	case METERING_CENTER:
		mExifInfo.metering_mode = EXIF_METERING_CENTER;
		break;
	default :
		mExifInfo.metering_mode = EXIF_METERING_AVERAGE;
		break;
	}

	//3 Flash
	int flash = device->getCtrl(V4L2_CID_CAMERA_GET_FLASH_ONOFF);
	if (flash < 0)
		mExifInfo.flash = EXIF_DEF_FLASH;
	else
		mExifInfo.flash = flash;

	//3 White Balance
	if (m_params->white_balance == WHITE_BALANCE_AUTO)
		mExifInfo.white_balance = EXIF_WB_AUTO;
	else
		mExifInfo.white_balance = EXIF_WB_MANUAL;
	//3 Scene Capture Type
	switch (m_params->scene_mode) {
	case SCENE_MODE_PORTRAIT:
		mExifInfo.scene_capture_type = EXIF_SCENE_PORTRAIT;
		break;
	case SCENE_MODE_LANDSCAPE:
		mExifInfo.scene_capture_type = EXIF_SCENE_LANDSCAPE;
		break;
	case SCENE_MODE_NIGHTSHOT:
		mExifInfo.scene_capture_type = EXIF_SCENE_NIGHT;
		break;
	default:
		mExifInfo.scene_capture_type = EXIF_SCENE_STANDARD;
		break;
	}

	//2 0th IFD GPS Info Tags
	if (m_gps_latitude != 0 && m_gps_longitude != 0) {
		if (m_gps_latitude > 0)
			strcpy((char *)mExifInfo.gps_latitude_ref, "N");
		else
			strcpy((char *)mExifInfo.gps_latitude_ref, "S");

		if (m_gps_longitude > 0)
			strcpy((char *)mExifInfo.gps_longitude_ref, "E");
		else
			strcpy((char *)mExifInfo.gps_longitude_ref, "W");

		if (m_gps_altitude > 0)
			mExifInfo.gps_altitude_ref = 0;
		else
			mExifInfo.gps_altitude_ref = 1;

		double latitude = fabs(m_gps_latitude / 10000.0);
		double longitude = fabs(m_gps_longitude / 10000.0);
		double altitude = fabs(m_gps_altitude / 100.0);

		mExifInfo.gps_latitude[0].num = (uint32_t)latitude;
		mExifInfo.gps_latitude[0].den = 1;
		mExifInfo.gps_latitude[1].num = (uint32_t)((latitude - mExifInfo.gps_latitude[0].num) * 60);
		mExifInfo.gps_latitude[1].den = 1;
		mExifInfo.gps_latitude[2].num = (uint32_t)((((latitude - mExifInfo.gps_latitude[0].num) * 60)
						- mExifInfo.gps_latitude[1].num) * 60);
		mExifInfo.gps_latitude[2].den = 1;

		mExifInfo.gps_longitude[0].num = (uint32_t)longitude;
		mExifInfo.gps_longitude[0].den = 1;
		mExifInfo.gps_longitude[1].num = (uint32_t)((longitude - mExifInfo.gps_longitude[0].num) * 60);
		mExifInfo.gps_longitude[1].den = 1;
		mExifInfo.gps_longitude[2].num = (uint32_t)((((longitude - mExifInfo.gps_longitude[0].num) * 60)
						 - mExifInfo.gps_longitude[1].num) * 60);
		mExifInfo.gps_longitude[2].den = 1;

		mExifInfo.gps_altitude.num = (uint32_t)altitude;
		mExifInfo.gps_altitude.den = 1;

		struct tm tm_data;
		gmtime_r(&m_gps_timestamp, &tm_data);
		mExifInfo.gps_timestamp[0].num = tm_data.tm_hour;
		mExifInfo.gps_timestamp[0].den = 1;
		mExifInfo.gps_timestamp[1].num = tm_data.tm_min;
		mExifInfo.gps_timestamp[1].den = 1;
		mExifInfo.gps_timestamp[2].num = tm_data.tm_sec;
		mExifInfo.gps_timestamp[2].den = 1;
		snprintf((char*)mExifInfo.gps_datestamp, sizeof(mExifInfo.gps_datestamp),
			 "%04d:%02d:%02d", tm_data.tm_year + 1900, tm_data.tm_mon + 1, tm_data.tm_mday);

		mExifInfo.enableGps = true;
	} else {
		mExifInfo.enableGps = false;
	}

	//2 1th IFD TIFF Tags
	mExifInfo.widthThumb = m_jpeg_thumbnail_width;
	mExifInfo.heightThumb = m_jpeg_thumbnail_height;
}

/* WTFs */

status_t SecCamera::dump(int fd, const Vector<String16> &args)
{
	const size_t SIZE = 256;
	char buffer[SIZE];
	String8 result;
	snprintf(buffer, 255, "dump(%d)\n", fd);
	result.append(buffer);
	::write(fd, result.string(), result.size());
	return NO_ERROR;
}

}; // namespace android
