LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(shell cat $(LOCAL_PATH)/busybox-full.sources) \
	libbb/android.c \
	android/libc/arch-arm/syscalls/adjtimex.S \
	android/libc/arch-arm/syscalls/getsid.S \
	android/libc/arch-arm/syscalls/stime.S \
	android/libc/arch-arm/syscalls/swapon.S \
	android/libc/arch-arm/syscalls/swapoff.S \
	android/libc/arch-arm/syscalls/sysinfo.S \
	android/libc/__set_errno.c

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/include-full \
	$(LOCAL_PATH)/include $(LOCAL_PATH)/libbb \
	external/clearsilver \
	external/clearsilver/util/regex \
	bionic/libc/private \
	libc/kernel/common

LOCAL_CFLAGS := \
	-std=gnu99 \
	-Werror=implicit \
	-DNDEBUG \
	-DANDROID_CHANGES \
	-include include-full/autoconf.h \
	-D'CONFIG_DEFAULT_MODULES_DIR="$(KERNEL_MODULES_DIR)"' \
	-D'BB_VER="teamwin"' -DBB_BT=AUTOCONF_TIMESTAMP -DBIONIC_ICS

LOCAL_MODULE := tw_busybox
LOCAL_MODULE_STEM := busybox
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULE_TAGS := eng

LOCAL_STATIC_LIBRARIES += libclearsilverregex

include $(BUILD_EXECUTABLE)

