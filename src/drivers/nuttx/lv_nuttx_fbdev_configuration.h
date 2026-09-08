/**
 * @file lv_nuttx_fbdev_configuration.h
 *
 */

#ifndef LV_NUTTX_FBDEV_CONFIGURATION_H
#define LV_NUTTX_FBDEV_CONFIGURATION_H

#include "lv_nuttx_fbdev.h"

#if LV_USE_NUTTX && defined(CONFIG_ASR_DPU_DISPLAY_V3)

#ifdef __cplusplus
extern "C" {
#endif

int lv_nuttx_fbdev_enable(int fd);
void lv_nuttx_fbdev_disable(int fd);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LV_USE_NUTTX && CONFIG_ASR_DPU_DISPLAY_V3 */

#endif /* LV_NUTTX_FBDEV_CONFIGURATION_H */
