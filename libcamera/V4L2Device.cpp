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

#define LOG_NDEBUG 0
#define LOG_TAG "V4L2Device"

#include <dirent.h>
#include <utils/Log.h>
#include "V4L2Device.h"
#include "utils.h"
/*
 * V4L2Allocation
 */

V4L2Allocation::V4L2Allocation(unsigned int nr_bufs,
				size_t buf_size, const char *pmem_path) :
	nr_buffers(0)
{
	TRACE();

	if (nr_bufs == 0 || nr_bufs > MAX_BUFFERS)
		return;

	buf_size = ALIGN_TO_PAGE(buf_size);
	size_t heap_size = buf_size*nr_bufs;

	heap = new MemoryHeapBase(pmem_path, heap_size, 0);
	if (heap == 0) {
		ERR("failed to create memory heap base");
		return;
	}

	void *vaddr = heap->getBase();
	if (vaddr == MAP_FAILED)
		return;

	pmemHeap = new MemoryHeapPmem(heap, 0);
	if (pmemHeap == 0) {
		ERR("failed to create pmem memory heap");
		return;
	}

	nr_buffers = nr_bufs;

	int i = 0;
	do {
		memset(vaddr, i << 5, buf_size);
		buffers[i].start = vaddr;
		buffers[i].length = buf_size;
		vaddr = (uint8_t *)vaddr + buf_size;
		++i;
	} while (--nr_bufs);
}

V4L2Allocation::~V4L2Allocation(void)
{
	TRACE();

	if (heap != 0)
		heap->dispose();
}

/*
 * V4L2Device
 */

const v4l2_buf_type V4L2Device::defaultType[V4L2_DIRECTIONS] = {
	V4L2_BUF_TYPE_VIDEO_CAPTURE,
	V4L2_BUF_TYPE_VIDEO_OUTPUT
};

V4L2Device::V4L2Device(const char *device) :
	fd(-1),
	emptyAllocation(0, 0, 0)
{
	TRACE();
	const char sysfsPath[] = "/sys/class/video4linux";
	char path[PATH_MAX];
	char name[32];
	bool found = false;

	DIR *d = opendir(sysfsPath);
	if (d == NULL) {
		ERR("error opening %s (%s)", sysfsPath, strerror(errno));
		return;
	}

	struct dirent* de;
	while ((de = readdir(d)) != NULL) {
		if (de->d_name[0] == '.')
			continue;

		snprintf(path, sizeof(path), "%s/%s/name",
							sysfsPath, de->d_name);

		DBG("Enumerating %s", path);

		FILE *f = fopen(path, "r");
		if (!f)
			continue;

		name[0] = '\0';
		fscanf(f, "%s", name);
		fclose(f);

		DBG("Enumerated %s at %s", name, path);

		if (!strcmp(device, name)) {
			snprintf(path, sizeof(path), "/dev/%s", de->d_name);
			found = true;
			break;
		}
	}
	closedir(d);

	if (!found) {
		ERR("device %s not found", device);
		return;
	}

	fd = open(path, O_RDWR);
	if (fd < 0) {
		ERR("failed to open %s", path);
		return;
	}

	emptyAllocation.incStrong(this);

	for (int i = 0; i < V4L2_DIRECTIONS; ++i) {
		allocation[i] = &emptyAllocation;
		type[i] = defaultType[i];
		isMultiPlane[i] = false;
	}
}

V4L2Device::~V4L2Device(void)
{
	TRACE();

	for (int i = 0; i < V4L2_DIRECTIONS; ++i)
		allocation[i].clear();

	if (fd >= 0)
		close(fd);
}

short V4L2Device::pollDevice(short mask, int timeout)
{
	struct pollfd events;
	int ret;

	TRACE();

	memset(&events, 0, sizeof(events));
	events.fd = fd;
	events.events = mask;

	ret = poll(&events, 1, timeout);
	if (ret < 0) {
		ERR("poll failed (%s)", strerror(errno));
		return 0;
	}

	if (!ret) {
		ERR("no data in %d ms", timeout);
		return 0;
	}

	return events.revents;
}

