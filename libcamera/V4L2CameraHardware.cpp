/*
 * V4L2CameraHardware class implementation
 *
 * Copyright 2008, The Android Open Source Project
 * Copyright 2010, Samsung Electronics Co. LTD
 * Copyright 2012, Tomasz Figa <tomasz.figa at gmail.com>
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

#define LOG_NDEBUG 0
#define LOG_TAG "V4L2CameraHardware"
#include <utils/Log.h>

#include "V4L2CameraHardware.h"
#include <utils/threads.h>
#include <fcntl.h>
#include <sys/mman.h>

#if defined(BOARD_USES_OVERLAY)
#include <hardware/overlay.h>
#include <ui/Overlay.h>
#define CACHEABLE_BUFFERS       0x1
#define ALL_BUFFERS_FLUSHED     -66
#endif

#define BACK_CAMERA_AUTO_FOCUS_DISTANCES_STR       "0.10,1.20,Infinity"
#define BACK_CAMERA_MACRO_FOCUS_DISTANCES_STR      "0.10,0.20,Infinity"
#define BACK_CAMERA_INFINITY_FOCUS_DISTANCES_STR   "0.10,1.20,Infinity"

#define NELEM(x)	(sizeof(x)/sizeof(*x))

namespace android {

struct addrs {
	unsigned int addr_y;
	unsigned int addr_cbcr;
	unsigned int buf_index;
	unsigned int reserved;
};

struct addrs_cap {
	unsigned int addr_y;
	unsigned int width;
	unsigned int height;
};

static const int INITIAL_SKIP_FRAME = 3;
static const int EFFECT_SKIP_FRAME = 1;

V4L2CameraHardware::V4L2CameraHardware(int cameraId)
	:
	mCaptureInProgress(false),
	mParameters(),
	mPreviewHeap(0),
	mRawHeap(0),
	mRecordHeap(0),
	mV4L2Camera(NULL),
	mCameraSensorName(NULL),
	mSkipFrame(0),
#if defined(BOARD_USES_OVERLAY)
	mUseOverlay(false),
	mOverlayBufferIdx(0),
#endif
	mNotifyCb(0),
	mDataCb(0),
	mDataCbTimestamp(0),
	mCallbackCookie(0),
	mMsgEnabled(0),
	mRecordRunning(false)
{
	LOGV("%s :", __func__);
	int ret = 0;

	mV4L2Camera = V4L2Camera::getInstance();

	if (mV4L2Camera == NULL) {
		LOGE("ERR(%s):Fail on mV4L2Camera object creation", __func__);
	}

	ret = mV4L2Camera->openCamera(cameraId);
	if (ret < 0) {
		LOGE("ERR(%s):Fail on mV4L2Camera init", __func__);
	}

	if (mV4L2Camera->isOpened() == 0) {
		LOGE("ERR(%s):Fail on mV4L2Camera->isOpened()", __func__);
	}

	int recordHeapSize = sizeof(struct addrs) * kBufferCount;
	LOGV("mRecordHeap : MemoryHeapBase(recordHeapSize(%d))", recordHeapSize);
	mRecordHeap = new MemoryHeapBase(recordHeapSize);
	if (mRecordHeap->getHeapID() < 0) {
		LOGE("ERR(%s): Record heap creation fail", __func__);
		mRecordHeap.clear();
	}

	initDefaultParameters(cameraId);

	mExitAutoFocusThread = false;
	mExitPreviewThread = false;
	/* whether the PreviewThread is active in preview or stopped.  we
	 * create the thread but it is initially in stopped state.
	 */
	mPreviewRunning = false;
	mPreviewThread = new PreviewThread(this);
	mAutoFocusThread = new AutoFocusThread(this);
	mPictureThread = new PictureThread(this);
}

void V4L2CameraHardware::initDefaultParameters(int cameraId)
{
	if (mV4L2Camera == NULL) {
		LOGE("ERR(%s):mV4L2Camera object is NULL", __func__);
		return;
	}

	CameraParameters &p = mParameters;

	mCameraSensorName = mV4L2Camera->getCameraSensorName();
	LOGV("CameraSensorName: %s", mCameraSensorName);

	unsigned int preview_max_width   = 0;
	unsigned int preview_max_height  = 0;
	unsigned int snapshot_max_width  = 0;
	unsigned int snapshot_max_height = 0;

	p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
	      "1024x768,640x480,352x288,176x144");
	p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
	      "2048x1536,1600x1200,1280x960,1024x768,640x480");

	p.getSupportedPreviewSizes(mSupportedPreviewSizes);

	// If these fail, then we are using an invalid cameraId and we'll leave the
	// sizes at zero to catch the error.
	if (mV4L2Camera->getPreviewMaxSize(&preview_max_width,
					  &preview_max_height) < 0)
		LOGE("getPreviewMaxSize fail (%d / %d) \n",
		     preview_max_width, preview_max_height);
	if (mV4L2Camera->getSnapshotMaxSize(&snapshot_max_width,
					   &snapshot_max_height) < 0)
		LOGE("getSnapshotMaxSize fail (%d / %d) \n",
		     snapshot_max_width, snapshot_max_height);

	//p.setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV420SP);
	p.setPreviewFormat(CameraParameters::PIXEL_FORMAT_RGB565);
	p.setPreviewSize(preview_max_width, preview_max_height);

	p.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
	p.setPictureSize(snapshot_max_width, snapshot_max_height);
	p.set(CameraParameters::KEY_JPEG_QUALITY, "100"); // maximum quality

	p.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT,
		CameraParameters::PIXEL_FORMAT_YUV422I);

	String8 parameterString;

	parameterString = CameraParameters::PIXEL_FORMAT_RGB565;
