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

#define LOG_NDEBUG 0
#define LOG_TAG "V4L2Camera"

#include <utils/Log.h>

#include <string.h>
#include <stdlib.h>
#include <sys/poll.h>
#include "V4L2Camera.h"
#include "cutils/properties.h"
#include "V4L2Device.h"
#include "utils.h"

int Tracer::level = 0;

using namespace android;

namespace android {

/*
 * V4L2Camera
 */

/* Constructor/destructor */

V4L2Camera::V4L2Camera() :
	device(0),
	jpegEncoder(0),
	cameraId(CAMERA_ID_BACK),
	autoFocusDone(false),
	previewStarted(false),
	recordingStarted(false),
	previewConvBuffer(0),
	snapshotWidth(2048),
	snapshotHeight(1536),
	recordingWidth(640),
	recordingHeight(480),
	jpegThumbnailWidth(320),
	jpegThumbnailHeight(240)
{
	TRACE();

	memset(&captureBuf, 0, sizeof(captureBuf));
	memset(&ctrlValues, 0xff, sizeof(ctrlValues));

	setSnapshotPixelFormat(V4L2_PIX_FMT_YUYV);
	setPreviewSize(640, 480, V4L2_PIX_FMT_NV21);
}

V4L2Camera::~V4L2Camera()
{
	TRACE();
}

/* Open/close */

int V4L2Camera::isOpened(void) const
{
	TRACE();
	return device != 0;
}

int V4L2Camera::openCamera(int index)
{
	int ret = 0;

	TRACE();

	if (index != 0)
		return -1;

	if (device)
		return 0;

	device = new V4L2Device(CAMERA_DEV_NAME);
	if (!device || !device->initCheck()) {
		delete device;
		device = 0;
		ERR("failed to open %s (%s)", CAMERA_DEV_NAME, strerror(errno));
		return -1;
	}

	DBG("V4L2 device opened");

	ret = device->setBufType(V4L2_CAPTURE,
					V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (ret < 0) {
		ERR("failed to set buffer type");
		goto error;
	}

	if (device->enumInput(index) == NULL) {
		ERR("failed to enum input");
		goto error;
	}

	ret = device->setInput(index);
	if (ret < 0) {
		ERR("failed to set input");
		goto error;
	}

	jpegEncoder = new V4L2JpegEncoder(JPEG_DEV_NAME);
	if (!jpegEncoder) {
		ERR("failed to create jpeg encoder object");
		goto error;
	}

	cameraId = index;

	initControlValues();
	setExifFixedAttribute();

	return 0;

error:
	delete device;
	device = 0;

	return -1;
}

void V4L2Camera::closeCamera(void)
{
	TRACE();

	if (!device)
		return;

	stopRecord();

	delete device;
	device = 0;

	delete jpegEncoder;
	jpegEncoder = 0;
}

/* Preview */

sp<MemoryHeapBase> V4L2Camera::getBufferHeap(void)
{
	TRACE();

	if (previewAllocation == 0)
		return NULL;

	return previewAllocation->getHeap();
}

sp<MemoryBase> V4L2Camera::getBuffer(int index)
{
	TRACE();

	if (previewAllocation == 0)
		return NULL;

	return previewAllocation->getMemory(index);
}

int V4L2Camera::previewPoll(void)
{
	int ret;

	TRACE();

	ret = device->pollDevice(POLLIN | POLLERR, 1000);
	if (!(ret & POLLIN)) {
		ERR("poll error");
		return 0;
	}

	return ret;
}

int V4L2Camera::startPreview(void)
{
	int ret;

	TRACE();

	if (previewStarted) {
		ERR("preview already started");
		return 0;
	}

	if (!device) {
		ERR("camera device is not opened");
		return -1;
	}

	ret = device->enumFormat(V4L2_CAPTURE, previewFormat);
	if (ret < 0) {
		ERR("failed to enum format");
		return -1;
	}

	ret = device->setCtrl(V4L2_CID_S5K4CA_CAPTURE, 0);
	if (ret < 0) {
		ERR("failed to set preview mode");
		return -1;
	}

	ret = device->setFormat(V4L2_CAPTURE,
			previewWidth, previewHeight, previewFormat);
	if (ret < 0) {
		ERR("failed to set format");
		return -1;
	}

	size_t sizeReal = get_buffer_size(previewWidth,
					previewHeight, previewFormat);
	size_t sizeTarget = get_buffer_size(previewWidth,
					previewHeight, previewTargetFormat);
	size_t buf_size = max(sizeReal, sizeTarget);

	if (previewFormat != previewTargetFormat) {
		previewConvBuffer = malloc(sizeTarget);
		if (!previewConvBuffer) {
			ERR("failed to allocate conversion buffer");
			return -1;
		}
	}

	previewAllocation = new V4L2Allocation(REC_BUFFERS,
						buf_size, PMEM_DEV_NAME);
	if (previewAllocation == 0) {
		ERR("failed to allocate preview buffers");
		goto err_free_conv_buffer;
	}

	ret = device->reqBufs(V4L2_CAPTURE, previewAllocation);
	if (ret < 0) {
		ERR("failed to request buffers");
		goto err_free_v4l2_allocation;
	}

	DBG("previewWidth: %d previewHeight: %d", previewWidth, previewHeight);

	/*
	 * start with all buffers, but 0 in queue
	 * buffer 0 will be queued after first preview frame
	 */
	prevBufIdx = 0;
	for (int i = 1; i < REC_BUFFERS; i++) {
		ret = device->queueBuf(V4L2_CAPTURE, i);
		if (ret < 0) {
			ERR("failed to queue buffer %d", i);
			goto err_free_v4l2_allocation;
		}
	}

	ret = device->setStream(V4L2_CAPTURE, true);
	if (ret < 0) {
		ERR("failed to enable streaming");
		goto err_free_v4l2_allocation;
	}

	/* Wait for first frame */
	ret = device->pollDevice(POLLIN | POLLERR, 10000);
	if (!(ret & POLLIN)) {
		device->setStream(V4L2_CAPTURE, false);
		ERR("failed to get first frame");
		goto err_free_v4l2_allocation;
	}

	DBG("got the first frame of the preview");
	previewStarted = 1;

	return 0;

err_free_v4l2_allocation:
	previewAllocation.clear();
	device->reqBufs(V4L2_CAPTURE, 0);
err_free_conv_buffer:
	free(previewConvBuffer);
	previewConvBuffer = 0;

	return -1;
}

int V4L2Camera::stopPreview(void)
{
	int ret;

	TRACE();

	if (!previewStarted)
		return 0;

	if (!device) {
		ERR("camera device is not opened");
		return -1;
	}

	ret = device->setStream(V4L2_CAPTURE, false);
	if (ret < 0) {
		ERR("failed to stop streaming");
		return -1;
	}

	device->reqBufs(V4L2_CAPTURE, 0);
	previewAllocation.clear();
	free(previewConvBuffer);
	previewConvBuffer = 0;

	previewStarted = 0;

	return ret;
}

int V4L2Camera::getPreview()
{
	int index;
	int ret;

	TRACE();

	if (!previewStarted) {
		ERR("preview is not started");
		return -1;
	}

	if (!previewPoll()) {
		ERR("failed to get preview frame from device");
		stopPreview();
		return -1;
	}

	ret = device->queueBuf(V4L2_CAPTURE, prevBufIdx);
	if (ret < 0) {
		ERR("failed to queue buffer %d", prevBufIdx);
		return -1;
	}

	index = device->dequeueBuf(V4L2_CAPTURE);
	if (index < 0 || index >= REC_BUFFERS) {
		ERR("dequeued invalid buffer id %d\n", index);
		return -1;
	}

	prevBufIdx = index;

	if (previewFormat != previewTargetFormat) {
		convertFrame(previewAllocation->getBuffer(index),
				previewConvBuffer, previewWidth, previewHeight,
				previewTargetFormat);
	}

	return index;
}

int V4L2Camera::setPreviewSize(unsigned int width, unsigned int height,
							int pixelFormat)
{
	int realFormat = pixelFormat;

	TRACE();

	DBG("width(%d), height(%d), format(%d))", width, height, pixelFormat);

	switch (pixelFormat) {
	case V4L2_PIX_FMT_YUV420:
		DBG("preview format: V4L2_PIX_FMT_YUV420");
		break;
	case V4L2_PIX_FMT_YVU420:
		DBG("preview format: V4L2_PIX_FMT_YVU420");
		break;
	case V4L2_PIX_FMT_YUV422P:
		DBG("preview format: V4L2_PIX_FMT_YUV422P");
		break;
	case V4L2_PIX_FMT_YUYV:
		DBG("preview format: V4L2_PIX_FMT_YUYV");
		break;
	case V4L2_PIX_FMT_RGB565:
		DBG("preview format: V4L2_PIX_FMT_RGB565");
		break;
	case V4L2_PIX_FMT_NV21:
		realFormat = V4L2_PIX_FMT_YUV420;
		DBG("preview format: V4L2_PIX_FMT_NV21 (SW conversion)");
		break;
	default:
		ERR("unknown preview format");
		return -1;
	}

	previewWidth		= width;
	previewHeight		= height;
	previewFormat		= realFormat;
	previewTargetFormat	= pixelFormat;

	return 0;
}

int V4L2Camera::getPreviewSize(unsigned int *width,
				unsigned int *height, unsigned int *frame_size)
{
	unsigned int sizeReal, sizeTarget;

	TRACE();

	sizeReal = get_buffer_size(previewWidth, previewHeight, previewFormat);
	sizeTarget = get_buffer_size(previewWidth, previewHeight,
							previewTargetFormat);

	*width		= previewWidth;
	*height		= previewHeight;
	*frame_size	= max(sizeReal, sizeTarget);

	return 0;
}

int V4L2Camera::getPreviewMaxSize(unsigned int *width, unsigned int *height)
{
	TRACE();

	*width	= MAX_BACK_CAMERA_PREVIEW_WIDTH;
	*height	= MAX_BACK_CAMERA_PREVIEW_HEIGHT;

	return 0;
}

int V4L2Camera::getPreviewPixelFormat(void)
{
	TRACE();
	return previewTargetFormat;
}

/* Recording */

int V4L2Camera::startRecord(void)
{
	int ret, i;

	TRACE();

	if (recordingStarted)
		return 0;

	if (!device) {
		ERR("camera device is not opened");
		return -1;
	}

	ret = device->enumFormat(V4L2_CAPTURE, V4L2_PIX_FMT_YUYV);
	if (ret < 0) {
		ERR("failed to enum format");
		return -1;
	}

	DBG("recordingWidth = %d, recordingHeight = %d",
					recordingWidth, recordingHeight);

	ret = device->setFormat(V4L2_CAPTURE, recordingWidth,
				recordingHeight, V4L2_PIX_FMT_YUYV);
	if (ret < 0) {
		ERR("failed to set format");
		return -1;
	}

	size_t buf_size = get_buffer_size(recordingWidth,
				recordingHeight, V4L2_PIX_FMT_YUYV);

	recordAllocation =
		new V4L2Allocation(REC_BUFFERS, buf_size, PMEM_DEV_NAME);
	if (recordAllocation == 0) {
		ERR("failed to allocate record buffers");
		return -1;
	}

	ret = device->reqBufs(V4L2_CAPTURE, recordAllocation);
	if (ret < 0) {
		ERR("failed to request buffers");
		return -1;
	}

	/* start with all buffers in queue */
	for (i = 0; i < REC_BUFFERS; i++) {
		ret = device->queueBuf(V4L2_CAPTURE, i);
		if (ret < 0) {
			ERR("failed to queue buffer %d", i);
			goto err_free_v4l2_allocation;
		}
	}

	ret = device->setStream(V4L2_CAPTURE, true);
	if (ret < 0) {
		ERR("failed to start streaming");
		goto err_free_v4l2_allocation;
	}

	ret = device->pollDevice(POLLIN | POLLERR, 10000);
	if (!(ret & POLLIN)) {
		device->setStream(V4L2_CAPTURE, false);
		ERR("failed to get first frame");
		goto err_free_v4l2_allocation;
	}

	recordingStarted = true;

	return 0;

err_free_v4l2_allocation:
	device->reqBufs(V4L2_CAPTURE, 0);
	recordAllocation.clear();
	return -1;
}

int V4L2Camera::stopRecord(void)
{
	int ret;

	TRACE();

	if (!recordingStarted)
		return 0;

	if (!device) {
		ERR("camera device is not opened");
		return -1;
	}

	ret = device->setStream(V4L2_CAPTURE, false);
	if (ret < 0) {
		ERR("failed to stop streaming");
		return -1;
	}

	device->reqBufs(V4L2_CAPTURE, 0);
	recordAllocation.clear();

	recordingStarted = false;

	return 0;
}

int V4L2Camera::getRecordFrame()
{
	TRACE();

	if (!recordingStarted) {
		ERR("recording is not started");
		return -1;
	}

	previewPoll();
	return device->dequeueBuf(V4L2_CAPTURE);
}

int V4L2Camera::releaseRecordFrame(int index)
{
	TRACE();

	if (!recordingStarted)
		return 0;

	return device->queueBuf(V4L2_CAPTURE, index);
}

void V4L2Camera::getThumbnailConfig(unsigned int *width,
				unsigned int *height, unsigned int *size)
{
	TRACE();

	*width	= BACK_CAMERA_THUMBNAIL_WIDTH;
	*height	= BACK_CAMERA_THUMBNAIL_HEIGHT;
	*size	= BACK_CAMERA_THUMBNAIL_WIDTH*BACK_CAMERA_THUMBNAIL_HEIGHT
						*BACK_CAMERA_THUMBNAIL_BPP / 8;
}

/* Snapshot */
void V4L2Camera::dumpData(const void *data, size_t size, const char *path)
{
	FILE *f = fopen(path, "wb");
	if (!f) {
		ERR("failed to open dump file %s (%s)", path, strerror(errno));
		return;
	}

	if (fwrite(data, size, 1, f) < 1)
		ERR("failed to dump data to %s (%s)", path, strerror(errno));

	fclose(f);
}

int V4L2Camera::getSnapshotAndJpeg(void *yuv_buf, void *jpeg_buf,
						unsigned int *output_size)
{
	int index;
	unsigned char *addr;
	int ret = 0;
	unsigned int jpegQuality = 0;
	sp<V4L2Allocation> allocation;
	sp<V4L2Allocation> jpegAllocation;

	TRACE();

	if (!device) {
		ERR("camera device is not opened");
		return -1;
	}

	if (!yuv_buf && !jpeg_buf)
		return 0;

	if (previewStarted) {
		DBG("preview is started, stopping");
		stopPreview();
	}

	switch (snapshotFormat) {
	case V4L2_PIX_FMT_YUV420:
		DBG("snapshot format: V4L2_PIX_FMT_YUV420");
		break;
	case V4L2_PIX_FMT_YUV422P:
		DBG("snapshot format: V4L2_PIX_FMT_YUV422P");
		break;
	case V4L2_PIX_FMT_YUYV:
		DBG("snapshot format: V4L2_PIX_FMT_YUYV");
		break;
	case V4L2_PIX_FMT_UYVY:
		DBG("snapshot format: V4L2_PIX_FMT_UYVY");
		break;
	case V4L2_PIX_FMT_RGB565:
		DBG("snapshot format: V4L2_PIX_FMT_RGB565");
		break;
	case V4L2_PIX_FMT_NV21:
		DBG("snapshot format: V4L2_PIX_FMT_NV21");
		break;
	case V4L2_PIX_FMT_JPEG:
		DBG("snapshot format: V4L2_PIX_FMT_JPEG");
		break;
	default:
		DBG("unknown snapshot format");
	}

	ret = device->setCtrl(V4L2_CID_S5K4CA_CAPTURE, 1);
	if (ret < 0) {
		ERR("failed to set capture mode");
		return -1;
	}

	ret = device->enumFormat(V4L2_CAPTURE, snapshotFormat);
	if (ret < 0) {
		ERR("failed to enum format");
		return -1;
	}

	ret = device->setFormat(V4L2_CAPTURE, snapshotWidth,
				snapshotHeight, snapshotFormat);
	if (ret < 0) {
		ERR("failed to set format");
		return -1;
	}

	size_t sizeReal = get_buffer_size(snapshotWidth,
						snapshotHeight, snapshotFormat);
	size_t sizeTarget = get_buffer_size(snapshotWidth,
					snapshotHeight, snapshotTargetFormat);
	size_t buf_size = max(sizeReal, sizeTarget);

	allocation = new V4L2Allocation(1, buf_size, PMEM_DEV_NAME);
	if (allocation == 0 || allocation->getBufferCount() < 1) {
		ERR("failed to allocate snapshot buffer");
		return -1;
	}

	ret = device->reqBufs(V4L2_CAPTURE, allocation);
	if (ret < 0) {
		ERR("failed to request buffers");
		return -1;
	}

	V4L2Buffer *captureBuf = allocation->getBuffer(0);
	if (!captureBuf) {
		ERR("failed to get buffer info");
		goto error;
	}

	ret = device->queueBuf(V4L2_CAPTURE, 0);
	if (ret < 0) {
		ERR("failed to queue buffer");
		goto error;
	}

	ret = device->setStream(V4L2_CAPTURE, true);
	if (ret < 0) {
		ERR("failed to start streaming");
		goto error;
	}

	ret = device->pollDevice(POLLIN | POLLERR, 10000);
	if (!(ret & POLLIN)) {
		ERR("failed to get image frame");
		goto error;
	}

	index = device->dequeueBuf(V4L2_CAPTURE);
	if (index < 0) {
		ERR("failed to dequeue buffer");
		goto error;
	}

	DBG("captured image frame");

	device->setStream(V4L2_CAPTURE, false);
	device->reqBufs(V4L2_CAPTURE, 0);

	if (jpeg_buf) {
		DBG("creating JPEG image");

		ret = jpegEncoder->setInput(allocation,
				snapshotWidth, snapshotHeight, snapshotFormat);
		if (ret < 0) {
			ERR("failed to set JPEG encoder input");
			goto error;
		}

		jpegAllocation = new V4L2Allocation(1, buf_size, PMEM_DEV_NAME);
		if (jpegAllocation == 0
		    || jpegAllocation->getBufferCount() < 1) {
			ERR("failed to allocate JPEG buffer");
			goto error;
		}

		jpegQuality = (100 - getControl(CAMERA_CTRL_JPEG_QUALITY));
		jpegQuality = JPEG_MAX_QUALITY*jpegQuality / 100;

		ret = jpegEncoder->setOutput(jpegAllocation, jpegQuality, true);
		if (ret < 0) {
			ERR("failed to set JPEG encoder output");
			goto error;
		}

		setExifChangedAttribute();

		ret = jpegEncoder->run();
		if (ret < 0) {
			ERR("failed to create JPEG image");
			goto error;
		}

		const V4L2Buffer *jpegBuffer = jpegAllocation->getBuffer(0);
		memcpy(jpeg_buf, jpegBuffer->getAddress(), ret);
		*output_size = ret;
	}

	dumpData(captureBuf->getAddress(), sizeReal, "/data/snapshot.raw");
	dumpData(jpeg_buf, ret, "/data/snapshot.jpg");

	if (yuv_buf) {
		DBG("copying raw image data");

		if (snapshotFormat != snapshotTargetFormat) {
			void *convBuffer = malloc(sizeTarget);
			if (!convBuffer) {
				ERR("failed to allocate conversion buffer");
				return -1;
			}

			convertFrame(captureBuf, convBuffer,
						snapshotWidth, snapshotHeight,
						snapshotTargetFormat);

			free(convBuffer);
		}

		memcpy(yuv_buf, captureBuf->getAddress(), buf_size);
	}

	return 0;

error:
	device->setStream(V4L2_CAPTURE, false);
	device->reqBufs(V4L2_CAPTURE, 0);

	return -1;
}

int V4L2Camera::setSnapshotSize(unsigned int width, unsigned int height)
{
	TRACE();
	DBG("(width(%d), height(%d))", width, height);

	snapshotWidth  = width;
	snapshotHeight = height;

	return 0;
}

int V4L2Camera::getSnapshotSize(unsigned int *width,
				unsigned int *height, unsigned int *frame_size)
{
	unsigned int sizeReal, sizeTarget;

	TRACE();

	sizeReal = get_buffer_size(snapshotWidth,
					snapshotHeight, snapshotFormat);
	sizeTarget = get_buffer_size(snapshotWidth,
					snapshotHeight, snapshotTargetFormat);

	*width  = snapshotWidth;
	*height = snapshotHeight;
	*frame_size = max(sizeReal, sizeTarget);

	if (*frame_size == 0)
		return -1;

	return 0;
}

int V4L2Camera::getSnapshotMaxSize(unsigned int *width, unsigned int *height)
{
	TRACE();

	*width  = MAX_BACK_CAMERA_SNAPSHOT_WIDTH;
	*height = MAX_BACK_CAMERA_SNAPSHOT_HEIGHT;

	return 0;
}

int V4L2Camera::setSnapshotPixelFormat(int pixelFormat)
{
	int realFormat = pixelFormat;

	TRACE();

	switch (pixelFormat) {
	case V4L2_PIX_FMT_YUV420:
		DBG("snapshot format: V4L2_PIX_FMT_YUV420");
		break;
	case V4L2_PIX_FMT_YVU420:
		DBG("snapshot format: V4L2_PIX_FMT_YVU420");
		break;
	case V4L2_PIX_FMT_YUV422P:
		DBG("snapshot format: V4L2_PIX_FMT_YUV422P");
		break;
	case V4L2_PIX_FMT_YUYV:
		DBG("snapshot format: V4L2_PIX_FMT_YUYV");
		break;
	case V4L2_PIX_FMT_UYVY:
		DBG("snapshot format: V4L2_PIX_FMT_UYVY");
		break;
	case V4L2_PIX_FMT_RGB565:
		DBG("snapshot format: V4L2_PIX_FMT_RGB565");
		break;
	case V4L2_PIX_FMT_NV21:
		realFormat = V4L2_PIX_FMT_YUV420;
		DBG("snapshot format: V4L2_PIX_FMT_NV21");
		break;
	default:
		ERR("unknown snapshot format");
		return -1;
	}

	snapshotFormat = realFormat;
	snapshotTargetFormat = pixelFormat;

	return 0;
}

int V4L2Camera::getSnapshotPixelFormat(void)
{
	TRACE();
	return snapshotTargetFormat;
}

int V4L2Camera::endSnapshot(void)
{
	int ret;

	TRACE();

	return 0;
}

/*
 * Utils
 */

void V4L2Camera::convertYUV420ToNV21(void *buffer, void *tmp,
							int width, int height)
{
	unsigned int count = (width*height) / 4;

	TRACE();

	memcpy(tmp, (uint8_t *)buffer + width*height, width*height / 2);

	uint32_t *Cb32 = (uint32_t *)tmp;
	uint32_t *Cr32 = (uint32_t *)((uint8_t *)tmp + count);
	uint32_t *CbCr32 = (uint32_t *)((uint8_t *)buffer + width*height);

	while (count > 8) {
		uint32_t cr = *Cr32++;
		uint32_t cb = *Cb32++;
		uint32_t crcb1 = (cb & 0x000000ff) << 8
				| (cb & 0x0000ff00) << 16
				| (cr & 0x000000ff)
				| (cr & 0x0000ff00) << 8;
		uint32_t crcb2 = (cb & 0x00ff0000) >> 8
				| (cb & 0xff000000)
				| (cr & 0x00ff0000) >> 16
				| (cr & 0xff000000) >> 8;
		*CbCr32++ = crcb1;
		*CbCr32++ = crcb2;
		count -= 8;
	}

	if (count) {
		uint8_t *Cb8 = (uint8_t *)Cb32;
		uint8_t *Cr8 = (uint8_t *)Cr32;
		uint16_t *CbCr16 = (uint16_t *)CbCr32;

		do {
			*CbCr16++ = *Cb8++ << 8 | *Cr8++;
		} while (--count);
	}
}

int V4L2Camera::convertFrame(V4L2Buffer *buffer, void *convBuffer,
					int width, int height, int dstFormat)
{
	TRACE();

	switch (dstFormat) {
	case V4L2_PIX_FMT_NV21:
		convertYUV420ToNV21(buffer->getAddress(),
						convBuffer, width, height);
		break;
	default:
		return -1;
	}

	return 0;
}

/*
 * Controls
 */

/* Auto focus */

int V4L2Camera::setAutofocus(void)
{
	TRACE();

	if (!device) {
		ERR("camera device is not opened");
		return -1;
	}

	autoFocusDone = true;
	if (device->setCtrl(V4L2_CID_FOCUS_AUTO, 0) < 0)
		autoFocusDone = false;

	return 0;
}

int V4L2Camera::getAutoFocusResult(void)
{
	TRACE();
	DBG("autoFocusDone = %d", autoFocusDone);
	return autoFocusDone;
}

int V4L2Camera::cancelAutofocus(void)
{
	TRACE();
	return 0;
}

/*
 * Camera controls
 */

const unsigned int V4L2Camera::ctrlTable[CAMERA_CTRL_NUM] = {
	V4L2_CID_S5K4CA_FRAME_RATE,
	V4L2_CID_AUTO_WHITE_BALANCE,
	V4L2_CID_S5K4CA_WB_PRESET,
	V4L2_CID_BRIGHTNESS,
	V4L2_CID_COLORFX,
	V4L2_CID_S5K4CA_ISO,
	V4L2_CID_CONTRAST,
	V4L2_CID_SATURATION,
	V4L2_CID_SHARPNESS,
	V4L2_CID_S5K4CA_GLAMOUR,
	V4L2_CID_S5K4CA_METERING,
	V4L2_CID_S5K4CA_NIGHTSHOT,
	V4L2_CID_FOCUS_ABSOLUTE,
	V4L2_CID_EXPOSURE_AUTO,
	0, /* Internal */
	0, /* Internal */
	0, /* Internal */
	0, /* Internal */
	0, /* Internal */
	0, /* Internal */
	0, /* Internal */
};

int V4L2Camera::getControl(unsigned int ctrl)
{
	int val = 0;

	TRACE();

	if (ctrl >= CAMERA_CTRL_NUM) {
		ERR("invalid ctrl id %d", ctrl);
		return -EINVAL;
	}

	if (!ctrlTable[ctrl])
		return ctrlValues[ctrl];

	if (device->getCtrl(ctrlTable[ctrl], &val) < 0)
		ERR("failed to get control %u from device", ctrl);

	return val;
}

int V4L2Camera::setControl(unsigned int ctrl, int val)
{
	int ret = 0;

	TRACE();

	if (ctrl >= CAMERA_CTRL_NUM) {
		ERR("invalid ctrl id %d", ctrl);
		return -EINVAL;
	}

	DBG("setting ctrl %u to val %d", ctrl, val);

	ctrlValues[ctrl] = val;

	if (!ctrlTable[ctrl])
		return 0;

	return device->setCtrl(ctrlTable[ctrl], val);
}

void V4L2Camera::initControlValues(void)
{
	TRACE();

	for (int i = 0; i < CAMERA_CTRL_NUM; ++i) {
		if (ctrlTable[i]) {
			ctrlValues[i] = 0;
			if (device->getCtrl(ctrlTable[i], &ctrlValues[i]) < 0)
				ERR("failed to get control %u from device", i);
		}
	}
}

/* Scene mode */
const SceneControl sceneListNone[] = {
	{ CAMERA_CTRL_ISO,		S5K4CA_ISO_AUTO },
	{ CAMERA_CTRL_BRIGHTNESS,	0 },
	{ CAMERA_CTRL_SATURATION,	0 },
	{ CAMERA_CTRL_SHARPNESS,	0 },
	{ CAMERA_CTRL_GLAMOUR,		0 },
	{ CAMERA_CTRL_AUTO_WB,		1 },
	{ CAMERA_CTRL_METERING,		S5K4CA_METERING_CENTER },
	{ CAMERA_CTRL_NIGHTSHOT,	0 },
	{ 0, 0 }
};

const SceneControl sceneListPortrait[] = {
	{ CAMERA_CTRL_ISO,		S5K4CA_ISO_50 },
	{ CAMERA_CTRL_BRIGHTNESS,	0 },
	{ CAMERA_CTRL_SATURATION,	0 },
	{ CAMERA_CTRL_SHARPNESS,	-1 },
	{ CAMERA_CTRL_GLAMOUR,		1 },
	{ CAMERA_CTRL_AUTO_WB,		1 },
	{ CAMERA_CTRL_METERING,		S5K4CA_METERING_CENTER },
	{ 0, 0 }
};

const SceneControl sceneListLandscape[] = {
	{ CAMERA_CTRL_ISO,		S5K4CA_ISO_50 },
	{ CAMERA_CTRL_BRIGHTNESS,	0 },
	{ CAMERA_CTRL_SATURATION,	1 },
	{ CAMERA_CTRL_SHARPNESS,	1 },
	{ CAMERA_CTRL_GLAMOUR,		-1 },
	{ CAMERA_CTRL_AUTO_WB,		1 },
	{ CAMERA_CTRL_METERING,		S5K4CA_METERING_MATRIX },
	{ 0, 0 }
};

const SceneControl sceneListSports[] = {
	{ CAMERA_CTRL_ISO,		S5K4CA_ISO_SPORT },
	{ CAMERA_CTRL_BRIGHTNESS,	0 },
	{ CAMERA_CTRL_SATURATION,	0 },
	{ CAMERA_CTRL_SHARPNESS,	0 },
	{ CAMERA_CTRL_GLAMOUR,		0 },
	{ CAMERA_CTRL_AUTO_WB,		1 },
	{ CAMERA_CTRL_METERING,		S5K4CA_METERING_CENTER },
	{ 0, 0 }
};

const SceneControl sceneListPartyIndoor[] = {
	{ CAMERA_CTRL_ISO,		S5K4CA_ISO_50 },
	{ CAMERA_CTRL_BRIGHTNESS,	0 },
	{ CAMERA_CTRL_SATURATION,	1 },
	{ CAMERA_CTRL_SHARPNESS,	0 },
	{ CAMERA_CTRL_GLAMOUR,		0 },
	{ CAMERA_CTRL_AUTO_WB,		1 },
	{ CAMERA_CTRL_METERING,		S5K4CA_METERING_CENTER },
	{ 0, 0 }
};

const SceneControl sceneListBeachSnow[] = {
	{ CAMERA_CTRL_ISO,		S5K4CA_ISO_50 },
	{ CAMERA_CTRL_BRIGHTNESS,	1 },
	{ CAMERA_CTRL_SATURATION,	1 },
	{ CAMERA_CTRL_SHARPNESS,	0 },
	{ CAMERA_CTRL_GLAMOUR,		0 },
	{ CAMERA_CTRL_AUTO_WB,		1 },
	{ CAMERA_CTRL_METERING,		S5K4CA_METERING_CENTER },
	{ 0, 0 }
};

const SceneControl sceneListSunsetCandleLight[] = {
	{ CAMERA_CTRL_ISO,		S5K4CA_ISO_50 },
	{ CAMERA_CTRL_BRIGHTNESS,	0 },
	{ CAMERA_CTRL_SATURATION,	0 },
	{ CAMERA_CTRL_SHARPNESS,	0 },
	{ CAMERA_CTRL_GLAMOUR,		0 },
	{ CAMERA_CTRL_AUTO_WB,		0 },
	{ CAMERA_CTRL_WB_PRESET,	S5K4CA_WB_SUNNY },
	{ CAMERA_CTRL_METERING,		S5K4CA_METERING_CENTER },
	{ 0, 0 }
};

const SceneControl sceneListNightshot[] = {
	{ CAMERA_CTRL_ISO,		S5K4CA_ISO_NIGHT },
	{ CAMERA_CTRL_BRIGHTNESS,	0 },
	{ CAMERA_CTRL_SATURATION,	4 },
	{ CAMERA_CTRL_SHARPNESS,	0 },
	{ CAMERA_CTRL_GLAMOUR,		0 },
	{ CAMERA_CTRL_AUTO_WB,		1 },
	{ CAMERA_CTRL_METERING,		S5K4CA_METERING_CENTER },
	{ CAMERA_CTRL_NIGHTSHOT,	1 },
	{ 0, 0 }
};

const SceneControl sceneListFireworks[] = {
	{ CAMERA_CTRL_ISO,		S5K4CA_ISO_NIGHT },
	{ CAMERA_CTRL_BRIGHTNESS,	0 },
	{ CAMERA_CTRL_SATURATION,	4 },
	{ CAMERA_CTRL_SHARPNESS,	0 },
	{ CAMERA_CTRL_GLAMOUR,		0 },
	{ CAMERA_CTRL_AUTO_WB,		1 },
	{ CAMERA_CTRL_METERING,		S5K4CA_METERING_CENTER },
	{ 0, 0 }
};

const SceneControl *V4L2Camera::sceneTable[CAMERA_SCENE_NUM] = {
	sceneListNone,
	sceneListPortrait,
	sceneListLandscape,
	sceneListSports,
	sceneListPartyIndoor,
	sceneListBeachSnow,
	sceneListSunsetCandleLight,
	sceneListNightshot,
	sceneListFireworks,
};

int V4L2Camera::setSceneMode(int scene_mode)
{
	TRACE();
	DBG("(scene_mode(%d))", scene_mode);

	if (scene_mode < 0 || scene_mode >= CAMERA_SCENE_NUM) {
		ERR("invalid scene mode %d", scene_mode);
		return -EINVAL;
	}

	const SceneControl *sc = sceneTable[scene_mode];
	while (sc->control) {
		setControl(sc->control, sc->value);
		++sc;
	}

	setControl(CAMERA_CTRL_SCENE_MODE, scene_mode);

	return 0;
}

int V4L2Camera::getSceneMode(void)
{
	TRACE();
	return getControl(CAMERA_CTRL_SCENE_MODE);
}

int V4L2Camera::setGPSProcessingMethod(const char *gps_processing_method)
{
	TRACE();
#if 0
	LOGV("%s(gps_processing_method(%s))", __func__, gps_processing_method);
	memset(exifData.gps_processing_method, 0, sizeof(exifData.gps_processing_method));
	if (gps_processing_method != NULL) {
		size_t len = strlen(gps_processing_method);
		if (len > sizeof(exifData.gps_processing_method)) {
			len = sizeof(exifData.gps_processing_method);
		}
		memcpy(exifData.gps_processing_method, gps_processing_method, len);
	}
#endif
	return 0;
}

/* Recording size */

int V4L2Camera::setRecordingSize(int width, int height)
{
	TRACE();
	DBG("(width(%d), height(%d))", width, height);

	recordingWidth  = width;
	recordingHeight = height;

	return 0;
}

const char* V4L2Camera::getCameraSensorName(void)
{
	TRACE();

	if (!device)
		return NULL;

	return device->enumInput(getCameraId());
}

/* JPEG */

int V4L2Camera::setJpegThumbnailSize(int width, int height)
{
	TRACE();
	DBG("(width(%d), height(%d))", width, height);

	jpegThumbnailWidth  = width;
	jpegThumbnailHeight = height;

	return 0;
}

int V4L2Camera::getJpegThumbnailSize(int *width, int  *height)
{
	TRACE();

	if (width)
		*width   = jpegThumbnailWidth;
	if (height)
		*height  = jpegThumbnailHeight;

	return 0;
}

/*
 * EXIF
 */

void V4L2Camera::setExifFixedAttribute()
{
	char property[PROPERTY_VALUE_MAX];

	TRACE();

	property_get("ro.product.brand", property, EXIF_DEF_MAKER);
	jpegEncoder->setExifTag(EXIF_STRING_MAKER, property);

	property_get("ro.product.model", property, EXIF_DEF_MODEL);
	jpegEncoder->setExifTag(EXIF_STRING_MODEL, property);

	property_get("ro.build.id", property, EXIF_DEF_SOFTWARE);
	jpegEncoder->setExifTag(EXIF_STRING_SOFTWARE, property);

	jpegEncoder->setExifTag(EXIF_SHORT_YCBCR_POSITIONING,
			(uint16_t)EXIF_DEF_YCBCR_POSITIONING);

	jpegEncoder->setExifTag(EXIF_RATIONAL_FNUMBER,
			(uint32_t)EXIF_DEF_FNUMBER_NUM,
			(uint32_t)EXIF_DEF_FNUMBER_DEN);

	jpegEncoder->setExifTag(EXIF_SHORT_EXPOSURE_PROGRAM,
			(uint16_t)EXIF_DEF_EXPOSURE_PROGRAM);

	jpegEncoder->setExifTag(EXIF_STRING_EXIF_VERSION,
			EXIF_DEF_EXIF_VERSION);

	double fnum = (double)EXIF_DEF_FNUMBER_NUM / EXIF_DEF_FNUMBER_DEN;
	uint32_t av = APEX_FNUM_TO_APERTURE(fnum);
	jpegEncoder->setExifTag(EXIF_RATIONAL_APERTURE,
			(uint32_t)(av*EXIF_DEF_APEX_DEN),
			(uint32_t)EXIF_DEF_APEX_DEN);
	jpegEncoder->setExifTag(EXIF_RATIONAL_MAX_APERTURE,
			(uint32_t)(av*EXIF_DEF_APEX_DEN),
			(uint32_t)EXIF_DEF_APEX_DEN);

	jpegEncoder->setExifTag(EXIF_RATIONAL_FOCAL_LENGTH,
			BACK_CAMERA_FOCAL_LENGTH, EXIF_DEF_FOCAL_LEN_DEN);

	jpegEncoder->setExifTag(EXIF_STRING_USER_COMMENT,
			EXIF_DEF_USERCOMMENTS);

	jpegEncoder->setExifTag(EXIF_SHORT_COLOR_SPACE,
			(uint16_t)EXIF_DEF_COLOR_SPACE);

	jpegEncoder->setExifTag(EXIF_SHORT_EXPOSURE_MODE,
			(uint16_t)EXIF_DEF_EXPOSURE_MODE);

	unsigned char gps_version[4] = { 0x02, 0x02, 0x00, 0x00 };
	memcpy(gpsData.versionId, gps_version, sizeof(gps_version));

	jpegEncoder->setExifTag(EXIF_RATIONAL_X_RESOLUTION,
			EXIF_DEF_RESOLUTION_NUM, EXIF_DEF_RESOLUTION_DEN);
	jpegEncoder->setExifTag(EXIF_RATIONAL_Y_RESOLUTION,
			EXIF_DEF_RESOLUTION_NUM, EXIF_DEF_RESOLUTION_DEN);
	jpegEncoder->setExifTag(EXIF_SHORT_RESOLUTION_UNIT,
			(uint16_t)EXIF_DEF_RESOLUTION_UNIT);
}

void V4L2Camera::setExifChangedAttribute()
{
	int orientation = getControl(CAMERA_CTRL_EXIF_ORIENTATION);

	TRACE();

	switch (orientation) {
	default:
	case 0:
		jpegEncoder->setExifTag(EXIF_SHORT_ORIENTATION,
						(uint16_t)EXIF_ORIENTATION_UP);
		break;
	case 90:
		jpegEncoder->setExifTag(EXIF_SHORT_ORIENTATION,
						(uint16_t)EXIF_ORIENTATION_90);
		break;
	case 180:
		jpegEncoder->setExifTag(EXIF_SHORT_ORIENTATION,
						(uint16_t)EXIF_ORIENTATION_180);
		break;
	case 270:
		jpegEncoder->setExifTag(EXIF_SHORT_ORIENTATION,
						(uint16_t)EXIF_ORIENTATION_270);
		break;
	}

	time_t rawtime;
	time(&rawtime);
	struct tm *timeinfo = localtime(&rawtime);
	char date[20];
	strftime(date, 20, "%Y:%m:%d %H:%M:%S", timeinfo);
	jpegEncoder->setExifTag(EXIF_STRING_DATE_TIME, date);

	int shutterSpeed/* = device->getCtrl(V4L2_CID_CAMERA_GET_SHT_TIME)*/;
	// TODO (get real value from the sensor)
	//if (shutterSpeed < 0) {
	//	LOGE("%s: error %d getting shutterSpeed, camera_id = %d, using 100",
	//	     __func__, shutterSpeed, cameraId);
		shutterSpeed = 100;
	//}
	jpegEncoder->setExifTag(EXIF_RATIONAL_EXPOSURE_TIME,
					(uint32_t)shutterSpeed, 1000000U);

	int iso/* = device->getCtrl(V4L2_CID_CAMERA_GET_ISO)*/;
	// TODO (get real value from the sensor)
	//if (iso < 0) {
	//	LOGE("%s: error %d getting iso, camera_id = %d, using 100",
	//	     __func__, iso, cameraId);
		iso = 100;
	//}
	jpegEncoder->setExifTag(EXIF_SHORT_ISO_SPEED_RATING, (uint16_t)iso);

	// TODO: Get real values
	//double fnum = (double)exifData.fnumber.num / exifData.fnumber.den;
	double fnum = 1;
	//double exposure = (double)exifData.exposure_time.num / exifData.exposure_time.den;
	double exposure = 1;
	int32_t av, tv, bv, sv, ev;
	av = APEX_FNUM_TO_APERTURE(fnum);
	tv = APEX_EXPOSURE_TO_SHUTTER(exposure);
	sv = APEX_ISO_TO_FILMSENSITIVITY(iso);
	bv = av + tv - sv;
	ev = av + tv;
	DBG("Shutter speed=%d us, iso=%d\n", shutterSpeed, iso);
	DBG("AV=%d, TV=%d, SV=%d\n", av, tv, sv);

	jpegEncoder->setExifTag(EXIF_SRATIONAL_SHUTTER_SPEED,
			(int32_t)tv*EXIF_DEF_APEX_DEN, EXIF_DEF_APEX_DEN);
	jpegEncoder->setExifTag(EXIF_SRATIONAL_BRIGHTNESS,
			(int32_t)bv*EXIF_DEF_APEX_DEN, EXIF_DEF_APEX_DEN);
	if (getControl(CAMERA_CTRL_SCENE_MODE) == CAMERA_SCENE_BEACH_SNOW)
		jpegEncoder->setExifTag(EXIF_SRATIONAL_EXPOSURE_BIAS,
						(int32_t)1, (int32_t)1);
	else
		jpegEncoder->setExifTag(EXIF_SRATIONAL_EXPOSURE_BIAS,
						(int32_t)0, (int32_t)1);

	int metering = getControl(CAMERA_CTRL_METERING);
	switch (metering) {
	case S5K4CA_METERING_SPOT:
		jpegEncoder->setExifTag(EXIF_SHORT_METERING_MODE,
					(uint16_t)EXIF_METERING_SPOT);
		break;
	case S5K4CA_METERING_CENTER:
		jpegEncoder->setExifTag(EXIF_SHORT_METERING_MODE,
					(uint16_t)EXIF_METERING_CENTER);
		break;
	case S5K4CA_METERING_MATRIX:
	default:
		jpegEncoder->setExifTag(EXIF_SHORT_METERING_MODE,
					(uint16_t)EXIF_METERING_AVERAGE);
		break;
	}

	jpegEncoder->setExifTag(EXIF_SHORT_FLASH, (uint16_t)EXIF_DEF_FLASH);

	int auto_wb = getControl(CAMERA_CTRL_AUTO_WB);
	if (auto_wb)
		jpegEncoder->setExifTag(EXIF_SHORT_WHITE_BALANCE,
						(uint16_t)EXIF_WB_AUTO);
	else
		jpegEncoder->setExifTag(EXIF_SHORT_WHITE_BALANCE,
						(uint16_t)EXIF_WB_MANUAL);

	switch (getControl(CAMERA_CTRL_SCENE_MODE)) {
	case CAMERA_SCENE_PORTRAIT:
		jpegEncoder->setExifTag(EXIF_SHORT_SCENE_CAPTURE_TYPE,
						(uint16_t)EXIF_SCENE_PORTRAIT);
		break;
	case CAMERA_SCENE_LANDSCAPE:
		jpegEncoder->setExifTag(EXIF_SHORT_SCENE_CAPTURE_TYPE,
						(uint16_t)EXIF_SCENE_LANDSCAPE);
		break;
	case CAMERA_SCENE_NIGHTSHOT:
		jpegEncoder->setExifTag(EXIF_SHORT_SCENE_CAPTURE_TYPE,
						(uint16_t)EXIF_SCENE_NIGHT);
		break;
	default:
		jpegEncoder->setExifTag(EXIF_SHORT_SCENE_CAPTURE_TYPE,
						(uint16_t)EXIF_SCENE_STANDARD);
		break;
	}

	int gpsLatitude = getControl(CAMERA_CTRL_GPS_LATITUDE);
	int gpsLongitude = getControl(CAMERA_CTRL_GPS_LONGITUDE);
	if (gpsLatitude == 0 && gpsLongitude == 0) {
		jpegEncoder->setGpsData(0);
		return;
	}

	int gpsAltitude = getControl(CAMERA_CTRL_GPS_ALTITUDE);
	int gpsTimestamp = getControl(CAMERA_CTRL_GPS_TIMESTAMP);
	double tmp = 0;

	gpsData.latitudeRef[0] = (gpsLatitude > 0) ? 'N' : 'S';
	gpsData.longitudeRef[0] = (gpsLongitude > 0) ? 'E' : 'W';
	gpsData.altitudeRef = (gpsAltitude <= 0);

	double latitudeDeg = fabs(gpsLatitude / 10000.0);
	double latitudeMin = modf(latitudeDeg, &tmp) * 60;
	double latitudeSec = modf(latitudeMin, &tmp) * 60;
	gpsData.latitude[0].num = (uint32_t)latitudeDeg;
	gpsData.latitude[0].den = 1;
	gpsData.latitude[1].num = (uint32_t)latitudeMin;
	gpsData.latitude[1].den = 1;
	gpsData.latitude[2].num = (uint32_t)latitudeSec;
	gpsData.latitude[2].den = 1;

	double longitudeDeg = fabs(gpsLongitude / 10000.0);
	double longitudeMin = modf(longitudeDeg, &tmp) * 60;
	double longitudeSec = modf(longitudeMin, &tmp) * 60;
	gpsData.longitude[0].num = (uint32_t)latitudeDeg;
	gpsData.longitude[0].den = 1;
	gpsData.longitude[1].num = (uint32_t)latitudeMin;
	gpsData.longitude[1].den = 1;
	gpsData.longitude[2].num = (uint32_t)latitudeSec;
	gpsData.longitude[2].den = 1;

	gpsData.altitude.num = gpsAltitude;
	gpsData.altitude.den = 100;

	struct tm tm_data;
	time_t gps_time = (time_t)gpsTimestamp;
	gmtime_r(&gps_time, &tm_data);
	gpsData.timestamp[0].num = tm_data.tm_hour;
	gpsData.timestamp[0].den = 1;
	gpsData.timestamp[1].num = tm_data.tm_min;
	gpsData.timestamp[1].den = 1;
	gpsData.timestamp[2].num = tm_data.tm_sec;
	gpsData.timestamp[2].den = 1;

	snprintf(gpsData.datestamp, sizeof(gpsData.datestamp),
			"%04d:%02d:%02d", tm_data.tm_year + 1900,
			tm_data.tm_mon + 1, tm_data.tm_mday);

	jpegEncoder->setGpsData(&gpsData);
}

/* WTFs */

status_t V4L2Camera::dump(int fd, const Vector<String16> &args)
{
	const size_t SIZE = 256;
	char buffer[SIZE];
	String8 result;

	TRACE();

	snprintf(buffer, 255, "dump(%d)\n", fd);
	result.append(buffer);
	::write(fd, result.string(), result.size());

	return NO_ERROR;
}

}; /* namespace android */
