LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
#include $(LOCAL_PATH)/../common.mk

LOCAL_SRC_FILES := \
    androidplayer.cpp

LOCAL_C_INCLUDES:= \
        $(LOCAL_PATH)/.. \
        external/libcxx/include \
        $(TARGET_OUT_HEADERS)/libva

LOCAL_SHARED_LIBRARIES := \
        libutils \
        liblog \
        libgui \
        libhardware \
        libva \
        libva-android \

#libva \
#        libc++ \
#        libva-android \

LOCAL_MODULE := androidplayer
include $(BUILD_EXECUTABLE)