//	parameterString.append(",");
// 	parameterString.append(CameraParameters::PIXEL_FORMAT_YUV422I);
// 	parameterString.append(",");
// 	parameterString.append(CameraParameters::PIXEL_FORMAT_YUV420P);
// 	parameterString.append(",");
// 	parameterString.append(CameraParameters::PIXEL_FORMAT_YUV420SP);
// 	parameterString.append(",");
// 	parameterString.append("yvu420");
	p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS,
						parameterString.string());

	parameterString = CameraParameters::PIXEL_FORMAT_JPEG;
	parameterString.append(",");
	parameterString.append(CameraParameters::PIXEL_FORMAT_RGB565);
	parameterString.append(",");
	parameterString.append(CameraParameters::PIXEL_FORMAT_YUV420SP);
	p.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,
						parameterString.string());

	parameterString = CameraParameters::FOCUS_MODE_AUTO;
	parameterString.append(",");
	parameterString.append(CameraParameters::FOCUS_MODE_INFINITY);
	parameterString.append(",");
	parameterString.append(CameraParameters::FOCUS_MODE_MACRO);
	p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
	      parameterString.string());

	p.set(CameraParameters::KEY_FOCUS_MODE,
	      CameraParameters::FOCUS_MODE_AUTO);
	p.set(CameraParameters::KEY_FOCUS_DISTANCES,
	      BACK_CAMERA_AUTO_FOCUS_DISTANCES_STR);
	p.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES,
	      "320x240,0x0");
	p.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, "320");
	p.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, "240");
	p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, "30");
	p.setPreviewFrameRate(30);

	parameterString = CameraParameters::EFFECT_NONE;
	parameterString.append(",");
	parameterString.append(CameraParameters::EFFECT_MONO);
	parameterString.append(",");
	parameterString.append(CameraParameters::EFFECT_NEGATIVE);
	parameterString.append(",");
	parameterString.append(CameraParameters::EFFECT_SEPIA);
	p.set(CameraParameters::KEY_SUPPORTED_EFFECTS, parameterString.string());

	parameterString = CameraParameters::SCENE_MODE_AUTO;
	parameterString.append(",");
	parameterString.append(CameraParameters::SCENE_MODE_PORTRAIT);
	parameterString.append(",");
	parameterString.append(CameraParameters::SCENE_MODE_LANDSCAPE);
	parameterString.append(",");
	parameterString.append(CameraParameters::SCENE_MODE_NIGHT);
	parameterString.append(",");
	parameterString.append(CameraParameters::SCENE_MODE_BEACH);
	parameterString.append(",");
	parameterString.append(CameraParameters::SCENE_MODE_SNOW);
	parameterString.append(",");
	parameterString.append(CameraParameters::SCENE_MODE_SUNSET);
	parameterString.append(",");
	parameterString.append(CameraParameters::SCENE_MODE_FIREWORKS);
	parameterString.append(",");
	parameterString.append(CameraParameters::SCENE_MODE_SPORTS);
	parameterString.append(",");
	parameterString.append(CameraParameters::SCENE_MODE_PARTY);
	parameterString.append(",");
	parameterString.append(CameraParameters::SCENE_MODE_CANDLELIGHT);
	p.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES,
	      parameterString.string());
	p.set(CameraParameters::KEY_SCENE_MODE,
	      CameraParameters::SCENE_MODE_AUTO);

	/* we have two ranges, 4-30fps for night mode and
	    * 15-30fps for all others
	    */
	p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(15000,30000)");
	p.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "15000,30000");

	p.set(CameraParameters::KEY_FOCAL_LENGTH, "3.43");

	parameterString = CameraParameters::WHITE_BALANCE_AUTO;
	parameterString.append(",");
	parameterString.append(CameraParameters::WHITE_BALANCE_INCANDESCENT);
	parameterString.append(",");
	parameterString.append(CameraParameters::WHITE_BALANCE_FLUORESCENT);
	parameterString.append(",");
	parameterString.append(CameraParameters::WHITE_BALANCE_DAYLIGHT);
	parameterString.append(",");
	parameterString.append(CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT);
	p.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE,
	      parameterString.string());

	p.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "100");

	p.set(CameraParameters::KEY_ROTATION, 0);
	p.set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);

	p.set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_NONE);

	p.set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE, "51.2");
	p.set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE, "39.4");

	p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, "0");
	p.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, "4");
	p.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, "-4");
	p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, "0.5");

	/* make sure mV4L2Camera has all the settings we do.  applications
	 * aren't required to call setParameters themselves (only if they
	 * want to change something.
	 */
	setParameters(p);
	mV4L2Camera->setControl(CAMERA_CTRL_ISO, S5K4CA_ISO_AUTO);
	mV4L2Camera->setControl(CAMERA_CTRL_METERING, S5K4CA_METERING_CENTER);
	mV4L2Camera->setControl(CAMERA_CTRL_CONTRAST, 0);
	mV4L2Camera->setControl(CAMERA_CTRL_SHARPNESS, 0);
	mV4L2Camera->setControl(CAMERA_CTRL_SATURATION, 0);
	mV4L2Camera->setControl(CAMERA_CTRL_FRAME_RATE, 0);
}

V4L2CameraHardware::~V4L2CameraHardware()
{
	LOGV("%s :", __func__);

	singleton.clear();
}

sp<IMemoryHeap> V4L2CameraHardware::getPreviewHeap() const
{
 	return mPreviewHeap;
}

sp<IMemoryHeap> V4L2CameraHardware::getRawHeap() const
{
 	return mRawHeap;
}

void V4L2CameraHardware::setCallbacks(notify_callback notify_cb,
				     data_callback data_cb,
				     data_callback_timestamp data_cb_timestamp,
				     void *user)
{
	mNotifyCb = notify_cb;
	mDataCb = data_cb;
	mDataCbTimestamp = data_cb_timestamp;
	mCallbackCookie = user;
}

void V4L2CameraHardware::enableMsgType(int32_t msgType)
{
	LOGV("%s : msgType = 0x%x, mMsgEnabled before = 0x%x",
	     __func__, msgType, mMsgEnabled);
	mMsgEnabled |= msgType;
	LOGV("%s : mMsgEnabled = 0x%x", __func__, mMsgEnabled);
}

void V4L2CameraHardware::disableMsgType(int32_t msgType)
{
	LOGV("%s : msgType = 0x%x, mMsgEnabled before = 0x%x",
	     __func__, msgType, mMsgEnabled);
	mMsgEnabled &= ~msgType;
	LOGV("%s : mMsgEnabled = 0x%x", __func__, mMsgEnabled);
}

bool V4L2CameraHardware::msgTypeEnabled(int32_t msgType)
{
	return (mMsgEnabled & msgType);
}

// ---------------------------------------------------------------------------
void V4L2CameraHardware::setSkipFrame(int frame)
{
	Mutex::Autolock lock(mSkipFrameLock);
	if (frame < mSkipFrame)
		return;

	mSkipFrame = frame;
}

int V4L2CameraHardware::previewThreadWrapper()
{
	LOGI("%s: starting", __func__);
	while (1) {
		mPreviewLock.lock();
		while (!mPreviewRunning) {
			LOGI("%s: calling mV4L2Camera->stopPreview() and waiting", __func__);
			mV4L2Camera->stopPreview();
			/* signal that we're stopping */
			mPreviewStoppedCondition.signal();
			mPreviewCondition.wait(mPreviewLock);
			LOGI("%s: return from wait", __func__);
		}
		mPreviewLock.unlock();

		if (mExitPreviewThread) {
			LOGI("%s: exiting", __func__);
			mV4L2Camera->stopPreview();
			return 0;
		}
		previewThread();
	}
}

#define ALIGN_TO_PAGE(x)        (((x) + 4095) & ~4095)