int V4L2Device::setBufType(unsigned int direction, enum v4l2_buf_type type)
{
	struct v4l2_requestbuffers req;
	int ret;

	TRACE();

	if (direction >= V4L2_DIRECTIONS)
		return -1;

	memset(&req, 0, sizeof(req));
	req.count = 0;
	req.type = type;
	req.memory = V4L2_MEMORY_USERPTR;

	ret = ioctl(fd, VIDIOC_REQBUFS, &req);
	if (ret < 0) {
		ERR("VIDIOC_REQBUFS failed (%s)", strerror(errno));
		return -1;
	}

	this->type[direction] = type;
	isMultiPlane[direction] = V4L2_TYPE_IS_MULTIPLANAR(type);

	return 0;
}

const char *V4L2Device::enumInput(int index)
{
	static struct v4l2_input input;
	int ret;

	TRACE();

	memset(&input, 0, sizeof(input));
	input.index = index;

	ret = ioctl(fd, VIDIOC_ENUMINPUT, &input);
	if (ret < 0) {
		ERR("VIDIOC_ENUMINPUT failed (%s)", strerror(errno));
		return NULL;
	}

	return (const char *)input.name;
}

int V4L2Device::setInput(int index)
{
	struct v4l2_input input;
	int ret;

	TRACE();

	memset(&input, 0, sizeof(input));
	input.index = index;

	ret = ioctl(fd, VIDIOC_S_INPUT, &input);
	if (ret < 0) {
		ERR("VIDIOC_S_INPUT failed (%s)", strerror(errno));
		return ret;
	}

	return ret;
}

int V4L2Device::setFormat(unsigned int direction,
					int width, int height, unsigned int fmt)
{
	struct v4l2_format v4l2_fmt;
	struct v4l2_pix_format *pixfmt = &v4l2_fmt.fmt.pix;
	int ret;

	TRACE();

	if (direction >= V4L2_DIRECTIONS)
		return -1;

	memset(&v4l2_fmt, 0, sizeof(v4l2_fmt));
	v4l2_fmt.type = type[direction];

	pixfmt->width		= width;
	pixfmt->height		= height;
	pixfmt->pixelformat	= fmt;
	pixfmt->sizeimage	= get_buffer_size(width, height, fmt);
	pixfmt->field		= V4L2_FIELD_NONE;
	if (fmt == V4L2_PIX_FMT_JPEG)
		pixfmt->colorspace = V4L2_COLORSPACE_JPEG;

	ret = ioctl(fd, VIDIOC_S_FMT, &v4l2_fmt);
	if (ret < 0) {
		ERR("VIDIOC_S_FMT failed (%s)", strerror(errno));
		return -1;
	}

	return 0;
}

int V4L2Device::enumFormat(unsigned int direction, unsigned int fmt)
{
	struct v4l2_fmtdesc fmtdesc;

	TRACE();

	if (direction >= V4L2_DIRECTIONS)
		return -1;

	memset(&fmtdesc, 0, sizeof(fmtdesc));
	fmtdesc.type = type[direction];
	fmtdesc.index = 0;

	while (!ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc)) {
		if (fmtdesc.pixelformat == fmt)
			break;

		++fmtdesc.index;
	}

	if (fmtdesc.pixelformat != fmt) {
		ERR("unsupported pixel format (%u)", fmt);
		return -1;
	}

	return 0;
}

int V4L2Device::reqBufs(unsigned int direction, sp<V4L2Allocation> allocation)
{
	struct v4l2_requestbuffers req;
	int ret;

	TRACE();

	if (direction >= V4L2_DIRECTIONS)
		return -1;

	if (allocation == 0)
		allocation = &emptyAllocation;

	memset(&req, 0, sizeof(req));
	req.count = allocation->getBufferCount();
	req.type = type[direction];
	req.memory = V4L2_MEMORY_USERPTR;

	ret = ioctl(fd, VIDIOC_REQBUFS, &req);
	if (ret < 0) {
		ERR("VIDIOC_REQBUFS failed (%s)", strerror(errno));
		return -1;
	}

	this->allocation[direction] = allocation;

	return req.count;
}

int V4L2Device::queryBuf(unsigned int direction, unsigned int index,
						void **addr, size_t *length)
{
	const struct V4L2Buffer *buf;
	int ret;

	TRACE();

	if (direction >= V4L2_DIRECTIONS)
		return -1;

	buf = allocation[direction]->getBuffer(index);
	if (!buf) {
		ERR("invalid buffer index %u", index);
		return -1;
	}

	*addr = buf->start;
	*length = buf->length;

	return 0;
}

