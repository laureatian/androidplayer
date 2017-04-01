/*
 * Copyright (C) 2013-2014 Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>
#include <ui/DisplayInfo.h>
#include <android/native_window.h>
#include <system/window.h>
#include <ui/GraphicBufferMapper.h>

#include <va/va_android.h>
#include <va/va_drmcommon.h>
#include <va/va.h>
//#include <cros_gralloc_helpers.h>

#include <map>
#include <vector>


#ifndef CHECK_EQ
#define CHECK_EQ(a, b) do {                     \
        if ((a) != (b)) {                   \
            assert(0 && "assert fails");    \
        }                                   \
    } while (0)
#endif

#include <memory>
#define SharedPtr std::shared_ptr

using namespace android;

struct VideoFrame {
    VASurfaceID surface;
    ANativeWindowBuffer* buf;

};

#define ANDROID_DISPLAY 0x18C34078
#define ERROR printf
//#define HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL 'NV12'
#define HAL_PIXEL_FORMAT_NV12  0x102

enum {
        GRALLOC_DRM_GET_STRIDE,
        GRALLOC_DRM_GET_FORMAT,
        GRALLOC_DRM_GET_DIMENSIONS,
        GRALLOC_DRM_GET_BACKING_STORE,
	GRALLOC_DRM_GET_DRM_FD,
};

#define GRALLOC_MODULE_PERFORM_GET_DRM_FD 0x80000002


class AndroidPlayer
{
public:
    bool init(int argc, char** argv)
    {
        /*if (argc != 2) {
            printf("usage: androidplayer xxx.264\n");
            return false;
        }*/

        if(!initWindow()) {
            fprintf(stderr, "failed to create android surface\n");
            return false;
        }

        if (!initDisplay()) {
            return false;
        }

        return true;
    }

    bool run()
    {
        renderOutputs();
        return true;
    }

    AndroidPlayer() : m_width(0), m_height(0)
    {
        hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (hw_module_t const**)&m_pGralloc);
    }

    ~AndroidPlayer()
    {
    }