int V4L2CameraHardware::previewThread()
{
	int index;
	nsecs_t timestamp;
	unsigned int phyYAddr;
	unsigned int phyCAddr;
	struct addrs *addrs;

	index = mV4L2Camera->getPreview();
	if (index < 0) {
		LOGE("ERR(%s):Fail on V4L2Camera->getPreview()", __func__);
		return UNKNOWN_ERROR;
	}
	mSkipFrameLock.lock();
	if (mSkipFrame > 0) {
		mSkipFrame--;
		mSkipFrameLock.unlock();
		return NO_ERROR;
	}
	mSkipFrameLock.unlock();

	timestamp = systemTime(SYSTEM_TIME_MONOTONIC);

	sp<MemoryBase> buffer = mV4L2Camera->getBuffer(index);

/* FIXME: #if defined(BOARD_USES_OVERLAY) */
#if 0
	if (mUseOverlay) {
		int ret;
		overlay_buffer_t overlay_buffer;

		mOverlayBufferIdx ^= 1;
		memcpy(static_cast<unsigned char*>(mPreviewHeap->base()) + offset + frame_size + sizeof(phyYAddr) + sizeof(phyCAddr),
		       &mOverlayBufferIdx, sizeof(mOverlayBufferIdx));

		ret = mOverlay->queueBuffer((void*)(static_cast<unsigned char *>(mPreviewHeap->base()) + (offset + frame_size)));

		if (ret == -1 ) {
			LOGE("ERR(%s):overlay queueBuffer fail", __func__);
		} else if (ret != ALL_BUFFERS_FLUSHED) {
			ret = mOverlay->dequeueBuffer(&overlay_buffer);
			if (ret == -1) {
				LOGE("ERR(%s):overlay dequeueBuffer fail", __func__);
			}
		}
	}
#endif

	// Notify the client of a new frame.
	if (mPreviewRunning && (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME)) {
		mDataCb(CAMERA_MSG_PREVIEW_FRAME, buffer, mCallbackCookie);
	}

	/* FIXME */
#if 0
	Mutex::Autolock lock(mRecordLock);
	if (mRecordRunning == true) {
		index = mV4L2Camera->getRecordFrame();
		if (index < 0) {
			LOGE("ERR(%s):Fail on V4L2Camera->getRecord()", __func__);
			return UNKNOWN_ERROR;
		}

		phyYAddr = mV4L2Camera->getRecPhyAddrY(index);
		phyCAddr = mV4L2Camera->getRecPhyAddrC(index);

		if (phyYAddr == 0xffffffff || phyCAddr == 0xffffffff) {
			LOGE("ERR(%s):Fail on V4L2Camera getRectPhyAddr Y addr = %0x C addr = %0x", __func__, phyYAddr, phyCAddr);
			return UNKNOWN_ERROR;
		}

		addrs = (struct addrs *)mRecordHeap->base();

		sp<MemoryBase> buffer = new MemoryBase(mRecordHeap, index * sizeof(struct addrs), sizeof(struct addrs));
		addrs[index].addr_y = phyYAddr;
		addrs[index].addr_cbcr = phyCAddr;
		addrs[index].buf_index = index;

		// Notify the client of a new frame.
		if (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) {
			mDataCbTimestamp(timestamp, CAMERA_MSG_VIDEO_FRAME, buffer, mCallbackCookie);
		} else {
			mV4L2Camera->releaseRecordFrame(index);
		}
	}
#endif

	return NO_ERROR;
}

status_t V4L2CameraHardware::startPreview()
{
	int ret = 0;        //s1 [Apply factory standard]

	LOGV("%s :", __func__);

	Mutex::Autolock lock(mStateLock);
	if (mCaptureInProgress) {
		LOGE("%s : capture in progress, not allowed", __func__);
		return INVALID_OPERATION;
	}

	Mutex::Autolock previewLock(mPreviewLock);
	if (mPreviewRunning) {
		// already running
		LOGE("%s : preview thread already running", __func__);
		return INVALID_OPERATION;
	}

	setSkipFrame(INITIAL_SKIP_FRAME);

	ret  = mV4L2Camera->startPreview();
	LOGV("%s : mV4L2Camera->startPreview() returned %d", __func__, ret);

	if (ret < 0) {
		LOGE("ERR(%s):Fail on mV4L2Camera->startPreview()", __func__);
		return -1; //UNKNOWN_ERROR;
	}

        mPreviewHeap.clear();
	mPreviewHeap = mV4L2Camera->getBufferHeap();

	mPreviewRunning = true;
	mPreviewCondition.signal();

	return NO_ERROR;
}

#if defined(BOARD_USES_OVERLAY)
bool V4L2CameraHardware::useOverlay()
{
	LOGV("%s: returning true", __func__);
	return true;
}

status_t V4L2CameraHardware::setOverlay(const sp<Overlay> &overlay)
{
	LOGV("%s :", __func__);

	int overlayWidth  = 0;
	int overlayHeight = 0;
	int overlayFrameSize = 0;

	if (overlay == NULL) {
		LOGV("%s : overlay == NULL", __func__);
		goto setOverlayFail;
	}
	LOGV("%s : overlay = %p", __func__, overlay->getHandleRef());

	if (overlay->getHandleRef()== NULL && mUseOverlay == true) {
		if (mOverlay != 0)
			mOverlay->destroy();

		mOverlay = NULL;
		mUseOverlay = false;

		return NO_ERROR;
	}

	if (overlay->getStatus() != NO_ERROR) {
		LOGE("ERR(%s):overlay->getStatus() fail", __func__);
		goto setOverlayFail;
	}

	mV4L2Camera->getPreviewSize(&overlayWidth, &overlayHeight, &overlayFrameSize);

	if (overlay->setCrop(0, 0, overlayWidth, overlayHeight) != NO_ERROR) {
		LOGE("ERR(%s)::(mOverlay->setCrop(0, 0, %d, %d) fail", __func__, overlayWidth, overlayHeight);
		goto setOverlayFail;
	}

	mOverlay = overlay;
	mUseOverlay = true;

	return NO_ERROR;

setOverlayFail :
	if (mOverlay != 0)
		mOverlay->destroy();
	mOverlay = 0;

	mUseOverlay = false;

	return UNKNOWN_ERROR;
}
#endif

void V4L2CameraHardware::stopPreview()
{
	LOGV("%s :", __func__);

	/* request that the preview thread stop. */
	mPreviewLock.lock();
	if (mPreviewRunning) {
		mPreviewRunning = false;
		mPreviewCondition.signal();
		/* wait until preview thread is stopped */
		mPreviewStoppedCondition.wait(mPreviewLock);
	} else {
		LOGI("%s : preview not running, doing nothing", __func__);
	}
	mPreviewLock.unlock();
	mPreviewHeap.clear();
}

bool V4L2CameraHardware::previewEnabled()
{
	Mutex::Autolock lock(mPreviewLock);
	LOGV("%s : %d", __func__, mPreviewRunning);
	return mPreviewRunning;
}

// ---------------------------------------------------------------------------

status_t V4L2CameraHardware::startRecording()
{
	LOGV("%s :", __func__);

	Mutex::Autolock lock(mRecordLock);

	if (mRecordRunning == false) {
		if (mV4L2Camera->startRecord() < 0) {
			LOGE("ERR(%s):Fail on mV4L2Camera->startRecord()", __func__);
			return UNKNOWN_ERROR;
		}
		mRecordRunning = true;
	}
	return NO_ERROR;
}

void V4L2CameraHardware::stopRecording()
{
	LOGV("%s :", __func__);

	Mutex::Autolock lock(mRecordLock);

	if (mRecordRunning == true) {
		if (mV4L2Camera->stopRecord() < 0) {
			LOGE("ERR(%s):Fail on mV4L2Camera->stopRecord()", __func__);
			return;
		}
		mRecordRunning = false;
	}
}

bool V4L2CameraHardware::recordingEnabled()
{
	LOGV("%s :", __func__);

	return mRecordRunning;
}

void V4L2CameraHardware::releaseRecordingFrame(const sp<IMemory>& mem)
{
	ssize_t offset;
	sp<IMemoryHeap> heap = mem->getMemory(&offset, NULL);
	struct addrs *addrs = (struct addrs *)((uint8_t *)heap->base() + offset);

	mV4L2Camera->releaseRecordFrame(addrs->buf_index);
}

// ---------------------------------------------------------------------------

