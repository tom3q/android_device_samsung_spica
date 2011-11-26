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

#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/select.h>
#include <cutils/log.h>


#include "Smb380Sensor.h"


/*****************************************************************************/
Smb380Sensor::Smb380Sensor()
    : SensorBase(NULL, "bma150"),
      mEnabled(0),
      mDelay(200),
      mInputReader(4),
      mHasPendingEvent(false)
{
    LOGD("Smb380Sensor::Smb380Sensor()");
    mPendingEvent.version = sizeof(sensors_event_t);
    mPendingEvent.sensor = ID_A;
    mPendingEvent.type = SENSOR_TYPE_ACCELEROMETER;
    memset(mPendingEvent.data, 0, sizeof(mPendingEvent.data));

    if (data_fd) {
        strcpy(input_sysfs_path, "/sys/class/input/");
        strcat(input_sysfs_path, input_name);
        strcat(input_sysfs_path, "/device/");
        input_sysfs_path_len = strlen(input_sysfs_path);

        close_device();
    }
}

Smb380Sensor::~Smb380Sensor() {

    LOGD("Smb380Sensor::~Smb380Sensor()");
    if (mEnabled) {
        enable(0, 0);
    }
}

int Smb380Sensor::enable(int32_t, int en) {
    int ret;
    en = !!en;

    if (mEnabled == en)
        return 0;

    if (en)
        ret = open_device();
    else
        ret = close_device();

    if (ret)
        return ret;

    mEnabled = en;

    return 0;
}

bool Smb380Sensor::hasPendingEvents() const {
    /* FIXME probably here should be returning mEnabled but instead
        mHasPendingEvents. It does not work, so we cheat.*/
    //LOGD("Smb380Sensor::~hasPendingEvents %d", mHasPendingEvent ? 1 : 0 );
    return mHasPendingEvent;
}

int Smb380Sensor::setDelay(int32_t handle, int64_t ns)
{
    int fd;

    strcpy(&input_sysfs_path[input_sysfs_path_len], "poll");

    mDelay = ns / 1000000;
    if (mDelay < 10)
        mDelay = 10;
    if (mDelay > 200)
        mDelay = 200;

    if (!mEnabled)
        return 0;

    fd = open(input_sysfs_path, O_RDWR);
    if (fd >= 0) {
        int ret;
        char buf[4];

        LOGD("Smb380Sensor::setDelay() setting delay to %d ms", mDelay);

        snprintf(buf, sizeof(buf), "%d", mDelay);

        ret = write(fd, buf, sizeof(buf));

        close(fd);

        if (ret)
            return -1;

        return 0;
    }

    return -1;
}

int Smb380Sensor::readEvents(sensors_event_t* data, int count)
{
    //LOGD("Smb380Sensor::~readEvents() %d", count);
    if (count < 1)
        return -EINVAL;
        
    if (mHasPendingEvent) {
        mHasPendingEvent = false;
        mPendingEvent.timestamp = getTimestamp();
        *data = mPendingEvent;
        return mEnabled ? 1 : 0;
    }

    ssize_t n = mInputReader.fill(data_fd);
    if (n < 0)
        return n;

    int numEventReceived = 0;
    input_event const* event;

    while (count && mInputReader.readEvent(&event)) {
        int type = event->type;
        if (type == EV_ABS) {
            float value = event->value;
            if (event->code == EVENT_TYPE_ACCEL_Y) {
                mPendingEvent.acceleration.x = value * CONVERT_A_X;
            } else if (event->code == EVENT_TYPE_ACCEL_X) {
                mPendingEvent.acceleration.y = value * CONVERT_A_Y;
            } else if (event->code == EVENT_TYPE_ACCEL_Z) {
                mPendingEvent.acceleration.z = value * CONVERT_A_Z;
            }
        } else if (type == EV_SYN) {
            mPendingEvent.timestamp = timevalToNano(event->time);
            if (mEnabled) {
                *data++ = mPendingEvent;
                count--;
                numEventReceived++;
            }
        } else {
            LOGE("Smb380Sensor: unknown event (type=%d, code=%d)",
                    type, event->code);
        }
        mInputReader.next();
    }

    //LOGD("Smb380Sensor::~readEvents() numEventReceived = %d", numEventReceived);
    return numEventReceived++;
}
