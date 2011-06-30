# Inherit AOSP device configuration for passion.
$(call inherit-product, $(SRC_TARGET_DIR)/product/languages_full.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full.mk)

PRODUCT_SPECIFIC_DEFINES += TARGET_PRELINKER_MAP=$(TOP)/vendor/spica/prelink-linux-arm-spica.map

DEVICE_PACKAGE_OVERLAYS += device/samsung/spica/overlay

# density in DPI of the LCD of this board. This is used to scale the UI
# appropriately. If this property is not defined, the default value is 160 dpi. 
PRODUCT_PROPERTY_OVERRIDES += \
    ro.sf.lcd_density=160

# Init files
PRODUCT_COPY_FILES += \
        device/samsung/spica/init.rc:root/init.rc \
        device/samsung/spica/init.gt-i5700.rc:root/init.gt-i5700.rc \
        device/samsung/spica/ueventd.gt-i5700.rc:root/ueventd.gt-i5700.rc \
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
# Copy spica specific prebuilt files
#

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

#
# Setup device specific product configuration.
#
PRODUCT_NAME := full_spica
PRODUCT_DEVICE := GT-I5700
PRODUCT_MODEL := GT-I5700
PRODUCT_BRAND := samsung
PRODUCT_MANUFACTURER := Samsung