int V4L2CameraHardware::autoFocusThread()
{
	int count =0;
	int af_status =0 ;

	LOGV("%s : starting", __func__);

	/* block until we're told to start.  we don't want to use
	 * a restartable thread and requestExitAndWait() in cancelAutoFocus()
	 * because it would cause deadlock between our callbacks and the
	 * caller of cancelAutoFocus() which both want to grab the same lock
	 * in CameraServices layer.
	 */
	mFocusLock.lock();
	/* check early exit request */
	if (mExitAutoFocusThread) {
		mFocusLock.unlock();
		LOGV("%s : exiting on request0", __func__);
		return NO_ERROR;
	}
	mFocusCondition.wait(mFocusLock);
	/* check early exit request */
	if (mExitAutoFocusThread) {
		mFocusLock.unlock();
		LOGV("%s : exiting on request1", __func__);
		return NO_ERROR;
	}
	mFocusLock.unlock();

	LOGV("%s : calling setAutoFocus", __func__);
	if (mV4L2Camera->setAutofocus() < 0) {
		LOGE("ERR(%s):Fail on mV4L2Camera->setAutofocus()", __func__);
		return UNKNOWN_ERROR;
	}

	af_status = mV4L2Camera->getAutoFocusResult();

	if (af_status == 0x01) {
		LOGV("%s : AF Success!!", __func__);
		if (mMsgEnabled & CAMERA_MSG_FOCUS)
			mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
	} else if (af_status == 0x02) {
		LOGV("%s : AF Cancelled !!", __func__);
		if (mMsgEnabled & CAMERA_MSG_FOCUS) {
			/* CAMERA_MSG_FOCUS only takes a bool.  true for
			 * finished and false for failure.  cancel is still
			 * considered a true result.
			 */
			mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
		}
	} else {
		LOGV("%s : AF Fail !!", __func__);
		LOGV("%s : mMsgEnabled = 0x%x", __func__, mMsgEnabled);
		if (mMsgEnabled & CAMERA_MSG_FOCUS)
			mNotifyCb(CAMERA_MSG_FOCUS, false, 0, mCallbackCookie);
	}

	LOGV("%s : exiting with no error", __func__);
	return NO_ERROR;
}

status_t V4L2CameraHardware::autoFocus()
{
	LOGV("%s :", __func__);
	/* signal autoFocusThread to run once */
	mFocusCondition.signal();
	return NO_ERROR;
}

/* 2009.10.14 by icarus for added interface */
status_t V4L2CameraHardware::cancelAutoFocus()
{
	LOGV("%s :", __func__);

	if (mV4L2Camera->cancelAutofocus() < 0) {
		LOGE("ERR(%s):Fail on mV4L2Camera->cancelAutofocus()", __func__);
		return UNKNOWN_ERROR;
	}

	return NO_ERROR;
}

int V4L2CameraHardware::save_jpeg( unsigned char *real_jpeg, int jpeg_size)
{
	FILE *yuv_fp = NULL;
	char filename[100], *buffer = NULL;

	/* file create/open, note to "wb" */
	yuv_fp = fopen("/data/camera_dump.jpeg", "wb");
	if (yuv_fp == NULL) {
		LOGE("Save jpeg file open error");
		return -1;
	}

	LOGV("[BestIQ]  real_jpeg size ========>  %d\n", jpeg_size);
	buffer = (char *) malloc(jpeg_size);
	if (buffer == NULL) {
		LOGE("Save YUV] buffer alloc failed");
		if (yuv_fp)
			fclose(yuv_fp);

		return -1;
	}

	memcpy(buffer, real_jpeg, jpeg_size);

	fflush(stdout);

	fwrite(buffer, 1, jpeg_size, yuv_fp);

	fflush(yuv_fp);

	if (yuv_fp)
		fclose(yuv_fp);
	if (buffer)
		free(buffer);

	return 0;
}

void V4L2CameraHardware::save_postview(const char *fname, uint8_t *buf, uint32_t size)
{
	int nw;
	int cnt = 0;
	uint32_t written = 0;

	LOGD("opening file [%s]\n", fname);
	int fd = open(fname, O_RDWR | O_CREAT);
	if (fd < 0) {
		LOGE("failed to create file [%s]: %s", fname, strerror(errno));
		return;
	}

	LOGD("writing %d bytes to file [%s]\n", size, fname);
	while (written < size) {
		nw = ::write(fd, buf + written, size - written);
		if (nw < 0) {
			LOGE("failed to write to file %d [%s]: %s",written,fname, strerror(errno));
			break;
		}
		written += nw;
		cnt++;
	}
	LOGD("done writing %d bytes to file [%s] in %d passes\n",size, fname, cnt);
	::close(fd);
}

bool V4L2CameraHardware::scaleDownYuv422(char *srcBuf, uint32_t srcWidth, uint32_t srcHeight,
					char *dstBuf, uint32_t dstWidth, uint32_t dstHeight)
{
	int32_t step_x, step_y;
	int32_t iXsrc, iXdst;
	int32_t x, y, src_y_start_pos, dst_pos, src_pos;

	if (dstWidth % 2 != 0 || dstHeight % 2 != 0) {
		LOGE("scale_down_yuv422: invalid width, height for scaling");
		return false;
	}

	step_x = srcWidth / dstWidth;
	step_y = srcHeight / dstHeight;

	dst_pos = 0;
	for (uint32_t y = 0; y < dstHeight; y++) {
		src_y_start_pos = (y * step_y * (srcWidth * 2));

		for (uint32_t x = 0; x < dstWidth; x += 2) {
			src_pos = src_y_start_pos + (x * (step_x * 2));

			dstBuf[dst_pos++] = srcBuf[src_pos    ];
			dstBuf[dst_pos++] = srcBuf[src_pos + 1];
			dstBuf[dst_pos++] = srcBuf[src_pos + 2];
			dstBuf[dst_pos++] = srcBuf[src_pos + 3];
		}
	}

	return true;
}

bool V4L2CameraHardware::YUY2toNV21(void *srcBuf, void *dstBuf, uint32_t srcWidth, uint32_t srcHeight)
{
	int32_t        x, y, src_y_start_pos, dst_cbcr_pos, dst_pos, src_pos;
	unsigned char *srcBufPointer = (unsigned char *)srcBuf;
	unsigned char *dstBufPointer = (unsigned char *)dstBuf;

	dst_pos = 0;
	dst_cbcr_pos = srcWidth*srcHeight;
	for (uint32_t y = 0; y < srcHeight; y++) {
		src_y_start_pos = (y * (srcWidth * 2));

		for (uint32_t x = 0; x < (srcWidth * 2); x += 2) {
			src_pos = src_y_start_pos + x;

			dstBufPointer[dst_pos++] = srcBufPointer[src_pos];
		}
	}
	for (uint32_t y = 0; y < srcHeight; y += 2) {
		src_y_start_pos = (y * (srcWidth * 2));

		for (uint32_t x = 0; x < (srcWidth * 2); x += 4) {
			src_pos = src_y_start_pos + x;

			dstBufPointer[dst_cbcr_pos++] = srcBufPointer[src_pos + 3];
			dstBufPointer[dst_cbcr_pos++] = srcBufPointer[src_pos + 1];
		}
	}

	return true;
}

int V4L2CameraHardware::pictureThread()
{
	LOGV("%s :", __func__);

	int ret = NO_ERROR;

	unsigned int mSnapshotWidth, mSnapshotHeight, mSnapshotRawSize;
	unsigned int mThumbWidth, mThumbHeight, mThumbSize;
	unsigned int mJpegSize = 0;

	mV4L2Camera->getThumbnailConfig(&mThumbWidth,
						&mThumbHeight, &mThumbSize);
	mV4L2Camera->getSnapshotSize(&mSnapshotWidth,
					&mSnapshotHeight, &mSnapshotRawSize);

	sp<MemoryHeapBase> rawHeap;
	void *rawPtr = 0;
	sp<MemoryHeapBase> jpegHeap;
	void *jpegPtr = 0;

	if (mMsgEnabled & CAMERA_MSG_SHUTTER)
		mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);

	if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
		rawHeap = new MemoryHeapBase(mSnapshotRawSize);
		rawPtr = rawHeap->base();
	}

	if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
		jpegHeap = new MemoryHeapBase(mSnapshotRawSize);
		jpegPtr = jpegHeap->base();
	}

	if (mV4L2Camera->getSnapshotAndJpeg(rawPtr, jpegPtr, &mJpegSize) < 0)
		LOGE("mV4L2Camera->getSnapshotAndJpeg() failed");

	LOGV("snapshotandjpeg done\n");

	if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
		mRawHeap.clear();
		mRawHeap = rawHeap;

		sp<MemoryBase> rawBuffer = new MemoryBase(rawHeap,
							0, mSnapshotRawSize);
		mDataCb(CAMERA_MSG_RAW_IMAGE, rawBuffer, mCallbackCookie);
	}

	if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
		sp<MemoryBase> mem = new MemoryBase(jpegHeap, 0, mJpegSize);
		mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, mem, mCallbackCookie);
	}

	LOGV("%s : pictureThread end", __func__);

