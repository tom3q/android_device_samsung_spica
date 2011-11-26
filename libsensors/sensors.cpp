/*
 * Copyright (C) 2008 The Android Open Source Project
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

#define LOG_TAG "Sensors"

#include <hardware/sensors.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>

#include <linux/input.h>


#include <utils/Atomic.h>
#include <utils/Log.h>

#include "sensors.h"

#include "Smb380Sensor.h"
#if 0
#include "CompassSensor.h"
#include "OrientationSensor.h"
#endif

/*****************************************************************************/

#define DELAY_OUT_TIME 0x7FFFFFFF

#define SENSORS_ACCELERATION     (1<<ID_A)
#define SENSORS_MAGNETIC_FIELD   (1<<ID_M)
#define SENSORS_ORIENTATION      (1<<ID_O)

#define SENSORS_ACCELERATION_HANDLE     0
#define SENSORS_MAGNETIC_FIELD_HANDLE   1
#define SENSORS_ORIENTATION_HANDLE      2

#define AKM_FTRACE 0
#define AKM_DEBUG 0
#define AKM_DATA 0

/*****************************************************************************/

/* The SENSORS Module */
static const struct sensor_t sSensorList[] = {
        {
            "BMA023 3-axis Accelerometer", "Bosch Sensortec",
            1, SENSORS_ACCELERATION_HANDLE, SENSOR_TYPE_ACCELEROMETER,
            RANGE_A, RESOLUTION_A, 0.20f, 20000, { }
        },
#if 0
        {
            "AK8973 3-axis Magnetic field sensor", "Asahi Kasei",
            1, SENSORS_MAGNETIC_FIELD_HANDLE, SENSOR_TYPE_MAGNETIC_FIELD,
            2000.0f, CONVERT_M, 6.8f, 30000, { }
        },
        {
            "AK8973 3-axis Orientation Sensor", "Asahi Kasei",
            1, SENSORS_ORIENTATION_HANDLE, SENSOR_TYPE_ORIENTATION,
            360.0f, CONVERT_O, 7.8f, 30000, { }
        },
#endif
};


static int open_sensors(const struct hw_module_t* module, const char* id,
                        struct hw_device_t** device);


static int sensors__get_sensors_list(struct sensors_module_t* module,
                                     struct sensor_t const** list) 
{
        *list = sSensorList;
        return ARRAY_SIZE(sSensorList);
}

static struct hw_module_methods_t sensors_module_methods = {
        open: open_sensors
};

struct sensors_module_t HAL_MODULE_INFO_SYM = {
        common: {
                tag: HARDWARE_MODULE_TAG,
                version_major: 1,
                version_minor: 0,
                id: SENSORS_HARDWARE_MODULE_ID,
                name: "Samsung Sensor module",
                author: "Samsung Electronic Company",
                methods: &sensors_module_methods,
        },
        get_sensors_list: sensors__get_sensors_list,
};

struct sensors_poll_context_t {
    struct sensors_poll_device_t device; // must be first

        sensors_poll_context_t();
        ~sensors_poll_context_t();
    int activate(int handle, int enabled);
    int setDelay(int handle, int64_t ns);
    int pollEvents(sensors_event_t* data, int count);

private:
    static const int wake = 0;

    enum {
        bosch = 0,
#if 0
        yamaha,
        orientation,
#endif
        numSensorDrivers,
        numFds,
    };

    static const char WAKE_MESSAGE = 'W';
    struct pollfd mPollFds[numFds];
    int mFdToSensor[numFds];
    int mWritePipeFd;
    int lastPollFd;
    SensorBase* mSensors[numSensorDrivers];

    // For keeping track of usage (only count from system)
    bool mAccelActive;
    bool mMagnetActive;
    bool mOrientationActive;

    int real_activate(int handle, int enabled);

    int handleToDriver(int handle) const {
        switch (handle) {
            case ID_A:
                return bosch;
#if 0
            case ID_M:
                return yamaha;
            case ID_O:
                return orientation;
#endif
        }
        return -EINVAL;
    }
};

/*****************************************************************************/

sensors_poll_context_t::sensors_poll_context_t()
{
    for (int i = 0; i < numFds; ++i)
        mFdToSensor[i] = -1;
    lastPollFd = 0;

    mSensors[bosch] = new Smb380Sensor();
#if 0
    mSensors[yamaha] = new CompassSensor();
    mSensors[orientation] = new OrientationSensor();
#endif
    int wakeFds[2];
    int result = pipe(wakeFds);
    LOGE_IF(result<0, "error creating wake pipe (%s)", strerror(errno));
    fcntl(wakeFds[0], F_SETFL, O_NONBLOCK);
    fcntl(wakeFds[1], F_SETFL, O_NONBLOCK);
    mWritePipeFd = wakeFds[1];

    mPollFds[wake].fd = wakeFds[0];
    mPollFds[wake].events = POLLIN;
    mPollFds[wake].revents = 0;

    mAccelActive = false;
#if 0
    mMagnetActive = false;
    mOrientationActive = false;
#endif
}

