/**
 * @file lv_demo_litewatch.h
 *
 */

#ifndef LV_DEMO_LITEWATCH_H
#define LV_DEMO_LITEWATCH_H

#ifdef __cplusplus
extern "C"
{
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../lv_demos.h"

#if LV_USE_DEMO_LITEWATCH

/*********************
 *      DEFINES
 *********************/
#define LITEWATCH_SCREEN_SIZE 466

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create a high-performance lite watch demo.
 * Two screens: Clock and Activity, swipe left/right to switch.
 * Recommended screen size 466x466.
 */
void lv_demo_litewatch(void);

#endif /*LV_USE_DEMO_LITEWATCH*/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_DEMO_LITEWATCH_H*/