out:
	mStateLock.lock();
	mCaptureInProgress = false;
	mStateLock.unlock();

	return 0;
}

status_t V4L2CameraHardware::takePicture()
{
	LOGV("%s :", __func__);

	stopPreview();

	Mutex::Autolock lock(mStateLock);
	if (mCaptureInProgress) {
		LOGE("%s : capture already in progress", __func__);
		return INVALID_OPERATION;
	}

	if (mPictureThread->run("CameraPictureThread", PRIORITY_DEFAULT) != NO_ERROR) {
		LOGE("%s : couldn't run picture thread", __func__);
		return INVALID_OPERATION;
	}
	mCaptureInProgress = true;

	return NO_ERROR;
}

status_t V4L2CameraHardware::cancelPicture()
{
	mPictureThread->requestExitAndWait();

	return NO_ERROR;
}

status_t V4L2CameraHardware::dump(int fd, const Vector<String16>& args) const
{
	const size_t SIZE = 256;
	char buffer[SIZE];
	String8 result;

	if (mV4L2Camera != 0) {
		mV4L2Camera->dump(fd, args);
		mParameters.dump(fd, args);
		snprintf(buffer, 255, " preview running(%s)\n", mPreviewRunning?"true": "false");
		result.append(buffer);
	} else {
		result.append("No camera client yet.\n");
	}
	write(fd, result.string(), result.size());
	return NO_ERROR;
}

bool V4L2CameraHardware::isSupportedPreviewSize(const int width,
		const int height) const
{
	unsigned int i;

	for (i = 0; i < mSupportedPreviewSizes.size(); i++) {
		if (mSupportedPreviewSizes[i].width == width &&
		    mSupportedPreviewSizes[i].height == height)
			return true;
	}

	return false;
}

struct androidToV4l2 {
	const char *android;
	int v4l2;
};

static androidToV4l2 androidToV4l2Format[] = {
	/* Android standard */
	{ CameraParameters::PIXEL_FORMAT_YUV420SP, V4L2_PIX_FMT_NV21 },
	{ CameraParameters::PIXEL_FORMAT_YUV420P, V4L2_PIX_FMT_YUV420 },
	{ CameraParameters::PIXEL_FORMAT_YUV422I, V4L2_PIX_FMT_YUYV },
	{ CameraParameters::PIXEL_FORMAT_RGB565, V4L2_PIX_FMT_RGB565 },
	{ CameraParameters::PIXEL_FORMAT_JPEG, V4L2_PIX_FMT_YUYV },
	/* Custom */
	{ "yuv422p", V4L2_PIX_FMT_YUV422P },
	{ "uyv422i_custom", V4L2_PIX_FMT_UYVY },
	{ "uyv422i", V4L2_PIX_FMT_UYVY },
	{ "yvu420", V4L2_PIX_FMT_YVU420 },
};

static androidToV4l2 androidToV4l2WhiteBalance[] = {
	{ CameraParameters::WHITE_BALANCE_DAYLIGHT, S5K4CA_WB_SUNNY },
	{ CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT, S5K4CA_WB_CLOUDY },
	{ CameraParameters::WHITE_BALANCE_FLUORESCENT, S5K4CA_WB_FLUORESCENT },
	{ CameraParameters::WHITE_BALANCE_INCANDESCENT, S5K4CA_WB_TUNGSTEN },
};

static androidToV4l2 androidToV4l2SceneMode[] = {
	{ CameraParameters::SCENE_MODE_AUTO, CAMERA_SCENE_NONE },
	{ CameraParameters::SCENE_MODE_PORTRAIT, CAMERA_SCENE_PORTRAIT },
	{ CameraParameters::SCENE_MODE_LANDSCAPE, CAMERA_SCENE_LANDSCAPE },
	{ CameraParameters::SCENE_MODE_SPORTS, CAMERA_SCENE_SPORTS },
	{ CameraParameters::SCENE_MODE_PARTY, CAMERA_SCENE_PARTY_INDOOR },
	{ CameraParameters::SCENE_MODE_BEACH, CAMERA_SCENE_BEACH_SNOW },
	{ CameraParameters::SCENE_MODE_SNOW, CAMERA_SCENE_BEACH_SNOW },
	{ CameraParameters::SCENE_MODE_SUNSET, CAMERA_SCENE_SUNSET_CANDLE },
	{ CameraParameters::SCENE_MODE_NIGHT, CAMERA_SCENE_NIGHTSHOT },
	{ CameraParameters::SCENE_MODE_FIREWORKS, CAMERA_SCENE_FIREWORKS },
	{ CameraParameters::SCENE_MODE_CANDLELIGHT, CAMERA_SCENE_SUNSET_CANDLE },
};

int V4L2CameraHardware::setPreviewFormat(int width, int height,
							const char *format)
{
	int new_preview_format = -1;
	int ret;

// 	for (unsigned i = 0; i < NELEM(androidToV4l2Format); ++i) {
// 		ret = strcmp(androidToV4l2Format[i].android, format);
// 		if (!ret) {
// 			new_preview_format = androidToV4l2Format[i].v4l2;
// 			break;
// 		}
// 	}

	if (new_preview_format == -1) {
		new_preview_format = V4L2_PIX_FMT_RGB565;
		LOGW("%s: Unsupported preview format %s, defaulting to RGB565",
					__func__, format);
	}

	if (mV4L2Camera->setSnapshotPixelFormat(new_preview_format) < 0) {
		LOGE("ERR(%s):Fail on mV4L2Camera->setSnapshotPixelFormat(format(%d))",
						__func__, new_preview_format);
		return UNKNOWN_ERROR;
	}

	ret = mV4L2Camera->setPreviewSize(width, height, new_preview_format);
	if (ret < 0) {
		LOGE("ERR(%s):Fail on mV4L2Camera->setPreviewSize(width(%d), height(%d), format(%d))",
				__func__, width, height,
				new_preview_format);
		return UNKNOWN_ERROR;
	}

	mParameters.setPreviewSize(width, height);
	mParameters.setPreviewFormat(format);

#if defined(BOARD_USES_OVERLAY)
	if (mUseOverlay == true && mOverlay != 0) {
		ret = mOverlay->setCrop(0, 0,
					width, height);
		if (ret != NO_ERROR)
			LOGE("ERR(%s)::(mOverlay->setCrop(0, 0, %d, %d) fail",
				__func__, width, height);
	}
#endif

	return NO_ERROR;
}

