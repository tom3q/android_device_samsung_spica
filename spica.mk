# Copyright (C) 2010 The Android Open Source Project
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


# This file is the device-specific product definition file for
# spica. It lists all the overlays, files, modules and properties
# that are specific to this hardware: i.e. those are device-specific
# drivers, configuration files, settings, etc...

# These is the hardware-specific overlay, which points to the location
# of hardware-specific resource overrides, typically the frameworks and
# application settings that are stored in resourced.
DEVICE_PACKAGE_OVERLAYS += device/samsung/spica/overlay

# Init files
PRODUCT_COPY_FILES += \
        device/samsung/spica/init.rc:root/init.rc \
        device/samsung/spica/init.spica.rc:root/init.spica.rc \
        device/samsung/spica/ueventd.spica.rc:root/ueventd.spica.rc \
        device/samsung/spica/recovery.rc:root/recovery.rc

PRODUCT_COPY_FILES += \
        device/common/gps/gps.conf_EU_SUPL:system/etc/gps.conf

# Install the features available on this device.
PRODUCT_COPY_FILES += \
    frameworks/base/data/etc/handheld_core_hardware.xml:system/etc/permissions/handheld_core_hardware.xml \
    frameworks/base/data/etc/android.hardware.camera.autofocus.xml:system/etc/permissions/android.hardware.camera.autofocus.xml \
    frameworks/base/data/etc/android.hardware.telephony.gsm.xml:system/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/base/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
    frameworks/base/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/base/data/etc/android.hardware.touchscreen.multitouch.distinct.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.distinct.xml 

# Enable SIP+VoIP on all targets
PRODUCT_COPY_FILES += \
    frameworks/base/data/etc/android.software.sip.voip.xml:system/etc/permissions/android.software.sip.voip.xml

# media config xml file
PRODUCT_COPY_FILES += \
    device/samsung/spica/media_profiles.xml:system/etc/media_profiles.xml

# T-Mobile theme engine
PRODUCT_PACKAGES += \
       ThemeManager \
       ThemeChooser \
       com.tmobile.themes

PRODUCT_COPY_FILES += \
       vendor/cyanogen/prebuilt/common/etc/permissions/com.tmobile.software.themes.xml:system/etc/permissions/com.tmobile.software.themes.xml

# Theme packages
PRODUCT_PACKAGES += \
       Androidian \
       Cyanbread

#
# AKMD
#
PRODUCT_COPY_FILES += \
    device/samsung/spica/prebuilt/spica/akmd/akmd:system/bin/akmd

#
# Wifi
#
PRODUCT_COPY_FILES += \
    device/samsung/spica/prebuilt/spica/wifi/libwlmlogger.so:system/lib/libwlmlogger.so \
    device/samsung/spica/prebuilt/spica/wifi/libwlservice.so:system/lib/libwlservice.so \
    device/samsung/spica/prebuilt/spica/wifi/nvram.txt:system/etc/nvram.txt \
    device/samsung/spica/prebuilt/acclaim/wifi/rtecdc.bin:system/etc/rtecdc.bin \
    device/samsung/spica/prebuilt/acclaim/wifi/rtecdc_apsta.bin:system/etc/rtecdc_apsta.bin \
    device/samsung/spica/prebuilt/spica/wifi/nvram_mfg.txt:system/etc/nvram_mfg.txt \
    device/samsung/spica/prebuilt/spica/wifi/rtecdc_mfg.bin:system/etc/rtecdc_mfg.bin \
    device/samsung/spica/prebuilt/spica/wifi/bcm_supp.conf:system/etc/bcm_supp.conf \
    device/samsung/spica/prebuilt/spica/wifi/wifi.conf:system/etc/wifi.conf \
    device/samsung/spica/prebuilt/spica/wifi/wpa_supplicant.conf:system/etc/wifi/wpa_supplicant.conf \
    device/samsung/spica/prebuilt/spica/wifi/dhcpcd.conf:system/etc/dhcpcd/dhcpcd.conf \
    device/samsung/spica/prebuilt/spica/wifi/wlservice:system/bin/wlservice

#
# Display (3D)
#
PRODUCT_COPY_FILES += \
    device/samsung/spica/prebuilt/spica/fimg-libs/egl.cfg:system/lib/egl/egl.cfg

#
# Vold
#
PRODUCT_COPY_FILES += \
    device/samsung/spica/prebuilt/spica/vold/vold.fstab:system/etc/vold.fstab

#
# RIL
#
PRODUCT_COPY_FILES += \
    device/samsung/spica/prebuilt/spica/ril/drexe:system/bin/drexe \
    device/samsung/spica/prebuilt/spica/ril/efsd:system/bin/efsd \
    device/samsung/spica/prebuilt/spica/ril/rilclient-test:system/bin/rilclient-test \
    device/samsung/spica/prebuilt/spica/ril/libsec-ril.so:system/lib/libsec-ril.so \
    device/samsung/spica/prebuilt/spica/ril/rild:system/bin/rild

#
# GSM APN list
#
PRODUCT_COPY_FILES += \
    device/samsung/spica/prebuilt/common/etc/apns-conf.xml:system/etc/apns-conf.xml

#
# Audio
#
PRODUCT_COPY_FILES += \
    device/samsung/spica/prebuilt/spica/audio/asound.conf:system/etc/asound.conf

#
# SamdroidTools
#
PRODUCT_COPY_FILES += \
    device/samsung/spica/prebuilt/spica/samdroidtools/SamdroidTools.apk:system/app/SamdroidTools.apk \
    device/samsung/spica/prebuilt/spica/samdroidtools/libsamdroidtools.so:system/lib/libsamdroidtools.so

