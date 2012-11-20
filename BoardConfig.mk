# Copyright (C) 2009 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# BoardConfig.mk
#
# Product-specific compile-time definitions.
#

# Set this up here so that BoardVendorConfig.mk can override it

# Audio
BOARD_USES_GENERIC_AUDIO := false

# Use the non-open-source parts, if they're present
-include vendor/samsung/spica/BoardConfigVendor.mk

# Platform
TARGET_BOARD_PLATFORM := s3c6410
TARGET_BOOTLOADER_BOARD_NAME := spica
TARGET_CPU_ABI := armeabi-v6l
TARGET_CPU_ABI2 := armeabi
TARGET_ARCH_VARIANT := armv6-vfp
TARGET_ARCH_VARIANT_CPU := arm1176jzf-s
TARGET_GLOBAL_CFLAGS += -mtune=arm1176jzf-s -mfpu=vfp -mfloat-abi=softfp -O3
TARGET_GLOBAL_CPPFLAGS += -mtune=arm1176jzf-s -mfpu=vfp -mfloat-abi=softfp -O3

# Package contents
TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true
TARGET_PREBUILT_KERNEL := device/samsung/spica/kernel.bin

# Init
TARGET_PROVIDES_INIT := true
TARGET_PROVIDES_INIT_TARGET_RC := true
BOARD_PROVIDES_BOOTMODE := true

# Recovery
TARGET_RECOVERY_INITRC := device/samsung/spica/recovery.rc
BOARD_CUSTOM_RECOVERY_KEYMAPPING:= ../../device/samsung/spica/recovery/recovery_ui.c

# Mobile data
BOARD_MOBILEDATA_INTERFACE_NAME = "pdp0"

# Releasetools
TARGET_RELEASETOOL_OTA_FROM_TARGET_SCRIPT := ./device/samsung/spica/releasetools/spica_ota_from_target_files
TARGET_RELEASETOOL_IMG_FROM_TARGET_SCRIPT := ./device/samsung/spica/releasetools/spica_img_from_target_files

# Camera
USE_CAMERA_STUB := false
ifeq ($(USE_CAMERA_STUB),false)
BOARD_CAMERA_LIBRARIES := libcamera
endif

# Bluetooth
BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_BLUETOOTH_BCM := true

# Video Devices
#BOARD_USES_OVERLAY := true
#BOARD_CAMERA_DEVICE := /dev/video0

# Partitions (fake data, needed by build system)
BOARD_BOOTIMAGE_PARTITION_SIZE := 0x00600000
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 0x10000000
BOARD_USERDATAIMAGE_PARTITION_SIZE := 0x0e000000
BOARD_FLASH_BLOCK_SIZE := 131072

# Misc
WITH_DEXPREOPT := true
BOARD_HAS_NO_SELECT_BUTTON := true
BOARD_HAS_NO_MISC_PARTITION := true
WITH_JIT := true
ENABLE_JSC_JIT := true
JS_ENGINE := v8
BUILD_WITH_FULL_STAGEFRIGHT := true
TARGET_PROVIDES_LIBAUDIO := true
TARGET_PROVIDES_LIBRIL := true

# Connectivity - Wi-Fi
WPA_SUPPLICANT_VERSION := VER_0_6_X
BOARD_WPA_SUPPLICANT_DRIVER := WEXT
BOARD_WLAN_DEVICE := bcm4329
WIFI_DRIVER_MODULE_PATH := "/lib/modules/bcmdhd.ko"
WIFI_DRIVER_FW_STA_PATH := "/system/etc/fw_bcm4325.bin"
WIFI_DRIVER_MODULE_NAME := "bcmdhd"
WIFI_DRIVER_MODULE_ARG := "firmware_path=/system/etc/fw_bcm4325.bin nvram_path=/system/etc/nvram.txt dhd_watchdog_ms=10 dhd_poll=1"

# Sensors
SOMC_CFG_SENSORS_ACCEL_BMA150_INPUT := yes
SOMC_CFG_SENSORS_COMPASS_AK8973_AKMTASTIC := yes

# GPS
BOARD_GPS_LIBRARIES := libsecgps libsecril-client
BOARD_USES_GPSSHIM := true

# 3D
TARGET_LIBAGL_USE_GRALLOC_COPYBITS := true
BOARD_EGL_CFG := device/samsung/spica/egl.cfg

# Prelinker
PRODUCT_SPECIFIC_DEFINES += TARGET_PRELINKER_MAP=\$(TOP)/device/samsung/spica/prelink-linux-arm-spica.map
