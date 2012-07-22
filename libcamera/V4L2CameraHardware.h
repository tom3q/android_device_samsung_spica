/*
 * V4L2CameraHardware class prototype
 *
 * Copyright 2008, The Android Open Source Project
 * Copyright 2010, Samsung Electronics Co. LTD
 * Copyright 2012, Tomasz Figa <tomasz.figa at gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _V4L2_CAMERA_HARDWARE_H_
#define _V4L2_CAMERA_HARDWARE_H_

#include "V4L2Camera.h"
#include <utils/threads.h>
#include <camera/CameraHardwareInterface.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <utils/threads.h>

namespace android {

class V4L2CameraHardware : public CameraHardwareInterface {
	/*
	 * Constants
	 */
	static const int kBufferCount = MAX_BUFFERS;
	static const int kBufferCountForRecord = MAX_BUFFERS;

	/*
	 * Static attributes
	 */
	static wp<CameraHardwareInterface> singleton;

	/*
	 * Internal classes
	 */
	class PreviewThread : public Thread {
		V4L2CameraHardware *mHardware;
	public:
		PreviewThread(V4L2CameraHardware *hw):
			Thread(false),
			mHardware(hw)
		{}

		virtual void onFirstRef()
		{
			run("CameraPreviewThread", PRIORITY_URGENT_DISPLAY);
		}

		virtual bool threadLoop()
		{
			mHardware->previewThreadWrapper();
			return false;
		}
	};

	class PictureThread : public Thread {
		V4L2CameraHardware *mHardware;
	public:
		PictureThread(V4L2CameraHardware *hw):
			Thread(false),
			mHardware(hw)
		{}

		virtual bool threadLoop()
		{
			mHardware->pictureThread();
			mHardware->mV4L2Camera->endSnapshot();
			return false;
		}
	};

	class AutoFocusThread : public Thread {
		V4L2CameraHardware *mHardware;
	public:
		AutoFocusThread(V4L2CameraHardware *hw):
			Thread(false),
			mHardware(hw)
		{}

		virtual void onFirstRef()
		{
			run("CameraAutoFocusThread", PRIORITY_DEFAULT);
		}

		virtual bool threadLoop()
		{
			mHardware->autoFocusThread();
			return true;
		}
	};

	/*
	 * Attributes
	 */
	sp<PreviewThread> mPreviewThread;
	sp<AutoFocusThread> mAutoFocusThread;
	sp<PictureThread> mPictureThread;

	bool mCaptureInProgress;

	/* used by auto focus thread to block until it's told to run */
	mutable Mutex mFocusLock;
	mutable Condition mFocusCondition;
	bool mExitAutoFocusThread;

	/* used by preview thread to block until it's told to run */
	mutable Mutex mPreviewLock;
	mutable Condition mPreviewCondition;
	mutable Condition mPreviewStoppedCondition;
	bool mPreviewRunning;
	bool mExitPreviewThread;

	/* used to guard threading state */
	mutable Mutex mStateLock;

	CameraParameters mParameters;

	sp<MemoryHeapPmem> mPreviewPmemHeap;
	sp<MemoryHeapBase> mPreviewHeap;
	sp<MemoryHeapBase> mRawHeap;
	sp<MemoryHeapBase> mRecordHeap;
	sp<MemoryHeapBase> mJpegHeap;
	sp<MemoryBase> mBuffers[kBufferCount];
	sp<MemoryBase> mRecordBuffers[kBufferCountForRecord];

	V4L2Camera *mV4L2Camera;
	const char *mCameraSensorName;

	mutable Mutex mSkipFrameLock;
	int mSkipFrame;

#if defined(BOARD_USES_OVERLAY)
	sp<Overlay> mOverlay;
	bool mUseOverlay;
	int mOverlayBufferIdx;
#endif

	notify_callback mNotifyCb;
	data_callback mDataCb;
	data_callback_timestamp mDataCbTimestamp;
	void *mCallbackCookie;

	int32_t mMsgEnabled;

	bool mRecordRunning;
	mutable Mutex mRecordLock;

	Vector<Size> mSupportedPreviewSizes;

	/*
	 * Methods
	 */
	V4L2CameraHardware(int cameraId);
	virtual ~V4L2CameraHardware();

	void initDefaultParameters(int cameraId);
	void initHeapLocked();

	int previewThread();
	int previewThreadWrapper();
	int autoFocusThread();
	int pictureThread();

	int save_jpeg(unsigned char *real_jpeg, int jpeg_size);
	void save_postview(const char *fname, uint8_t *buf,
				 uint32_t size);

	bool YUY2toNV21(void *srcBuf, void *dstBuf, uint32_t srcWidth, uint32_t srcHeight);
	bool scaleDownYuv422(char *srcBuf, uint32_t srcWidth,
				 uint32_t srcHight, char *dstBuf,
				 uint32_t dstWidth, uint32_t dstHight);

	void setSkipFrame(int frame);
	bool isSupportedPreviewSize(const int width,
					 const int height) const;

	int setPreviewFormat(int width, int height, const char *format);
	int setPictureFormat(const char *format);

public:
	/*
	 * Static methods
	 */
	static sp<CameraHardwareInterface> createInstance(int cameraId);

	/*
	 * CameraHardware interface
	 */
	virtual sp<IMemoryHeap> getPreviewHeap() const;
	virtual sp<IMemoryHeap> getRawHeap() const;

	virtual void setCallbacks(notify_callback notify_cb,
				data_callback data_cb,
				data_callback_timestamp data_cb_timestamp,
				void *user);
	virtual void enableMsgType(int32_t msgType);
	virtual void disableMsgType(int32_t msgType);
	virtual bool msgTypeEnabled(int32_t msgType);

	virtual status_t startPreview();
#if defined(BOARD_USES_OVERLAY)
	virtual bool useOverlay();
	virtual status_t setOverlay(const sp<Overlay> &overlay);
#endif
	virtual void stopPreview();
	virtual bool previewEnabled();

	virtual status_t startRecording();
	virtual void stopRecording();
	virtual bool recordingEnabled();
	virtual void releaseRecordingFrame(const sp<IMemory> &mem);

	virtual status_t autoFocus();
	virtual status_t cancelAutoFocus();
	virtual status_t takePicture();
	virtual status_t cancelPicture();
	virtual status_t dump(int fd, const Vector<String16> &args) const;
	virtual status_t setParameters(const CameraParameters& params);
	virtual CameraParameters getParameters() const;
	virtual status_t sendCommand(int32_t command, int32_t arg1, int32_t arg2);
	virtual void release();
};

}; /* namespace android */

#endif /* _V4L2_CAMERA_HARDWARE_H_ */
