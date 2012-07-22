ifneq ($(filter spica,$(TARGET_DEVICE)),)

# When zero we link against libqcamera; when 1, we dlopen libqcamera.
ifeq ($(BOARD_CAMERA_LIBRARIES),libcamera)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_CFLAGS:=-fno-short-enums

LOCAL_C_INCLUDES += $(LOCAL_PATH)/../include

LOCAL_SRC_FILES:= \
	V4L2Device.cpp \
	V4L2JpegEncoder.cpp \
	V4L2Camera.cpp \
	V4L2CameraHardware.cpp

LOCAL_SHARED_LIBRARIES := libutils libui liblog libbinder libcutils
LOCAL_SHARED_LIBRARIES += libcamera_client

ifeq ($(BOARD_USES_OVERLAY),true)
LOCAL_CFLAGS += -DBOARD_USES_OVERLAY
endif

LOCAL_MODULE:= libcamera

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

endif
endif