private:

    bool initDisplay()
    {
        unsigned int display = ANDROID_DISPLAY;
        m_vaDisplay = vaGetDisplay(&display);

        int major, minor;
        VAStatus status;
        status = vaInitialize(m_vaDisplay, &major, &minor);
        if (status != VA_STATUS_SUCCESS) {
            fprintf(stderr, "init vaDisplay failed\n");
            return false;
        }

        return true;
    }


    void renderOutputs()
    {
        SharedPtr<VideoFrame> srcFrame;
        do {
            srcFrame = getFrame();
            if (!srcFrame)
                break;

            if(!displayFrame(srcFrame))
                break;
        } while (1);
    }

    bool initWindow()
    {
        static sp<SurfaceComposerClient> client = new SurfaceComposerClient();
        //create surface
        static sp<SurfaceControl> surfaceCtl = client->createSurface(String8("testsurface"), 800, 600, HAL_PIXEL_FORMAT_NV12, 0);

        // configure surface
        SurfaceComposerClient::openGlobalTransaction();
        surfaceCtl->setLayer(100000);
        surfaceCtl->setPosition(100, 100);
        surfaceCtl->setSize(800, 600);
        SurfaceComposerClient::closeGlobalTransaction();

        m_surface = surfaceCtl->getSurface();

        sp<ANativeWindow> mNativeWindow = m_surface;
        int bufWidth = 640;
        int bufHeight = 480;
        CHECK_EQ(0,
                 native_window_set_usage(
                                         mNativeWindow.get(),
                                         GRALLOC_USAGE_SW_READ_NEVER | GRALLOC_USAGE_SW_WRITE_OFTEN
                                         | GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_EXTERNAL_DISP));

        CHECK_EQ(0,
                 native_window_set_scaling_mode(
                                                mNativeWindow.get(),
                                                NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW));

        CHECK_EQ(0, native_window_set_buffers_geometry(
                                                       mNativeWindow.get(),
                                                       bufWidth,
                                                       bufHeight,
                                                       HAL_PIXEL_FORMAT_NV12));

        CHECK_EQ(0, native_window_api_connect(mNativeWindow.get(), NATIVE_WINDOW_API_MEDIA));

        status_t err;
        err = native_window_set_buffer_count(mNativeWindow.get(), 5);
        if (err != 0) {
            ALOGE("native_window_set_buffer_count failed: %s (%d)", strerror(-err), -err);
            return false;
        }

        return true;
    }


    SharedPtr<VideoFrame> createVaSurface(ANativeWindowBuffer* buf)
    {
        SharedPtr<VideoFrame> frame;


        uint32_t width, height, stride;
        uint32_t format;
        int fd;
        CHECK_EQ(0, m_pGralloc->perform(m_pGralloc, GRALLOC_DRM_GET_DIMENSIONS, (buffer_handle_t)buf->handle, &width, &height));
        CHECK_EQ(0, m_pGralloc->perform(m_pGralloc, GRALLOC_DRM_GET_STRIDE, (buffer_handle_t)buf->handle, &stride));
        CHECK_EQ(0, m_pGralloc->perform(m_pGralloc, GRALLOC_DRM_GET_FORMAT, (buffer_handle_t)buf->handle, &format));
        CHECK_EQ(0, m_pGralloc->perform(m_pGralloc, GRALLOC_DRM_GET_DRM_FD, (buffer_handle_t)buf->handle, &fd));
        ERROR("%dx%d, %d, format = %d, fd = %d\r\n", (int)width, (int)height, (int)stride, (int)format, fd);

        VASurfaceAttribExternalBuffers external;
        memset(&external, 0, sizeof(external));

        external.pixel_format = VA_FOURCC_NV12;
        external.width = width;
        external.height = height;
        external.pitches[0] = stride;
        external.pitches[1] = stride/2;
        external.offsets[0] = 0;
        external.offsets[1] = stride * height;
        external.data_size = stride * height * 3/2;
        external.num_planes = 2;
        external.num_buffers = 1;
        ERROR("%dx%d\n", buf->width, buf->height);
        for (unsigned int i = 0; i < external.num_planes; i++) {
            ERROR("i = %d, stride = %d, offset = %d\r\n", i , external.pitches[i], external.offsets[i]);
        }
        ERROR("datasize = %d",  external.data_size);


        unsigned long handle = (unsigned long)fd;
        external.buffers = &handle;


		VASurfaceAttrib attribs[2];
        memset(&attribs, 0, sizeof(attribs));
        attribs[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
        attribs[0].type = VASurfaceAttribMemoryType;
        attribs[0].value.type = VAGenericValueTypeInteger;
        //attribs[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_KERNEL_DRM;
        attribs[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME;

        attribs[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
        attribs[1].type = VASurfaceAttribExternalBufferDescriptor;
        attribs[1].value.type = VAGenericValueTypePointer;
        attribs[1].value.value.p = &external;

        VASurfaceID id;
        VAStatus vaStatus = vaCreateSurfaces(m_vaDisplay, VA_RT_FORMAT_YUV420,
                width, height, &id, 1, attribs, 2);
        if (vaStatus != VA_STATUS_SUCCESS) {
            ERROR("vaCreateSurface failed, status = %d\n", vaStatus);
            return frame;
        }
        frame.reset(new VideoFrame);
        frame->buf = buf;
        frame->surface = id;
        ERROR("id = %x\r\n", id);
        return frame;



#if 0
        SharedPtr<VideoFrame> frame;

        intel_ufo_buffer_details_t info;
        memset(&info, 0, sizeof(info));
        *reinterpret_cast<uint32_t*>(&info) = sizeof(info);

        int err = 0;
        if (m_pGralloc)
            err = m_pGralloc->perform(m_pGralloc, INTEL_UFO_GRALLOC_MODULE_PERFORM_GET_BO_INFO, (buffer_handle_t)buf->handle, &info);

        if (0 != err || !m_pGralloc) {
            fprintf(stderr, "create vaSurface failed\n");
            return frame;
        }

        VASurfaceAttrib attrib;
        memset(&attrib, 0, sizeof(attrib));

        VASurfaceAttribExternalBuffers external;
        memset(&external, 0, sizeof(external));

        external.pixel_format = VA_FOURCC_NV12;
        external.width = buf->width;
        external.height = buf->height;
        external.pitches[0] = info.pitch;
        external.num_planes = 2;
        external.num_buffers = 1;
        uint8_t* handle = (uint8_t*)buf->handle;
        external.buffers = (long unsigned int*)&handle; //graphic handel
        external.flags = VA_SURFACE_ATTRIB_MEM_TYPE_ANDROID_GRALLOC;

        attrib.flags = VA_SURFACE_ATTRIB_SETTABLE;
        attrib.type = (VASurfaceAttribType)VASurfaceAttribExternalBufferDescriptor;
        attrib.value.type = VAGenericValueTypePointer;
        attrib.value.value.p = &external;

        VASurfaceID id;
        VAStatus vaStatus = vaCreateSurfaces(m_vaDisplay, VA_RT_FORMAT_YUV420,
                                             buf->width, buf->height, &id, 1, &attrib, 1);
        if (vaStatus != VA_STATUS_SUCCESS)
            return frame;

        frame.reset(new VideoFrame);
        memset(frame.get(), 0, sizeof(VideoFrame));

        frame->surface = static_cast<intptr_t>(id);
        frame->crop.width = buf->width;
        frame->crop.height = buf->height;
#endif

        return frame;
    }

    SharedPtr<VideoFrame> getFrame()
    {
        status_t err;
        SharedPtr<VideoFrame> frame;
        sp<ANativeWindow> mNativeWindow = m_surface;
        ANativeWindowBuffer* buf;
        ERROR("+wait");

        err = native_window_dequeue_buffer_and_wait(mNativeWindow.get(), &buf);
        if (err != 0) {
            fprintf(stderr, "dequeueBuffer failed: %s (%d)\n", strerror(-err), -err);
            return frame;
        }
        ERROR("-wait");

        std::map< ANativeWindowBuffer*, SharedPtr<VideoFrame> >::const_iterator it;
        it = m_buff.find(buf);
        if (it != m_buff.end()) {
            frame = it->second;
        } else {
            frame = createVaSurface(buf);
            m_buff.insert(std::pair<ANativeWindowBuffer*, SharedPtr<VideoFrame> >(buf, frame));
        }
        return frame;
    }

    bool displayFrame(SharedPtr<VideoFrame>& frame)
    {
        sp<ANativeWindow> mNativeWindow = m_surface;
        //ERROR("id = %p", frame->buf);
        if (mNativeWindow->queueBuffer(mNativeWindow.get(), frame->buf, -1) != 0) {
            fprintf(stderr, "queue buffer to native window failed\n");
            return false;
        }
        return true;
    }

    VADisplay m_vaDisplay;


    int m_width, m_height;

    sp<Surface> m_surface;
    std::map< ANativeWindowBuffer*, SharedPtr<VideoFrame> > m_buff;

    gralloc_module_t* m_pGralloc;
};

int main(int argc, char** argv)
{
    AndroidPlayer player;
    if (!player.init(argc, argv)) {
        ERROR("init player failed with %s", argv[1]);
        return -1;
    }
    if (!player.run()){
        ERROR("run simple player failed");
        return -1;
    }
    printf("play file done\n");

    return  0;

}
