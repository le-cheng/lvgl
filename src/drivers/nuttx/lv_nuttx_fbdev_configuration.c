/**
 * @file lv_nuttx_fbdev_configuration.c
 *
 */

#include "lv_nuttx_fbdev_configuration.h"

#if LV_USE_NUTTX && defined(CONFIG_ASR_DPU_DISPLAY_V3)

#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>

#ifdef __NuttX__
    #include <nuttx/video/fb.h>
    #include <nuttx/display/dpu.h>
#else
    #include "mock/nuttx_video_fb.h"
#endif

#include "../../../lvgl.h"

int lv_nuttx_fbdev_enable(int fd, bool * enabled)
{
    struct fb_buffer_config_s config;

    if(enabled == NULL) {
        return -EINVAL;
    }

    lv_memzero(&config, sizeof(config));
    config.fb_index = 0;
    config.layer_id = GRAPHICS0;
    config.remote_layer_id = FB_BUFFER_NO_REMOTE_LAYER;
    config.buffer_count = 2;
    config.mode = FB_BUFFER_MODE_INTERNAL;

    if(ioctl(fd, FBIO_ENABLE,
             (unsigned long)(uintptr_t)&config) < 0) {
        LV_LOG_ERROR("ioctl(FBIO_ENABLE) for FB0 failed: %d", errno);
        return -errno;
    }

    *enabled = true;
    return 0;
}

void lv_nuttx_fbdev_disable(int fd, bool * enabled)
{
    if(enabled == NULL || !*enabled) {
        return;
    }

    if(ioctl(fd, FBIO_DISABLE, 0) < 0) {
        LV_LOG_WARN("ioctl(FBIO_DISABLE) for FB0 failed: %d", errno);
    }

    *enabled = false;
}

#endif /* LV_USE_NUTTX && CONFIG_ASR_DPU_DISPLAY_V3 */