int V4L2CameraHardware::setPictureFormat(const char *format)
{
	int new_picture_format = -1;
	int ret;

	for (unsigned i = 0; i < NELEM(androidToV4l2Format); ++i) {
		ret = strcmp(androidToV4l2Format[i].android, format);
		if (!ret) {
			new_picture_format = androidToV4l2Format[i].v4l2;
			break;
		}
	}

	if (new_picture_format == -1) {
		new_picture_format = V4L2_PIX_FMT_RGB565;
		LOGW("%s: Unsupported preview format %s, defaulting to RGB565",
					__func__, format);
	}

	if (mV4L2Camera->setSnapshotPixelFormat(new_picture_format) < 0) {
		LOGE("ERR(%s):Fail on mV4L2Camera->setSnapshotPixelFormat(format(%d))",
						__func__, new_picture_format);
		return UNKNOWN_ERROR;
	}

	mParameters.setPictureFormat(format);

	return NO_ERROR;
}

status_t V4L2CameraHardware::setParameters(const CameraParameters& params)
{
	LOGV("%s :", __func__);
	int err;
	status_t ret = NO_ERROR;

	/* if someone calls us while picture thread is running, it could screw
	 * up the sensor quite a bit so return error.  we can't wait because
	 * that would cause deadlock with the callbacks
	 */
	mStateLock.lock();
	if (mCaptureInProgress) {
		mStateLock.unlock();
		LOGE("%s : capture in progress, not allowed", __func__);
		return UNKNOWN_ERROR;
	}
	mStateLock.unlock();

	/* preview size and format */
	int new_preview_width  = 0;
	int new_preview_height = 0;
	params.getPreviewSize(&new_preview_width, &new_preview_height);
	const char *new_str_preview_format = params.getPreviewFormat();

	LOGV("%s : new_preview_width x new_preview_height = %dx%d, format = %s",
			__func__, new_preview_width, new_preview_height,
			new_str_preview_format);

	if (new_preview_width > 0 && new_preview_height > 0
	    && new_str_preview_format != NULL
	    && isSupportedPreviewSize(new_preview_width, new_preview_height)) {
		err = setPreviewFormat(new_preview_width, new_preview_height,
							new_str_preview_format);
		if (err != NO_ERROR)
			ret = err;
	} else {
		LOGE("%s: Invalid preview size(%dx%d)",
		     __func__, new_preview_width, new_preview_height);

		ret = INVALID_OPERATION;
	}

	/* picture size */
	int new_picture_width  = 0;
	int new_picture_height = 0;

	params.getPictureSize(&new_picture_width, &new_picture_height);
	LOGV("%s : new_picture_width x new_picture_height = %dx%d",
			__func__, new_picture_width, new_picture_height);
	if (0 < new_picture_width && 0 < new_picture_height) {
		err = mV4L2Camera->setSnapshotSize(new_picture_width,
							new_picture_height);
		if (err < 0) {
			LOGE("ERR(%s):Fail on mV4L2Camera->setSnapshotSize(width(%d), height(%d))",
			     __func__, new_picture_width, new_picture_height);
			ret = UNKNOWN_ERROR;
		} else {
			mParameters.setPictureSize(new_picture_width, new_picture_height);
		}
	}

	/* picture format */
	const char *new_str_picture_format = params.getPictureFormat();
	LOGV("%s : new_str_picture_format %s", __func__, new_str_picture_format);
	if (new_str_picture_format != NULL)
		setPictureFormat(new_str_picture_format);

	/* JPEG image quality */
	int new_jpeg_quality = params.getInt(CameraParameters::KEY_JPEG_QUALITY);
	LOGV("%s : new_jpeg_quality %d", __func__, new_jpeg_quality);
	/* we ignore bad values */
	if (new_jpeg_quality >=1 && new_jpeg_quality <= 100) {
		if (mV4L2Camera->setControl(CAMERA_CTRL_JPEG_QUALITY, new_jpeg_quality) < 0) {
			LOGE("ERR(%s):Fail on mV4L2Camera->setJpegQuality(quality(%d))",
						__func__, new_jpeg_quality);
			ret = UNKNOWN_ERROR;
		} else {
			mParameters.set(CameraParameters::KEY_JPEG_QUALITY,
							new_jpeg_quality);
		}
	}

	/* JPEG thumbnail size */
	int new_jpeg_thumbnail_width = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
	int new_jpeg_thumbnail_height = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
	if (0 <= new_jpeg_thumbnail_width && 0 <= new_jpeg_thumbnail_height) {
		err = mV4L2Camera->setJpegThumbnailSize(new_jpeg_thumbnail_width,
						new_jpeg_thumbnail_height);
		if (err < 0) {
			LOGE("ERR(%s):Fail on mV4L2Camera->setJpegThumbnailSize(width(%d), height(%d))",
					__func__, new_jpeg_thumbnail_width,
					new_jpeg_thumbnail_height);
			ret = UNKNOWN_ERROR;
		} else {
			mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH,
						new_jpeg_thumbnail_width);
			mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT,
						new_jpeg_thumbnail_height);
		}
	}

	/* frame rate */
	int new_frame_rate = params.getPreviewFrameRate();
	/*
	 * FIXME: ignore any fps request, we're determine fps automatically based
	 * on scene mode.  don't return an error because it causes CTS failure.
	 */
	if (new_frame_rate != mParameters.getPreviewFrameRate()) {
		LOGW("WARN(%s): request for preview frame %d not allowed, != %d\n",
		     __func__, new_frame_rate, mParameters.getPreviewFrameRate());
	}

	/* screen orientation */
	int new_rotation = params.getInt(CameraParameters::KEY_ROTATION);
	LOGV("%s : new_rotation %d", __func__, new_rotation);
	if (0 <= new_rotation) {
		LOGV("%s : set orientation:%d\n", __func__, new_rotation);
		if (mV4L2Camera->setControl(CAMERA_CTRL_EXIF_ORIENTATION, new_rotation) < 0) {
			LOGE("ERR(%s):Fail on mV4L2Camera->setExifOrientationInfo(%d)",
							__func__, new_rotation);
			ret = UNKNOWN_ERROR;
		} else {
			mParameters.set(CameraParameters::KEY_ROTATION, new_rotation);
		}
	}

	/* brightness */
	int new_exposure_compensation = params.getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
	int max_exposure_compensation = params.getInt(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION);
	int min_exposure_compensation = params.getInt(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION);
	LOGV("%s : new_exposure_compensation %d", __func__, new_exposure_compensation);
	if ((min_exposure_compensation <= new_exposure_compensation)
	    && (max_exposure_compensation >= new_exposure_compensation)) {
		if (mV4L2Camera->setControl(CAMERA_CTRL_BRIGHTNESS, new_exposure_compensation) < 0) {
			LOGE("ERR(%s):Fail on mV4L2Camera->setBrightness(brightness(%d))",
					__func__, new_exposure_compensation);
			ret = UNKNOWN_ERROR;
		} else {
			mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION,
						new_exposure_compensation);
		}
	}

	/* whitebalance */
	const char *new_white_str = params.get(CameraParameters::KEY_WHITE_BALANCE);
	LOGV("%s : new_white_str %s", __func__, new_white_str);
	if (new_white_str != NULL) {
		int new_white = -1;
		int auto_wb;

		auto_wb = !strcmp(new_white_str, CameraParameters::WHITE_BALANCE_AUTO);

		for (unsigned i = 0; i < NELEM(androidToV4l2WhiteBalance); ++i) {
			err = strcmp(new_white_str,
					androidToV4l2WhiteBalance[i].android);
			if (!err) {
				new_white = androidToV4l2WhiteBalance[i].v4l2;
				break;
			}
		}

		if (mV4L2Camera->setControl(CAMERA_CTRL_AUTO_WB, auto_wb) < 0) {
			LOGE("ERR(%s):Fail on mV4L2Camera->setWhiteBalance(white(%d))",
						__func__, new_white);
			ret = UNKNOWN_ERROR;
		}

		if (!auto_wb && new_white < 0) {
			LOGE("ERR(%s):Invalid white balance(%s)",
						__func__, new_white_str);
			ret = UNKNOWN_ERROR;
		} else if (!auto_wb) {
			if (mV4L2Camera->setControl(CAMERA_CTRL_WB_PRESET, new_white) < 0) {
				LOGE("ERR(%s):Fail on mV4L2Camera->setWhiteBalance(white(%d))",
							__func__, new_white);
				ret = UNKNOWN_ERROR;
			} else {
				mParameters.set(CameraParameters::KEY_WHITE_BALANCE,
								new_white_str);
			}
		}
	}

	/* scene mode */
	const char *new_scene_mode_str = params.get(CameraParameters::KEY_SCENE_MODE);
	const char *current_scene_mode_str = mParameters.get(CameraParameters::KEY_SCENE_MODE);

	/* fps range */
	int new_min_fps = 0;
	int new_max_fps = 0;
	int current_min_fps, current_max_fps;
	params.getPreviewFpsRange(&new_min_fps, &new_max_fps);
	mParameters.getPreviewFpsRange(&current_min_fps, &current_max_fps);
	/* our fps range is determined by the sensor, reject any request
	 * that isn't exactly what we're already at.
	 * but the check is performed when requesting only changing fps range
	 */
	if (new_scene_mode_str && current_scene_mode_str) {
		if (!strcmp(new_scene_mode_str, current_scene_mode_str)) {
			if ((new_min_fps != current_min_fps) || (new_max_fps != current_max_fps)) {
				LOGW("%s : requested new_min_fps = %d, new_max_fps = %d not allowed",
				     __func__, new_min_fps, new_max_fps);
				LOGE("%s : current_min_fps = %d, current_max_fps = %d",
				     __func__, current_min_fps, current_max_fps);
				ret = UNKNOWN_ERROR;
			}
		}
	} else {
		/* Check basic validation if scene mode is different */
		if ((new_min_fps > new_max_fps) ||
		    (new_min_fps < 0) || (new_max_fps < 0))
			ret = UNKNOWN_ERROR;
	}

	if (new_scene_mode_str != NULL) {
		int  new_scene_mode = -1;

		const char *new_flash_mode_str = params.get(CameraParameters::KEY_FLASH_MODE);
		const char *new_focus_mode_str;

		new_focus_mode_str = params.get(CameraParameters::KEY_FOCUS_MODE);
		// fps range is (15000,30000) by default.
		mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(15000,30000)");
		mParameters.set(CameraParameters::KEY_PREVIEW_FPS_RANGE,
				"15000,30000");

		for (unsigned i = 0; i < NELEM(androidToV4l2SceneMode); ++i) {
			err = strcmp(new_scene_mode_str,
					androidToV4l2SceneMode[i].android);
			if (!err) {
				new_scene_mode = androidToV4l2SceneMode[i].v4l2;
				break;
			}
		}

		if (new_scene_mode < 0) {
			LOGE("%s::unmatched scene_mode(%s)",
				__func__, new_scene_mode_str); //action, night-portrait, theatre, steadyphoto
			ret = UNKNOWN_ERROR;
		}

		new_focus_mode_str = CameraParameters::FOCUS_MODE_AUTO;
		new_flash_mode_str = CameraParameters::FLASH_MODE_OFF;

		/* Special settings */
		switch (new_scene_mode) {
		case CAMERA_SCENE_NONE:
			new_focus_mode_str = 0;
			new_flash_mode_str = 0;
			break;
		case CAMERA_SCENE_PORTRAIT:
			new_flash_mode_str = CameraParameters::FLASH_MODE_AUTO;
			break;
		case CAMERA_SCENE_PARTY_INDOOR:
			new_flash_mode_str = CameraParameters::FLASH_MODE_AUTO;
			break;
		case CAMERA_SCENE_NIGHTSHOT:
			mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE,
							"(4000,30000)");
			mParameters.set(CameraParameters::KEY_PREVIEW_FPS_RANGE,
							"4000,30000");
			break;
		}

		// focus mode
		if (new_focus_mode_str != NULL) {
			int new_focus_mode;
			bool new_focus_mode_valid = true;

			if (!strcmp(new_focus_mode_str,
				    CameraParameters::FOCUS_MODE_AUTO)) {
				new_focus_mode = 0;
				mParameters.set(CameraParameters::KEY_FOCUS_DISTANCES,
						BACK_CAMERA_AUTO_FOCUS_DISTANCES_STR);
			} else if (!strcmp(new_focus_mode_str,
					   CameraParameters::FOCUS_MODE_MACRO)) {
				new_focus_mode = 1;
				mParameters.set(CameraParameters::KEY_FOCUS_DISTANCES,
						BACK_CAMERA_MACRO_FOCUS_DISTANCES_STR);
			} else if (!strcmp(new_focus_mode_str,
					   CameraParameters::FOCUS_MODE_INFINITY)) {
				new_focus_mode = -1;
				mParameters.set(CameraParameters::KEY_FOCUS_DISTANCES,
						BACK_CAMERA_INFINITY_FOCUS_DISTANCES_STR);
			} else {
				LOGE("%s::unmatched focus_mode(%s)", __func__, new_focus_mode_str);
				ret = UNKNOWN_ERROR;
				new_focus_mode_valid = false;
			}

			if (new_focus_mode_valid) {
				if (mV4L2Camera->setControl(CAMERA_CTRL_FOCUS_MODE, new_focus_mode) < 0) {
					LOGE("%s::mV4L2Camera->setFocusMode(%d) fail", __func__, new_focus_mode);
					ret = UNKNOWN_ERROR;
				} else {
					mParameters.set(CameraParameters::KEY_FOCUS_MODE, new_focus_mode_str);
				}
			}
		}

		//  scene..
		if (0 <= new_scene_mode) {
			if (mV4L2Camera->setSceneMode(new_scene_mode) < 0) {
				LOGE("%s::mV4L2Camera->setSceneMode(%d) fail", __func__, new_scene_mode);
				ret = UNKNOWN_ERROR;
			} else {
				mParameters.set(CameraParameters::KEY_SCENE_MODE, new_scene_mode_str);
			}
		}
	}

	// ---------------------------------------------------------------------------

	// image effect
	const char *new_image_effect_str = params.get(CameraParameters::KEY_EFFECT);
	if (new_image_effect_str != NULL) {

		int  new_image_effect = -1;

		if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_NONE))
			new_image_effect = V4L2_COLORFX_NONE;
		else if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_MONO))
			new_image_effect = V4L2_COLORFX_BW;
		else if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_SEPIA))
			new_image_effect = V4L2_COLORFX_SEPIA;
		else if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_AQUA))
			new_image_effect = V4L2_COLORFX_SKY_BLUE;
		else if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_NEGATIVE))
			new_image_effect = V4L2_COLORFX_NEGATIVE;
		else {
			//posterize, whiteboard, blackboard, solarize
			LOGE("ERR(%s):Invalid effect(%s)", __func__, new_image_effect_str);
			ret = UNKNOWN_ERROR;
		}

		if (new_image_effect >= 0) {
			if (mV4L2Camera->setControl(CAMERA_CTRL_COLORFX, new_image_effect) < 0) {
				LOGE("ERR(%s):Fail on mV4L2Camera->setImageEffect(effect(%d))", __func__, new_image_effect);
				ret = UNKNOWN_ERROR;
			} else {
				const char *old_image_effect_str = mParameters.get(CameraParameters::KEY_EFFECT);

				if (old_image_effect_str) {
					if (strcmp(old_image_effect_str, new_image_effect_str)) {
						setSkipFrame(EFFECT_SKIP_FRAME);
					}
				}

				mParameters.set(CameraParameters::KEY_EFFECT, new_image_effect_str);
			}
		}
	}

	// gps latitude FIXME
