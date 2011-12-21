ifneq ($(TARGET_SIMULATOR),true)
ifeq ($(TARGET_ARCH),arm)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	mtdutils.c \
	mounts.c 

LOCAL_MODULE := libmtdutils
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin

include $(BUILD_SHARED_LIBRARY)

endif	# TARGET_ARCH == arm
endif	# !TARGET_SIMULATOR

