/**
 * @file lv_nuttx_fbdev_configuration.c
 *
 */

#include "lv_nuttx_fbdev_configuration.h"

#if LV_USE_NUTTX && defined(CONFIG_ASR_DPU_DISPLAY_V3)

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/ioctl.h>

#ifdef __NuttX__
    #include <nuttx/video/fb.h>
#else
    #include "mock/nuttx_video_fb.h"
#endif

#include "../../../lvgl.h"

static bool enabled;

int lv_nuttx_fbdev_enable(int fd)
{
    struct fb_buffer_config_s config;

    lv_memzero(&config, sizeof(config));
    config.fb_index = 0;
    config.remote_layer_id = FB_BUFFER_NO_REMOTE_LAYER;
    config.buffer_count = 2;
    config.mode = FB_BUFFER_MODE_STATIC;
#if defined(CONFIG_ASR_DPU_FB_DISPLAY_MODE_DIFFERENT)
    /* One logical LVGL canvas spans the two physical DPU outputs. */
    config.display_mode = FB_DISPLAY_MODE_DIFFERENT;
#else
    config.display_mode = FB_DISPLAY_MODE_SAME;
#endif

    if(ioctl(fd, FBIO_ENABLE,
             (unsigned long)(uintptr_t)&config) < 0) {
        LV_LOG_ERROR("ioctl(FBIO_ENABLE) for FB0 failed: %d", errno);
        return -errno;
    }

    enabled = true;
    return 0;
}

void lv_nuttx_fbdev_disable(int fd)
{
    if(!enabled) {
        return;
    }

    if(ioctl(fd, FBIO_DISABLE, 0) < 0) {
        LV_LOG_WARN("ioctl(FBIO_DISABLE) for FB0 failed: %d", errno);
    }

    enabled = false;
}

#endif /* LV_USE_NUTTX && CONFIG_ASR_DPU_DISPLAY_V3 */
