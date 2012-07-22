/*
 * Generic V4L2 device implementation
 *
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

#ifndef _V4L2DEVICE_H_
#define _V4L2DEVICE_H_

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <sys/mman.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/stat.h>

#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <binder/MemoryHeapPmem.h>

#include <linux/videodev2.h>

using namespace android;

/*
 * Data structures
 */

class V4L2Buffer {
	void *start;
	size_t length;
	size_t used;

	friend class V4L2Allocation;
	friend class V4L2Device;
public:
	inline void *getAddress(void) { return start; }
	inline const void *getAddress(void) const { return start; }
	inline size_t getLength(void) const { return length; }
	inline size_t getUsed(void) const { return used; }
};

/*
 * Utility functions
 */

#define ALIGN_TO_PAGE(x)        (((x) + 4095) & ~4095)

static inline int get_pixel_depth(unsigned int fmt)
{
	int depth = 0;

	switch (fmt) {
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_YUV420:
		depth = 12;
		break;

	case V4L2_PIX_FMT_RGB565:
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

/*
 * Generic V4L2 device implementation
 */

#define MAX_BUFFERS	(8)

class V4L2Allocation : public RefBase {
	sp<MemoryHeapBase> heap;
	sp<MemoryHeapPmem> pmemHeap;
	V4L2Buffer buffers[MAX_BUFFERS];
	unsigned int nr_buffers;

public:
	V4L2Allocation(unsigned int nr_bufs,
				size_t buf_size, const char *pmem_path);
	~V4L2Allocation(void);

	inline unsigned int getBufferCount(void) const
	{
		return nr_buffers;
	}

	inline V4L2Buffer *getBuffer(unsigned int index)
	{
		if (index >= nr_buffers)
			return 0;

		return &buffers[index];
	}

	inline sp<MemoryHeapBase> getHeap(void)
	{
		return pmemHeap;
	}

	inline sp<MemoryBase> getMemory(int index)
	{
		intptr_t addr = (intptr_t)buffers[index].start;
		intptr_t base = (intptr_t)heap->getBase();
		return new MemoryBase(pmemHeap,
					addr - base, buffers[index].length);
	}
};

enum {
	V4L2_CAPTURE = 0,
	V4L2_OUTPUT,
	V4L2_DIRECTIONS
};

class V4L2Device {
	int fd;
	V4L2Allocation emptyAllocation;
	sp<V4L2Allocation> allocation[V4L2_DIRECTIONS];
	enum v4l2_buf_type type[V4L2_DIRECTIONS];
	static const v4l2_buf_type defaultType[V4L2_DIRECTIONS];
	bool isMultiPlane[V4L2_DIRECTIONS];

public:
	V4L2Device(const char *device);
	~V4L2Device(void);

	bool initCheck(void)
	{
		return fd != -1;
	}

	int setBufType(unsigned int direction, enum v4l2_buf_type type);
	short pollDevice(short mask, int timeout);
	const char *enumInput(int index);
	int setInput(int index);
	int setFormat(unsigned int direction,
				int width, int height, unsigned int fmt);
	int enumFormat(unsigned int direction, unsigned int fmt);
	int reqBufs(unsigned int direction, sp<V4L2Allocation> allocation);
	int queryBuf(unsigned int direction, unsigned int index,
						void **addr, size_t *length);
	int setStream(unsigned int direction, bool on);
	int queueBuf(unsigned int direction, int index);
	int dequeueBuf(unsigned int direction);
	int getCtrl(unsigned int id, int *value);
	int setCtrl(unsigned int id, int value);
	int getParam(unsigned int direction,
					struct v4l2_streamparm *streamparm);
	int setParam(unsigned int direction,
					struct v4l2_streamparm *streamparm);
};

#endif /* _V4L2DEVICE_H_ */