int V4L2Device::setStream(unsigned int direction, bool on)
{
	int request;
	int ret;

	TRACE();

	if (direction >= V4L2_DIRECTIONS)
		return -1;

	request = (on) ? VIDIOC_STREAMON : VIDIOC_STREAMOFF;

	ret = ioctl(fd, request, &type[direction]);
	if (ret < 0) {
		ERR("VIDIOC_STREAM%s failed (%s)",
					(on) ? "ON" : "OFF", strerror(errno));
		return ret;
	}

	return ret;
}

int V4L2Device::queueBuf(unsigned int direction, int index)
{
	const struct V4L2Buffer *buf;
	struct v4l2_buffer v4l2_buf;
	struct v4l2_plane plane;
	int ret;

	TRACE();

	if (direction >= V4L2_DIRECTIONS)
		return -1;

	buf = allocation[direction]->getBuffer(index);
	if (!buf) {
		ERR("invalid buffer index %d", index);
		return -1;
	}

	memset(&v4l2_buf, 0, sizeof(v4l2_buf));
	v4l2_buf.type = type[direction];
	v4l2_buf.memory = V4L2_MEMORY_USERPTR;
	v4l2_buf.index = index;

	if (isMultiPlane[direction]) {
		memset(&plane, 0, sizeof(plane));
		plane.m.userptr = (unsigned long)buf->start;
		plane.length = buf->length;

		v4l2_buf.m.planes = &plane;
		v4l2_buf.length = 1;
	} else {
		v4l2_buf.m.userptr = (unsigned long)buf->start;
		v4l2_buf.length = buf->length;
	}

	ret = ioctl(fd, VIDIOC_QBUF, &v4l2_buf);
	if (ret < 0) {
		ERR("VIDIOC_QBUF failed (%s)", strerror(errno));
		return ret;
	}

	return 0;
}

int V4L2Device::dequeueBuf(unsigned int direction)
{
	struct V4L2Buffer *buf;
	struct v4l2_buffer v4l2_buf;
	int ret;

	TRACE();

	if (direction >= V4L2_DIRECTIONS)
		return -1;

	memset(&v4l2_buf, 0, sizeof(v4l2_buf));
	v4l2_buf.type = type[direction];
	v4l2_buf.memory = V4L2_MEMORY_USERPTR;

	ret = ioctl(fd, VIDIOC_DQBUF, &v4l2_buf);
	if (ret < 0) {
		ERR("VIDIOC_DQBUF failed (%s)", strerror(errno));
		return ret;
	}

	buf = allocation[direction]->getBuffer(v4l2_buf.index);
	if (!buf) {
		ERR("invalid buffer index %d", v4l2_buf.index);
		return -1;
	}

	buf->used = v4l2_buf.bytesused;
	return v4l2_buf.index;
}

int V4L2Device::getCtrl(unsigned int id, int *value)
{
	struct v4l2_control ctrl;
	int ret;

	TRACE();

	ctrl.id = id;

	ret = ioctl(fd, VIDIOC_G_CTRL, &ctrl);
	if (ret < 0) {
		ERR("VIDIOC_G_CTRL(0x%x) failed (%s)", id, strerror(errno));
		return ret;
	}

	*value = ctrl.value;
	return 0;
}

int V4L2Device::setCtrl(unsigned int id, int value)
{
	struct v4l2_control ctrl;
	int ret;

	TRACE();

	ctrl.id = id;
	ctrl.value = value;

	ret = ioctl(fd, VIDIOC_S_CTRL, &ctrl);
	if (ret < 0) {
		ERR("VIDIOC_S_CTRL(0x%x, %d) failed (%s)",
						id, value, strerror(errno));
		return ret;
	}

	return ctrl.value;
}

int V4L2Device::getParam(unsigned int direction,
					struct v4l2_streamparm *streamparm)
{
	int ret;

	TRACE();

	if (direction >= V4L2_DIRECTIONS)
		return -1;

	streamparm->type = type[direction];

	ret = ioctl(fd, VIDIOC_G_PARM, streamparm);
	if (ret < 0) {
		ERR("VIDIOC_G_PARM failed (%s)", strerror(errno));
		return -1;
	}

	return 0;
}

int V4L2Device::setParam(unsigned int direction,
					struct v4l2_streamparm *streamparm)
{
	int ret;

	TRACE();

	if (direction >= V4L2_DIRECTIONS)
		return -1;

	streamparm->type = type[direction];

	ret = ioctl(fd, VIDIOC_S_PARM, streamparm);
	if (ret < 0) {
		ERR("VIDIOC_S_PARM failed (%s)", strerror(errno));
		return ret;
	}

	return 0;
}
