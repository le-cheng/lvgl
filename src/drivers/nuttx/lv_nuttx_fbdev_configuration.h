/**
 * @file lv_nuttx_fbdev_configuration.h
 *
 */

#ifndef LV_NUTTX_FBDEV_CONFIGURATION_H
#define LV_NUTTX_FBDEV_CONFIGURATION_H

#include "lv_nuttx_fbdev.h"

#if LV_USE_NUTTX && \
    (defined(CONFIG_ASR_DPU_DISPLAY_V1) || defined(CONFIG_ASR_DPU_DISPLAY_V3))

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_ASR_DPU_DISPLAY_V3
int lv_nuttx_fbdev_enable(int fd);
void lv_nuttx_fbdev_disable(int fd);
#else
struct fb_getbufferinfo_s;

int lv_nuttx_fbdev_set_buffer_config(int fd,
                                     struct fb_getbufferinfo_s *binfo);
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LV_USE_NUTTX && (CONFIG_ASR_DPU_DISPLAY_V1 || CONFIG_ASR_DPU_DISPLAY_V3) */

#endif /* LV_NUTTX_FBDEV_CONFIGURATION_H */
