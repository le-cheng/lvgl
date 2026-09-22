/**
 * @file lv_nuttx_fbdev_configuration.c
 *
 */

#include "lv_nuttx_fbdev_configuration.h"

#if LV_USE_NUTTX && \
    (defined(CONFIG_ASR_DPU_DISPLAY_V1) || defined(CONFIG_ASR_DPU_DISPLAY_V3))

#include <errno.h>
#ifdef CONFIG_ASR_DPU_DISPLAY_V3
#include <stdbool.h>
#endif
#include <stdint.h>
#include <sys/ioctl.h>

#ifdef __NuttX__
    #include <nuttx/video/fb.h>
    #include <nuttx/display/dpu.h>
    #include <lvgldemo_layer.h>
#else
    #include "mock/nuttx_video_fb.h"
#endif

#include "../../../lvgl.h"

#ifdef CONFIG_ASR_DPU_DISPLAY_V3
static bool enabled;

int lv_nuttx_fbdev_enable(int fd)
{
    const fb_buffer_config_s * config =
        lvgldemo_fb_config(LVGLDEMO_DISPLAY_LVGL_UI);

    if(!lvgldemo_display_enabled(LVGLDEMO_DISPLAY_LVGL_UI)) {
        LV_LOG_WARN("LVGL UI display disabled");
        return -ENODEV;
    }

    if(ioctl(fd, FBIO_ENABLE,
             (unsigned long)(uintptr_t)config) < 0) {
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
#else
int lv_nuttx_fbdev_set_buffer_config(int fd,
                                     struct fb_getbufferinfo_s *binfo)
{
    /* FB0 的配置来自 apps/system/display_server/lvgldemo_layer.h 的配置表，
     * 和 camera、远端 android 共用同一处配置。STATIC 模式的 buffer 由 FB
     * 驱动的 linker section 提供，几何尺寸也在配置表中显式提供。
     */
    const fb_buffer_config_s * config =
        lvgldemo_fb_config(LVGLDEMO_DISPLAY_LVGL_UI);

    if(!binfo) {
        return -EINVAL;
    }

    if(!lvgldemo_display_enabled(LVGLDEMO_DISPLAY_LVGL_UI)) {
        LV_LOG_WARN("LVGL UI display disabled");
        return -ENODEV;
    }

    if(ioctl(fd, FBIOSET_LAYER_SHOW_CONFIG,
             (unsigned long)(uintptr_t)config) < 0) {
        LV_LOG_ERROR("ioctl(FBIOSET_LAYER_SHOW_CONFIG) for FB0 failed: %d", errno);
        return -errno;
    }

    if(ioctl(fd, FBIOGET_BUFFERINFO,
             (unsigned long)(uintptr_t)binfo) < 0) {
        LV_LOG_ERROR("ioctl(FBIOGET_BUFFERINFO) for FB0 failed: %d", errno);
        return -errno;
    }

    return 0;
}
#endif

#endif /* LV_USE_NUTTX && (CONFIG_ASR_DPU_DISPLAY_V1 || CONFIG_ASR_DPU_DISPLAY_V3) */