// 	const char *new_gps_latitude_str = params.get(CameraParameters::KEY_GPS_LATITUDE);
// 	if (mV4L2Camera->setGPSLatitude(new_gps_latitude_str) < 0) {
// 		LOGE("%s::mV4L2Camera->setGPSLatitude(%s) fail", __func__, new_gps_latitude_str);
// 		ret = UNKNOWN_ERROR;
// 	} else {
// 		if (new_gps_latitude_str) {
// 			mParameters.set(CameraParameters::KEY_GPS_LATITUDE, new_gps_latitude_str);
// 		} else {
// 			mParameters.remove(CameraParameters::KEY_GPS_LATITUDE);
// 		}
// 	}

	// gps longitude FIXME
// 	const char *new_gps_longitude_str = params.get(CameraParameters::KEY_GPS_LONGITUDE);
//
// 	if (mV4L2Camera->setGPSLongitude(new_gps_longitude_str) < 0) {
// 		LOGE("%s::mV4L2Camera->setGPSLongitude(%s) fail", __func__, new_gps_longitude_str);
// 		ret = UNKNOWN_ERROR;
// 	} else {
// 		if (new_gps_longitude_str) {
// 			mParameters.set(CameraParameters::KEY_GPS_LONGITUDE, new_gps_longitude_str);
// 		} else {
// 			mParameters.remove(CameraParameters::KEY_GPS_LONGITUDE);
// 		}
// 	}

	// gps altitude FIXME