#
# serviceModeApp
#
PRODUCT_COPY_FILES += \
    device/samsung/spica/prebuilt/spica/servicemodeapp/serviceModeApp.apk:system/app/serviceModeApp.apk

#
# Bluetooth
#
PRODUCT_COPY_FILES += \
    device/samsung/spica/prebuilt/spica/bt/BCM4325D1_004.002.004.0153.0173.hcd:system/bin/BCM4325D1_004.002.004.0153.0173.hcd

#
# system/sd
#
PRODUCT_COPY_FILES += \
    device/samsung/spica/prebuilt/spica/placeholder/.placeholder:system/sd/.placeholder

#
# Prebuilt kl keymaps
#
PRODUCT_COPY_FILES += \
    device/samsung/spica/samsung-keypad.kl:system/usr/keylayout/samsung-keypad.kl \
    device/samsung/spica/gpio-keys.kl:system/usr/keylayout/gpio-keys.kl \

#
# Generated kcm keymaps
#
PRODUCT_PACKAGES := \
       samsung-keypad.kcm \
       gpio-keys.kcm

# Used by BusyBox
KERNEL_MODULES_DIR:=/lib/modules

# Enable Windows Media if supported by the board
WITH_WINDOWS_MEDIA:=true

# These are the hardware-specific settings that are stored in system properties.
# Note that the only such settings should be the ones that are too low-level to
# be reachable from resources or other mechanisms.
PRODUCT_PROPERTY_OVERRIDES += \
       wifi.interface=eth0 \
       wifi.supplicant_scan_interval=20 \
       ro.telephony.ril_class=samsung \
       mobiledata.interfaces=pdp0,eth0,gprs,ppp0 \
       dalvik.vm.heapsize=32m \
       dalvik.vm.dexopt-data-only=1 \

# enable Google-specific location features,
# like NetworkLocationProvider and LocationCollector
PRODUCT_PROPERTY_OVERRIDES += \
        ro.com.google.locationfeatures=1 \
        ro.com.google.networklocation=1

# OpenVPN
PRODUCT_PACKAGES += \
    openvpn

PRODUCT_PACKAGES += \
    ADWLauncher \
    AndroidTerm \
    CMParts \
    CMStats \
    CMUpdateNotify \
    CMWallpapers \
    FileManager \
    Pacman \
    Stk \
    Superuser

# Live wallpaper packages
PRODUCT_PACKAGES += \
    LiveWallpapers \
    LiveWallpapersPicker \
    MagicSmokeWallpapers \
    librs_jni

# Packages in device dir
PRODUCT_PACKAGES += \
    libs3cjpeg.so \
    libcamera.so \
    copybit.spica \
    gps.spica \
    gralloc.spica \
    lights.spica \
    sensors.spica \
    libsecgps.so \
    libsecril-client.so \
    libGLES_fimg \
    libfimg

# Publish that we support the live wallpaper feature.
PRODUCT_COPY_FILES += \
    packages/wallpapers/LivePicker/android.software.live_wallpaper.xml:/system/etc/permissions/android.software.live_wallpaper.xml

# Common CM overlay
PRODUCT_PACKAGE_OVERLAYS += vendor/spica/overlay/spica

# Bring in some audio files
include frameworks/base/data/sounds/AudioPackage4.mk

PRODUCT_COPY_FILES += \
    device/samsung/spica/prebuilt/common/bin/backuptool.sh:system/bin/backuptool.sh \
    device/samsung/spica/prebuilt/common/bin/remount:system/bin/remount \
    device/samsung/spica/prebuilt/common/bin/compcache:system/bin/compcache \
    device/samsung/spica/prebuilt/common/bin/handle_compcache:system/bin/handle_compcache \
    device/samsung/spica/prebuilt/common/lib/libncurses.so:system/lib/libncurses.so \
    device/samsung/spica/prebuilt/common/etc/init.d/03firstboot:system/etc/init.d/03firstboot \
    device/samsung/spica/prebuilt/common/etc/init.d/04apps2sd:system/etc/init.d/04apps2sd \
    device/samsung/spica/prebuilt/common/etc/init.d/05apps2sdoff:system/etc/init.d/05apps2sdoff \
    device/samsung/spica/prebuilt/common/etc/init.d/07userinit:system/etc/init.d/07userinit \
    device/samsung/spica/prebuilt/common/etc/init.d/99complete:system/etc/init.d/99complete \
    device/samsung/spica/prebuilt/common/etc/resolv.conf:system/etc/resolv.conf \
    device/samsung/spica/prebuilt/common/etc/terminfo/l/linux:system/etc/terminfo/l/linux \
    device/samsung/spica/prebuilt/common/etc/terminfo/u/unknown:system/etc/terminfo/u/unknown \
    device/samsung/spica/prebuilt/common/etc/profile:system/etc/profile \
    device/samsung/spica/prebuilt/common/xbin/bash:system/xbin/bash \
    device/samsung/spica/prebuilt/common/xbin/htop:system/xbin/htop \
    device/samsung/spica/prebuilt/common/xbin/lsof:system/xbin/lsof \
    device/samsung/spica/prebuilt/common/xbin/nano:system/xbin/nano \
    device/samsung/spica/prebuilt/common/xbin/powertop:system/xbin/powertop \
    device/samsung/spica/prebuilt/common/xbin/openvpn-up.sh:system/xbin/openvpn-up.sh
