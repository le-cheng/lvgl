/**
 * @file lv_demo_litewatch.c
 * High-performance lite watch demo with 2 pages (Clock + Activity).
 * Uses scroll container with snap for page switching.
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_demo_litewatch.h"
#if LV_USE_DEMO_LITEWATCH

/*********************
 *      DEFINES
 *********************/
#define SCREEN_W        LITEWATCH_SCREEN_SIZE
#define SCREEN_H        LITEWATCH_SCREEN_SIZE
#define BG_COLOR        0x4A4590
#define ACCENT_CYAN     0x00E5FF
#define ACCENT_GREEN    0x00E676
#define TEXT_PRIMARY     0xFFFFFF
#define TEXT_SECONDARY   0x999999
#define ARC_BG_COLOR    0x222222
#define TIMER_PERIOD_MS 1000

/* Activity simulation */
#define STEPS_MAX              10000
#define STEPS_SIM_DIV          5000
#define STEPS_SIM_AMPLITUDE    100

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void create_clock_content(lv_obj_t * parent);
static void create_activity_content(lv_obj_t * parent);
static void create_swipe_dots(lv_obj_t * parent, bool active_right);
static void timer_cb(lv_timer_t * timer);
static void update_clock(void);
static void update_activity(void);
static void scroll_begin_cb(lv_event_t * e);
static void scroll_end_cb(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t * scroll_cont = NULL;
static lv_obj_t * page_activity = NULL;

/* Clock screen widgets */
static lv_obj_t * lbl_hour;
static lv_obj_t * lbl_minute;
static lv_obj_t * lbl_second;
static lv_obj_t * lbl_date;
static lv_obj_t * lbl_weekday;

/* Activity screen widgets */
static lv_obj_t * arc_steps;
static lv_obj_t * lbl_steps_val;
static lv_obj_t * lbl_steps_unit;
static lv_timer_t * update_timer;

/* Track last values to avoid unnecessary redraws */
static int32_t last_hour = -1;
static int32_t last_min = -1;
static int32_t last_sec = -1;
static int32_t last_steps = -1;
static uint32_t last_activity_ms = 0;
static bool activity_active = false;
static bool is_scrolling = false;

/**********************
 *  GLOBAL FUNCTIONS
 **********************/

void lv_demo_litewatch(void)
{
    lv_display_t * display = lv_display_get_default();
    int32_t hres = lv_display_get_horizontal_resolution(display);
    int32_t vres = lv_display_get_vertical_resolution(display);
    if(hres != SCREEN_W || vres != SCREEN_H) {
        LV_LOG_WARN("litewatch demo: display %dx%d recommended, got %dx%d",
                     SCREEN_W, SCREEN_H, (int)hres, (int)vres);
    }

    lv_obj_t * scr = lv_screen_active();
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(scr, lv_color_hex(BG_COLOR), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Create horizontal scroll container with snap-to-page.
     * Use absolute positioning for pages (not flex) to avoid layout
     * recalculation during scroll gestures. */
    scroll_cont = lv_obj_create(scr);
    lv_obj_remove_style_all(scroll_cont);
    lv_obj_set_size(scroll_cont, SCREEN_W, SCREEN_H);
    lv_obj_set_scroll_dir(scroll_cont, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(scroll_cont, LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(scroll_cont, LV_SCROLLBAR_MODE_OFF);

    /* Create 2 pages side by side */
    lv_obj_t * page_clock = lv_obj_create(scroll_cont);
    lv_obj_remove_style_all(page_clock);
    lv_obj_set_size(page_clock, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(page_clock, 0, 0);
    lv_obj_remove_flag(page_clock, LV_OBJ_FLAG_SCROLLABLE);
    /* Opaque background: LVGL skips blending layers behind the page during scroll */
    lv_obj_set_style_bg_color(page_clock, lv_color_hex(BG_COLOR), 0);
    lv_obj_set_style_bg_opa(page_clock, LV_OPA_COVER, 0);

    page_activity = lv_obj_create(scroll_cont);
    lv_obj_remove_style_all(page_activity);
    lv_obj_set_size(page_activity, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(page_activity, SCREEN_W, 0);
    lv_obj_remove_flag(page_activity, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(page_activity, lv_color_hex(BG_COLOR), 0);
    lv_obj_set_style_bg_opa(page_activity, LV_OPA_COVER, 0);

    /* Build page content */
    create_clock_content(page_clock);
    create_activity_content(page_activity);

    /* Freeze timer updates during scroll to avoid redraw contention */
    lv_obj_add_event_cb(scroll_cont, scroll_begin_cb, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(scroll_cont, scroll_end_cb, LV_EVENT_SCROLL_END, NULL);
    activity_active = false;
    is_scrolling = false;

    /* Timer for periodic updates */
    update_timer = lv_timer_create(timer_cb, TIMER_PERIOD_MS, NULL);
    lv_timer_ready(update_timer); /* update immediately */
}

/**********************
 *   PAGE BUILDERS
 **********************/

static void create_clock_content(lv_obj_t * parent)
{
    /* Time container */
    lv_obj_t * time_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(time_cont);
    lv_obj_set_size(time_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(time_cont);
    lv_obj_set_flex_flow(time_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(time_cont, 4, 0);

    /* Hour label - large */
    lbl_hour = lv_label_create(time_cont);
    lv_label_set_text(lbl_hour, "10");
    lv_obj_set_style_text_font(lbl_hour, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_hour, lv_color_hex(TEXT_PRIMARY), 0);

    /* Colon separator */
    lv_obj_t * colon = lv_label_create(time_cont);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(colon, lv_color_hex(ACCENT_CYAN), 0);
    lv_obj_set_style_pad_bottom(colon, 2, 0);

    /* Minute label - large */
    lbl_minute = lv_label_create(time_cont);
    lv_label_set_text(lbl_minute, "30");
    lv_obj_set_style_text_font(lbl_minute, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_minute, lv_color_hex(TEXT_PRIMARY), 0);

    /* Second label - smaller, accent color */
    lbl_second = lv_label_create(time_cont);
    lv_label_set_text(lbl_second, "00");
    lv_obj_set_style_text_font(lbl_second, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_second, lv_color_hex(ACCENT_CYAN), 0);
    lv_obj_set_style_pad_bottom(lbl_second, 6, 0);

    /* Date container - below time */
    lv_obj_t * date_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(date_cont);
    lv_obj_set_size(date_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_align(date_cont, LV_ALIGN_CENTER);
    lv_obj_set_y(date_cont, 55);
    lv_obj_set_flex_flow(date_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(date_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(date_cont, 12, 0);

    lbl_weekday = lv_label_create(date_cont);
    lv_label_set_text(lbl_weekday, "WED");
    lv_obj_set_style_text_font(lbl_weekday, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_weekday, lv_color_hex(ACCENT_CYAN), 0);

    lbl_date = lv_label_create(date_cont);
    lv_label_set_text(lbl_date, "06/23");
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_date, lv_color_hex(TEXT_SECONDARY), 0);

    /* Swipe hint dots */
    create_swipe_dots(parent, false);
}

static void create_activity_content(lv_obj_t * parent)
{
    /* Title */
    lv_obj_t * title = lv_label_create(parent);
    lv_label_set_text(title, "Activity");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(title, 30);

    /* Steps progress bar */
    arc_steps = lv_bar_create(parent);
    lv_obj_remove_style_all(arc_steps);
    lv_obj_set_size(arc_steps, 260, 14);
    lv_obj_center(arc_steps);
    lv_obj_set_y(arc_steps, -55);
    lv_bar_set_range(arc_steps, 0, 100);
    lv_bar_set_value(arc_steps, 65, LV_ANIM_OFF);
    /* Bar background */
    lv_obj_set_style_bg_color(arc_steps, lv_color_hex(ARC_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(arc_steps, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(arc_steps, LV_RADIUS_CIRCLE, 0);
    /* Bar indicator */
    lv_obj_set_style_bg_color(arc_steps, lv_color_hex(ACCENT_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc_steps, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(arc_steps, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    /* Steps value - center of arc */
    lbl_steps_val = lv_label_create(parent);
    lv_label_set_text(lbl_steps_val, "6524");
    lv_obj_set_style_text_font(lbl_steps_val, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_steps_val, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_align(lbl_steps_val, LV_ALIGN_CENTER);
    lv_obj_set_y(lbl_steps_val, -10);

    /* Steps unit label */
    lbl_steps_unit = lv_label_create(parent);
    lv_label_set_text(lbl_steps_unit, "steps");
    lv_obj_set_style_text_font(lbl_steps_unit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_steps_unit, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_align(lbl_steps_unit, LV_ALIGN_CENTER);
    lv_obj_set_y(lbl_steps_unit, 20);

    /* Swipe hint dots */
    create_swipe_dots(parent, true);
}

/**********************
 *   SWIPE DOTS HELPER
 **********************/

static void create_swipe_dots(lv_obj_t * parent, bool active_right)
{
    /* Left dot: active when on left page (clock), inactive when on right page (activity) */
    lv_obj_t * dot_left = lv_obj_create(parent);
    lv_obj_remove_style_all(dot_left);
    lv_obj_set_size(dot_left, active_right ? 6 : 8, active_right ? 6 : 8);
    lv_obj_set_style_bg_color(dot_left, active_right ? lv_color_hex(TEXT_SECONDARY) : lv_color_hex(ACCENT_CYAN), 0);
    lv_obj_set_style_bg_opa(dot_left, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot_left, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_align(dot_left, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_x(dot_left, -10);
    lv_obj_set_y(dot_left, -20);

    /* Right dot: active when on right page (activity) */
    lv_obj_t * dot_right = lv_obj_create(parent);
    lv_obj_remove_style_all(dot_right);
    lv_obj_set_size(dot_right, active_right ? 8 : 6, active_right ? 8 : 6);
    lv_obj_set_style_bg_color(dot_right, active_right ? lv_color_hex(ACCENT_GREEN) : lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_bg_opa(dot_right, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot_right, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_align(dot_right, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_x(dot_right, 10);
    lv_obj_set_y(dot_right, active_right ? -20 : -21);
}

/**********************
 *   TIMER / UPDATE
 **********************/

static void scroll_begin_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    is_scrolling = true;
}

static void scroll_end_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    is_scrolling = false;

    int32_t scroll_x = lv_obj_get_scroll_x(scroll_cont);
    /* page_activity is at x = SCREEN_W; its left edge hits viewport left at scroll_x = SCREEN_W */
    activity_active = (scroll_x >= SCREEN_W / 2);

    /* Refresh display now that scrolling is done */
    lv_timer_ready(update_timer);
}

static void timer_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    if(is_scrolling) return;

    update_clock();
    if(activity_active) {
        update_activity();
    }
}

static void update_clock(void)
{
    /* Use LVGL's tick count for simulated time */
    uint32_t ms = lv_tick_get();
    int32_t total_sec = (int32_t)(ms / 1000);

    /* Simulate real clock: start from 10:30:00 */
    int32_t sec = (total_sec % 60);
    int32_t min = ((total_sec / 60) % 60);
    int32_t hour = ((total_sec / 3600) % 24);

    /* Add offset so we start at a nice time */
    hour = (hour + 10) % 24;
    min = (min + 30) % 60;

    /* Early return if nothing changed since last tick */
    if(sec == last_sec && min == last_min && hour == last_hour) return;

    /* Only update hour if changed */
    if(hour != last_hour) {
        last_hour = hour;
        lv_label_set_text_fmt(lbl_hour, "%02d", (int)hour);
    }

    /* Only update minute if changed */
    if(min != last_min) {
        last_min = min;
        lv_label_set_text_fmt(lbl_minute, "%02d", (int)min);
    }

    /* Always update seconds */
    last_sec = sec;
    lv_label_set_text_fmt(lbl_second, "%02d", (int)sec);
}

static void update_activity(void)
{
    /* Simulated activity data with slight variation */
    uint32_t ms = lv_tick_get();
    if(ms == last_activity_ms) return;
    last_activity_ms = ms;

    int32_t steps = 6524 + (int32_t)((ms / STEPS_SIM_DIV) % STEPS_SIM_AMPLITUDE);

    if(steps != last_steps) {
        last_steps = steps;
        lv_label_set_text_fmt(lbl_steps_val, "%d", (int)steps);

        int32_t pct = (steps * 100) / STEPS_MAX;
        if(pct > 100) pct = 100;
        lv_bar_set_value(arc_steps, pct, LV_ANIM_OFF);
    }
}

/**********************
 *   DEINIT
 **********************/

void lv_demo_litewatch_deinit(void)
{
    if(update_timer) {
        lv_timer_delete(update_timer);
        update_timer = NULL;
    }

    /* Reset cached values */
    last_hour = -1;
    last_min = -1;
    last_sec = -1;
    last_steps = -1;
    last_activity_ms = 0;
    activity_active = false;
    is_scrolling = false;

    /* Reset widget pointers */
    lbl_hour = NULL;
    lbl_minute = NULL;
    lbl_second = NULL;
    lbl_date = NULL;
    lbl_weekday = NULL;
    arc_steps = NULL;
    lbl_steps_val = NULL;
    lbl_steps_unit = NULL;
    scroll_cont = NULL;
    page_activity = NULL;
}

#endif /*LV_USE_DEMO_LITEWATCH*/