// 	const char *new_gps_altitude_str = params.get(CameraParameters::KEY_GPS_ALTITUDE);
//
// 	if (mV4L2Camera->setGPSAltitude(new_gps_altitude_str) < 0) {
// 		LOGE("%s::mV4L2Camera->setGPSAltitude(%s) fail", __func__, new_gps_altitude_str);
// 		ret = UNKNOWN_ERROR;
// 	} else {
// 		if (new_gps_altitude_str) {
// 			mParameters.set(CameraParameters::KEY_GPS_ALTITUDE, new_gps_altitude_str);
// 		} else {
// 			mParameters.remove(CameraParameters::KEY_GPS_ALTITUDE);
// 		}
// 	}

	// gps timestamp FIXME
// 	const char *new_gps_timestamp_str = params.get(CameraParameters::KEY_GPS_TIMESTAMP);
//
// 	if (mV4L2Camera->setGPSTimeStamp(new_gps_timestamp_str) < 0) {
// 		LOGE("%s::mV4L2Camera->setGPSTimeStamp(%s) fail", __func__, new_gps_timestamp_str);
// 		ret = UNKNOWN_ERROR;
// 	} else {
// 		if (new_gps_timestamp_str) {
// 			mParameters.set(CameraParameters::KEY_GPS_TIMESTAMP, new_gps_timestamp_str);
// 		} else {
// 			mParameters.remove(CameraParameters::KEY_GPS_TIMESTAMP);
// 		}
// 	}

	// gps processing method
	const char *new_gps_processing_method_str = params.get(CameraParameters::KEY_GPS_PROCESSING_METHOD);

	if (mV4L2Camera->setGPSProcessingMethod(new_gps_processing_method_str) < 0) {
		LOGE("%s::mV4L2Camera->setGPSProcessingMethod(%s) fail", __func__, new_gps_processing_method_str);
		ret = UNKNOWN_ERROR;
	} else {
		if (new_gps_processing_method_str) {
			mParameters.set(CameraParameters::KEY_GPS_PROCESSING_METHOD, new_gps_processing_method_str);
		} else {
			mParameters.remove(CameraParameters::KEY_GPS_PROCESSING_METHOD);
		}
	}

	LOGV("%s return ret = %d", __func__, ret);

	return ret;
}

CameraParameters V4L2CameraHardware::getParameters() const
{
	LOGV("%s :", __func__);
	return mParameters;
}

status_t V4L2CameraHardware::sendCommand(int32_t command, int32_t arg1, int32_t arg2)
{
	return BAD_VALUE;
}

void V4L2CameraHardware::release()
{
	LOGV("%s :", __func__);

	/* shut down any threads we have that might be running.  do it here
	 * instead of the destructor.  we're guaranteed to be on another thread
	 * than the ones below.  if we used the destructor, since the threads
	 * have a reference to this object, we could wind up trying to wait
	 * for ourself to exit, which is a deadlock.
	 */
	if (mPreviewThread != NULL) {
		/* this thread is normally already in it's threadLoop but blocked
		 * on the condition variable or running.  signal it so it wakes
		 * up and can exit.
		 */
		mPreviewThread->requestExit();
		mExitPreviewThread = true;
		mPreviewRunning = true; /* let it run so it can exit */
		mPreviewCondition.signal();
		mPreviewThread->requestExitAndWait();
		mPreviewThread.clear();
	}
	if (mAutoFocusThread != NULL) {
		/* this thread is normally already in it's threadLoop but blocked
		 * on the condition variable.  signal it so it wakes up and can exit.
		 */
		mFocusLock.lock();
		mAutoFocusThread->requestExit();
		mExitAutoFocusThread = true;
		mFocusCondition.signal();
		mFocusLock.unlock();
		mAutoFocusThread->requestExitAndWait();
		mAutoFocusThread.clear();
	}
	if (mPictureThread != NULL) {
		mPictureThread->requestExitAndWait();
		mPictureThread.clear();
	}

 	mRawHeap.clear();
	mRecordHeap.clear();
 	mPreviewHeap.clear();

#if defined(BOARD_USES_OVERLAY)
	if (mUseOverlay) {
		mOverlay->destroy();
		mUseOverlay = false;
		mOverlay = NULL;
	}
#endif

	/* close after all the heaps are cleared since those
	 * could have dup'd our file descriptor.
	 */

	mV4L2Camera->closeCamera();
	mV4L2Camera = NULL;
}

wp<CameraHardwareInterface> V4L2CameraHardware::singleton;

sp<CameraHardwareInterface> V4L2CameraHardware::createInstance(int cameraId)
{
	LOGV("%s :", __func__);
	if (singleton != 0) {
		sp<CameraHardwareInterface> hardware = singleton.promote();
		if (hardware != 0) {
			return hardware;
		}
	}
	sp<CameraHardwareInterface> hardware(new V4L2CameraHardware(cameraId));
	singleton = hardware;
	return hardware;
}

static CameraInfo sCameraInfo[] = {
	{
		CAMERA_FACING_BACK,
		90,  /* orientation */
	},
};

extern "C" int HAL_getNumberOfCameras()
{
	return sizeof(sCameraInfo) / sizeof(sCameraInfo[0]);
}

extern "C" void HAL_getCameraInfo(int cameraId, struct CameraInfo *cameraInfo)
{
	memcpy(cameraInfo, &sCameraInfo[cameraId], sizeof(CameraInfo));
}

extern "C" sp<CameraHardwareInterface> HAL_openCameraHardware(int cameraId)
{
	return V4L2CameraHardware::createInstance(cameraId);
}

}; /* namespace android */