sensors_poll_context_t::~sensors_poll_context_t() {
    for (int i=0 ; i<numSensorDrivers ; i++)
        delete mSensors[i];

    close(mPollFds[wake].fd);
    close(mWritePipeFd);
}

int sensors_poll_context_t::activate(int handle, int enabled) {
    int index = handleToDriver(handle);
    if (index < 0)
        return index;

    LOGD("%s: handle=%d, enabled=%d", __func__, handle, enabled);

    int i;
    for (i = 1; i < numFds; ++i)
        if (mFdToSensor[i] == index)
            break;

    if (!enabled) {
        if (i == numFds)
            return 0;

        LOGD("%s: index=%d, mFdToSensor[%d]=%d", __func__, index, i, mFdToSensor[i]);

        mPollFds[i] = mPollFds[lastPollFd];
        mFdToSensor[i] = mFdToSensor[lastPollFd];
        mFdToSensor[lastPollFd] = -1;
        --lastPollFd;
    }

    int err =  mSensors[index]->enable(handle, enabled);

    if (enabled && !err) {
        const char wakeMessage(WAKE_MESSAGE);
        int result;

        if (i == numFds) {
            int fd = mSensors[index]->getFd();
            if (fd < 0)
                return -EFAULT;
            mPollFds[++lastPollFd].fd = fd;
            mPollFds[lastPollFd].events = POLLIN;
            mPollFds[lastPollFd].revents = 0;
            mFdToSensor[lastPollFd] = index;
        }

        result = write(mWritePipeFd, &wakeMessage, 1);
        LOGE_IF(result<0, "error sending wake message (%s)", strerror(errno));
    }

    return err;
}

int sensors_poll_context_t::setDelay(int handle, int64_t ns) {

    int index = handleToDriver(handle);
    if (index < 0) return index;
    return mSensors[index]->setDelay(handle, ns);
}

int sensors_poll_context_t::pollEvents(sensors_event_t* data, int count)
{
    int nbEvents = 0;
    int n = 0;

    do {
        // see if we have some leftover from the last poll()
        for (int i=1 ; count && i<=lastPollFd ; i++) {
            SensorBase* const sensor(mSensors[mFdToSensor[i]]);
            if ((mPollFds[i].revents & POLLIN) || (sensor->hasPendingEvents())) {
                int nb = sensor->readEvents(data, count);
                if (nb < count) {
                    // no more data for this sensor
                    mPollFds[i].revents = 0;
                }
                count -= nb;
                nbEvents += nb;
                data += nb;
            }
        }

        if (count) {
            // we still have some room, so try to see if we can get
            // some events immediately or just wait if we don't have
            // anything to return
            n = poll(mPollFds, lastPollFd + 1, nbEvents ? 0 : -1);
            if (n<0) {
                LOGE("poll() failed (%s)", strerror(errno));
                return -errno;
            }
            if (mPollFds[wake].revents & POLLIN) {
                char msg;
                int result = read(mPollFds[wake].fd, &msg, 1);
                LOGE_IF(result<0, "error reading from wake pipe (%s)", strerror(errno));
                LOGE_IF(msg != WAKE_MESSAGE, "unknown message on wake queue (0x%02x)", int(msg));

                mPollFds[wake].revents = 0;
            }
        }
        // if we have events and space, go read them
    } while (n && count);

    return nbEvents;
}

/*****************************************************************************/

static int poll__close(struct hw_device_t *dev)
{
    sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
    if (ctx) {
        delete ctx;
    }
    return 0;
}

static int poll__activate(struct sensors_poll_device_t *dev,
        int handle, int enabled) {
    sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
    return ctx->activate(handle, enabled);
}

static int poll__setDelay(struct sensors_poll_device_t *dev,
        int handle, int64_t ns) {
    sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
    return ctx->setDelay(handle, ns);
}

static int poll__poll(struct sensors_poll_device_t *dev,
        sensors_event_t* data, int count) {
    sensors_poll_context_t *ctx = (sensors_poll_context_t *)dev;
    return ctx->pollEvents(data, count);
}

/*****************************************************************************/

/** Open a new instance of a sensor device using name */
static int open_sensors(const struct hw_module_t* module, const char* id,
                        struct hw_device_t** device)
{
        int status = -EINVAL;
        sensors_poll_context_t *dev = new sensors_poll_context_t();

        memset(&dev->device, 0, sizeof(sensors_poll_device_t));

        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version  = 0;
        dev->device.common.module   = const_cast<hw_module_t*>(module);
        dev->device.common.close    = poll__close;
        dev->device.activate        = poll__activate;
        dev->device.setDelay        = poll__setDelay;
        dev->device.poll            = poll__poll;

        *device = &dev->device.common;
        status = 0;

        return status;
}
