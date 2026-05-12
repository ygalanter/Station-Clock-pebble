#include <pebble.h>
#include <stdlib.h>
#include <string.h>

#define ROW_H 76
#define HALF_W 100
#define GLYPH_SPACING 1   // px between adjacent glyphs
#define TAP_DEBOUNCE_SECONDS 2
#define MAX_TRACKING_TIMEOUT_SECONDS 300

typedef enum {
    VIEW_MAIN = 0,
    VIEW_TRACKING = 1
} ViewMode;

// ----- Glyph sets (bitmaps loaded once) -----

typedef struct {
    char ch;
    GBitmap *bmp;
} GlyphEntry;

#define G_BIG_COUNT   11
#define G_MED_COUNT   40  // MR(14) + MG(11) + MW(15)
#define G_SMALL_COUNT 14

// BIG white — time
static GlyphEntry s_glyph_big[G_BIG_COUNT];
// MED with multiple colors — day(red), date(green), tracking(white)
static GlyphEntry s_glyph_red[14];
static GlyphEntry s_glyph_green[11];
static GlyphEntry s_glyph_white_med[15];
// SMALL yellow — temperature
static GlyphEntry s_glyph_yellow[G_SMALL_COUNT];

typedef enum {
    GS_TIME,         // BIG white
    GS_DAY,          // MED red letters
    GS_DATE,         // MED green digits + /
    GS_TRACKING,     // MED white digits + . : M I
    GS_TEMP          // SMALL yellow digits + - * F
} GlyphSet;

static GBitmap* glyph_lookup(GlyphSet gs, char ch) {
    GlyphEntry *t = NULL; int n = 0;
    switch (gs) {
        case GS_TIME:     t = s_glyph_big;       n = G_BIG_COUNT; break;
        case GS_DAY:      t = s_glyph_red;       n = 14; break;
        case GS_DATE:     t = s_glyph_green;     n = 11; break;
        case GS_TRACKING: t = s_glyph_white_med; n = 15; break;
        case GS_TEMP:     t = s_glyph_yellow;    n = G_SMALL_COUNT; break;
    }
    for (int i = 0; i < n; i++) if (t[i].ch == ch) return t[i].bmp;
    return NULL;
}

static int text_width(GlyphSet gs, const char *s) {
    int w = 0, n = 0;
    for (const char *p = s; *p; p++) {
        GBitmap *b = glyph_lookup(gs, *p);
        if (b) { w += gbitmap_get_bounds(b).size.w; n++; }
    }
    if (n > 1) w += (n - 1) * GLYPH_SPACING;
    return w;
}

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } Align;

static void draw_text_glyphs(GContext *ctx, GRect frame, GlyphSet gs, const char *s, Align align) {
    int total = text_width(gs, s);
    int x;
    if (align == ALIGN_CENTER) x = frame.origin.x + (frame.size.w - total) / 2;
    else if (align == ALIGN_RIGHT) x = frame.origin.x + frame.size.w - total;
    else x = frame.origin.x;

    // Find max glyph height for vertical centering
    int max_h = 0;
    for (const char *p = s; *p; p++) {
        GBitmap *b = glyph_lookup(gs, *p);
        if (b) {
            int h = gbitmap_get_bounds(b).size.h;
            if (h > max_h) max_h = h;
        }
    }
    int y_base = frame.origin.y + (frame.size.h - max_h) / 2;

    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    for (const char *p = s; *p; p++) {
        GBitmap *b = glyph_lookup(gs, *p);
        if (!b) continue;
        GRect gb = gbitmap_get_bounds(b);
        // bottom-align each glyph with max_h baseline
        int y = y_base + (max_h - gb.size.h);
        graphics_draw_bitmap_in_rect(ctx, b, GRect(x, y, gb.size.w, gb.size.h));
        x += gb.size.w + GLYPH_SPACING;
    }
}

// ----- State -----

static Window *s_window;
static Layer *s_bg_layer;

static char s_time_buf[8];
static char s_day_buf[8];
static char s_date_buf[16];
static char s_temp_buf[16];
static char s_steps_buf[12];
static char s_dist_buf[16];
static char s_am_buf[12];
static char s_cals_buf[12];

static int s_temperature = -999;
static int s_weather_code = -1;
static int s_is_day = 1;

static ViewMode s_view = VIEW_MAIN;
static uint16_t s_tracking_display_timeout = 0;
static AppTimer *s_tracking_view_timer = NULL;
static time_t s_last_tap_time = 0;
static bool s_use_fahrenheit = true;
static bool s_use_miles = true;
static bool s_show_battery = false;
static uint8_t s_battery_level = 0;

// Text-rendering layers
static Layer *s_time_layer;
static Layer *s_temp_layer;
static Layer *s_day_layer;
static Layer *s_date_layer;
static Layer *s_steps_layer;
static Layer *s_dist_layer;
static Layer *s_am_layer;
static Layer *s_cals_layer;

// Battery indicator
static Layer *s_battery_layer;

// Weather + tracking icons
static BitmapLayer *s_weather_icon_layer;
static GBitmap *s_weather_bitmap;
static Layer *s_tracking_layer;
static BitmapLayer *s_steps_icon, *s_dist_icon, *s_am_icon, *s_cals_icon;
static GBitmap *s_steps_bitmap, *s_dist_bitmap, *s_am_bitmap, *s_cals_bitmap;

// ----- Background bands -----

static void fill_dithered_time_band(GContext *ctx, int width) {
    // Pebble's neutral palette has no color between black and dark gray.
    // Use sparse dark-gray pixels over black to approximate an in-between tone.
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(0, 0, width, ROW_H), 0, GCornerNone);

    graphics_context_set_fill_color(ctx, GColorDarkGray);
    for (int y = 0; y < ROW_H; y += 2) {
        for (int x = 0; x < width; x += 2) {
            graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
        }
    }
}

static void bg_update_proc(Layer *layer, GContext *ctx) {
    GRect b = layer_get_bounds(layer);
    fill_dithered_time_band(ctx, b.size.w);
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx, GRect(0, ROW_H, HALF_W, ROW_H), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, GColorArmyGreen);
    graphics_fill_rect(ctx, GRect(HALF_W, ROW_H, HALF_W, ROW_H), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, GColorBulgarianRose);
    graphics_fill_rect(ctx, GRect(0, ROW_H * 2, HALF_W, ROW_H), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, GColorDarkGreen);
    graphics_fill_rect(ctx, GRect(HALF_W, ROW_H * 2, HALF_W, ROW_H), 0, GCornerNone);
}

static void tracking_update_proc(Layer *layer, GContext *ctx) {
    GRect b = layer_get_bounds(layer);
    int rh = b.size.h / 4;
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx, GRect(0, 0,       b.size.w, rh), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(0, rh * 2,  b.size.w, rh), 0, GCornerNone);
}

// ----- Text layer update_procs -----

static void time_update(Layer *l, GContext *ctx)  { draw_text_glyphs(ctx, layer_get_bounds(l), GS_TIME, s_time_buf,  ALIGN_CENTER); }
static void temp_update(Layer *l, GContext *ctx)  { draw_text_glyphs(ctx, layer_get_bounds(l), GS_TEMP, s_temp_buf,  ALIGN_CENTER); }
static void day_update(Layer *l, GContext *ctx)   { draw_text_glyphs(ctx, layer_get_bounds(l), GS_DAY,  s_day_buf,   ALIGN_CENTER); }
static void date_update(Layer *l, GContext *ctx)  { draw_text_glyphs(ctx, layer_get_bounds(l), GS_DATE, s_date_buf,  ALIGN_CENTER); }
static void steps_update(Layer *l, GContext *ctx) { draw_text_glyphs(ctx, layer_get_bounds(l), GS_TRACKING, s_steps_buf, ALIGN_RIGHT); }
static void dist_update(Layer *l, GContext *ctx)  { draw_text_glyphs(ctx, layer_get_bounds(l), GS_TRACKING, s_dist_buf,  ALIGN_RIGHT); }
static void am_update(Layer *l, GContext *ctx)    { draw_text_glyphs(ctx, layer_get_bounds(l), GS_TRACKING, s_am_buf,    ALIGN_RIGHT); }
static void cals_update(Layer *l, GContext *ctx)  { draw_text_glyphs(ctx, layer_get_bounds(l), GS_TRACKING, s_cals_buf,  ALIGN_RIGHT); }

// ----- Weather mapping -----

static uint32_t resolve_weather_resource(int code, int is_day) {
    if (code < 0) return RESOURCE_ID_IMG_W_WAIT;
    if (code == 0) return is_day ? RESOURCE_ID_IMG_W_CLEAR : RESOURCE_ID_IMG_W_CLEAR_NIGHT;
    if (code == 1 || code == 2) return is_day ? RESOURCE_ID_IMG_W_PARTLY_CLOUDY
                                              : RESOURCE_ID_IMG_W_PARTLY_CLOUDY_NIGHT;
    if (code == 3) return is_day ? RESOURCE_ID_IMG_W_SCATTERED : RESOURCE_ID_IMG_W_CLOUDY_NIGHT;
    if (code == 45 || code == 48) return RESOURCE_ID_IMG_W_FOG;
    if (code >= 51 && code <= 55) return RESOURCE_ID_IMG_W_RAIN;
    if (code == 56 || code == 57) return RESOURCE_ID_IMG_W_ICE;
    if (code == 61 || code == 63) return RESOURCE_ID_IMG_W_RAIN;
    if (code == 65) return RESOURCE_ID_IMG_W_RAIN_HEAVY;
    if (code == 66 || code == 67) return RESOURCE_ID_IMG_W_ICE;
    if (code >= 71 && code <= 77) return RESOURCE_ID_IMG_W_SNOW;
    if (code == 80 || code == 81) return RESOURCE_ID_IMG_W_RAIN;
    if (code == 82) return RESOURCE_ID_IMG_W_RAIN_HEAVY;
    if (code == 85 || code == 86) return RESOURCE_ID_IMG_W_SNOW;
    if (code >= 95 && code <= 99) return RESOURCE_ID_IMG_W_STORM;
    return RESOURCE_ID_IMG_W_UNKNOWN;
}

static void update_weather_icon(void) {
    if (s_weather_bitmap) gbitmap_destroy(s_weather_bitmap);
    s_weather_bitmap = gbitmap_create_with_resource(resolve_weather_resource(s_weather_code, s_is_day));
    bitmap_layer_set_bitmap(s_weather_icon_layer, s_weather_bitmap);
}

// ----- Battery indicator -----

#define BATTERY_LINE_HEIGHT 3

static void battery_update_proc(Layer *layer, GContext *ctx) {
    GRect b = layer_get_bounds(layer);
    int bar_w = (s_battery_level * b.size.w) / 100;

    GColor color;
    if (s_battery_level > 75) color = GColorGreen;
    else if (s_battery_level > 50) color = GColorYellow;
    else if (s_battery_level > 25) color = GColorOrange;
    else color = GColorRed;

    graphics_context_set_fill_color(ctx, color);
    graphics_fill_rect(ctx, GRect(0, 0, bar_w, b.size.h), 0, GCornerNone);
}

static void battery_handler(BatteryChargeState charge) {
    s_battery_level = charge.charge_percent;
    if (s_battery_layer) layer_mark_dirty(s_battery_layer);
}

// ----- Time / date update -----

static void update_time(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (!t) return;

    if (clock_is_24h_style()) {
        strftime(s_time_buf, sizeof(s_time_buf), "%H:%M", t);
    } else {
        strftime(s_time_buf, sizeof(s_time_buf), "%I:%M", t);
        if (s_time_buf[0] == '0') {
            // shift left to drop leading zero
            memmove(s_time_buf, s_time_buf + 1, strlen(s_time_buf));
        }
    }
    layer_mark_dirty(s_time_layer);

    strftime(s_day_buf, sizeof(s_day_buf), "%a", t);
    for (char *p = s_day_buf; *p; p++) {
        if (*p >= 'a' && *p <= 'z') *p -= 32;
    }
    layer_mark_dirty(s_day_layer);

    snprintf(s_date_buf, sizeof(s_date_buf), "%d/%d", t->tm_mon + 1, t->tm_mday);
    layer_mark_dirty(s_date_layer);
}

// ----- Health / tracking -----

#if defined(PBL_HEALTH)
static void refresh_health(void) {
    time_t start = time_start_of_today();
    time_t end = time(NULL);

    HealthServiceAccessibilityMask mask = health_service_metric_accessible(HealthMetricStepCount, start, end);
    int steps = (mask & HealthServiceAccessibilityMaskAvailable)
                ? (int)health_service_sum_today(HealthMetricStepCount) : 0;
    snprintf(s_steps_buf, sizeof(s_steps_buf), "%d", steps);

    mask = health_service_metric_accessible(HealthMetricWalkedDistanceMeters, start, end);
    int meters = (mask & HealthServiceAccessibilityMaskAvailable)
                 ? (int)health_service_sum_today(HealthMetricWalkedDistanceMeters) : 0;
    if (s_use_miles) {
        int tenth_mi = (meters * 10) / 1609;
        snprintf(s_dist_buf, sizeof(s_dist_buf), "%d.%dMI", tenth_mi / 10, tenth_mi % 10);
    } else {
        int tenth_km = meters / 100;
        snprintf(s_dist_buf, sizeof(s_dist_buf), "%d.%dKM", tenth_km / 10, tenth_km % 10);
    }

    mask = health_service_metric_accessible(HealthMetricActiveSeconds, start, end);
    int active_sec = (mask & HealthServiceAccessibilityMaskAvailable)
                     ? (int)health_service_sum_today(HealthMetricActiveSeconds) : 0;
    int am_total = active_sec / 60;
    snprintf(s_am_buf, sizeof(s_am_buf), "%02d:%02d", am_total / 60, am_total % 60);

    int cals = 0;
    mask = health_service_metric_accessible(HealthMetricActiveKCalories, start, end);
    if (mask & HealthServiceAccessibilityMaskAvailable)
        cals += (int)health_service_sum_today(HealthMetricActiveKCalories);
    mask = health_service_metric_accessible(HealthMetricRestingKCalories, start, end);
    if (mask & HealthServiceAccessibilityMaskAvailable)
        cals += (int)health_service_sum_today(HealthMetricRestingKCalories);
    snprintf(s_cals_buf, sizeof(s_cals_buf), "%d", cals);

    layer_mark_dirty(s_steps_layer);
    layer_mark_dirty(s_dist_layer);
    layer_mark_dirty(s_am_layer);
    layer_mark_dirty(s_cals_layer);
}
#else
static void refresh_health(void) {
    snprintf(s_steps_buf, sizeof(s_steps_buf), "0");
    snprintf(s_dist_buf,  sizeof(s_dist_buf),  s_use_miles ? "0.0MI" : "0.0KM");
    snprintf(s_am_buf,    sizeof(s_am_buf),    "00:00");
    snprintf(s_cals_buf,  sizeof(s_cals_buf),  "0");
    layer_mark_dirty(s_steps_layer);
    layer_mark_dirty(s_dist_layer);
    layer_mark_dirty(s_am_layer);
    layer_mark_dirty(s_cals_layer);
}
#endif

// ----- View switching -----

static uint16_t clamp_timeout_seconds(int value) {
    if (value < 0) return 0;
    if (value > MAX_TRACKING_TIMEOUT_SECONDS) return MAX_TRACKING_TIMEOUT_SECONDS;
    return (uint16_t)value;
}

static uint16_t parse_timeout_tuple(Tuple *t) {
    if (!t) return 0;
    if (t->type == TUPLE_CSTRING) {
        return clamp_timeout_seconds(atoi(t->value->cstring));
    }
    return clamp_timeout_seconds((int)t->value->int32);
}

static bool parse_bool_tuple(Tuple *t, bool fallback) {
    if (!t) return fallback;
    if (t->type == TUPLE_CSTRING) {
        const char *v = t->value->cstring;
        if (!v) return fallback;
        if (strcmp(v, "true") == 0 || strcmp(v, "TRUE") == 0) return true;
        if (strcmp(v, "false") == 0 || strcmp(v, "FALSE") == 0) return false;
        return atoi(v) != 0;
    }
    return t->value->uint8 != 0;
}

static void cancel_tracking_timer(void) {
    if (s_tracking_view_timer) {
        app_timer_cancel(s_tracking_view_timer);
        s_tracking_view_timer = NULL;
    }
}

static void return_to_main_display(void *ctx);

static void schedule_tracking_timer(void) {
    cancel_tracking_timer();
    if (s_tracking_display_timeout > 0) {
        s_tracking_view_timer = app_timer_register(
            s_tracking_display_timeout * 1000, return_to_main_display, NULL
        );
    }
}

static void show_view(ViewMode v) {
    s_view = v;
    bool main_hidden = (v != VIEW_MAIN);
    bool track_hidden = (v != VIEW_TRACKING);
    layer_set_hidden(s_bg_layer, main_hidden);
    layer_set_hidden(s_time_layer, main_hidden);
    layer_set_hidden(s_day_layer, main_hidden);
    layer_set_hidden(s_date_layer, main_hidden);
    layer_set_hidden(s_temp_layer, main_hidden);
    layer_set_hidden(bitmap_layer_get_layer(s_weather_icon_layer), main_hidden);

    layer_set_hidden(s_tracking_layer, track_hidden);
    layer_set_hidden(s_steps_layer, track_hidden);
    layer_set_hidden(s_dist_layer,  track_hidden);
    layer_set_hidden(s_am_layer,    track_hidden);
    layer_set_hidden(s_cals_layer,  track_hidden);
    layer_set_hidden(bitmap_layer_get_layer(s_steps_icon), track_hidden);
    layer_set_hidden(bitmap_layer_get_layer(s_dist_icon),  track_hidden);
    layer_set_hidden(bitmap_layer_get_layer(s_am_icon),    track_hidden);
    layer_set_hidden(bitmap_layer_get_layer(s_cals_icon),  track_hidden);

    if (v == VIEW_TRACKING) {
        refresh_health();
        schedule_tracking_timer();
    } else {
        cancel_tracking_timer();
    }
}

static void return_to_main_display(void *ctx) {
    (void)ctx;
    s_tracking_view_timer = NULL;
    if (s_view == VIEW_TRACKING) {
        show_view(VIEW_MAIN);
    }
}

static void refresh_temperature_display(void) {
    if (s_temperature > -900) {
        int shown = s_temperature;
        if (s_use_fahrenheit) {
            // Signed rounding to nearest integer for C->F conversion.
            shown = ((s_temperature * 9) + (s_temperature >= 0 ? 2 : -2)) / 5 + 32;
        }
        snprintf(s_temp_buf, sizeof(s_temp_buf), "%d*%c", shown, s_use_fahrenheit ? 'F' : 'C');
    } else {
        snprintf(s_temp_buf, sizeof(s_temp_buf), "--*%c", s_use_fahrenheit ? 'F' : 'C');
    }
    layer_mark_dirty(s_temp_layer);
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
    (void)axis;
    (void)direction;
    time_t now = time(NULL);
    if (now - s_last_tap_time < TAP_DEBOUNCE_SECONDS) {
        return;
    }
    s_last_tap_time = now;
    show_view(s_view == VIEW_MAIN ? VIEW_TRACKING : VIEW_MAIN);
}

// ----- AppMessage -----

static void request_weather(void) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
        dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
        app_message_outbox_send();
    }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    update_time();
    if (s_view == VIEW_TRACKING) refresh_health();
    if (tick_time->tm_min % 30 == 0) request_weather();
}

static void inbox_received(DictionaryIterator *iter, void *ctx) {
    Tuple *t_temp = dict_find(iter, MESSAGE_KEY_TEMPERATURE);
    Tuple *t_code = dict_find(iter, MESSAGE_KEY_WEATHER_CODE);
    Tuple *t_day  = dict_find(iter, MESSAGE_KEY_IS_DAY);
    Tuple *t_timeout = dict_find(iter, MESSAGE_KEY_HEALTH_DISPLAY_TIMEOUT);
    Tuple *t_temp_unit = dict_find(iter, MESSAGE_KEY_TEMP_UNIT_IS_F);
    Tuple *t_dist_unit = dict_find(iter, MESSAGE_KEY_DIST_UNIT_IS_MI);
    Tuple *t_show_batt = dict_find(iter, MESSAGE_KEY_SHOW_BATTERY);

    bool weather_changed = false;
    if (t_temp) s_temperature = (int)t_temp->value->int32;
    if (t_code) {
        s_weather_code = (int)t_code->value->int32;
        weather_changed = true;
    }
    if (t_day) {
        s_is_day = (int)t_day->value->int32;
        weather_changed = true;
    }
    if (t_temp_unit) {
        s_use_fahrenheit = parse_bool_tuple(t_temp_unit, true);
        persist_write_int(MESSAGE_KEY_TEMP_UNIT_IS_F, s_use_fahrenheit ? 1 : 0);
        refresh_temperature_display();
    }
    if (t_dist_unit) {
        s_use_miles = parse_bool_tuple(t_dist_unit, true);
        persist_write_int(MESSAGE_KEY_DIST_UNIT_IS_MI, s_use_miles ? 1 : 0);
        if (s_view == VIEW_TRACKING) refresh_health();
    }
    if (t_timeout) {
        s_tracking_display_timeout = parse_timeout_tuple(t_timeout);
        persist_write_int(MESSAGE_KEY_HEALTH_DISPLAY_TIMEOUT, s_tracking_display_timeout);
        if (s_view == VIEW_TRACKING) {
            schedule_tracking_timer();
        } else {
            cancel_tracking_timer();
        }
    }

    if (t_show_batt) {
        s_show_battery = parse_bool_tuple(t_show_batt, false);
        persist_write_int(MESSAGE_KEY_SHOW_BATTERY, s_show_battery ? 1 : 0);
        if (s_battery_layer) {
            layer_set_hidden(s_battery_layer, !s_show_battery);
            if (s_show_battery) layer_mark_dirty(s_battery_layer);
        }
    }

    if (t_temp) refresh_temperature_display();
    if (weather_changed) update_weather_icon();
}

static void inbox_dropped(AppMessageResult reason, void *ctx) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

// ----- Glyph loading -----

static void load_big(void) {
    static const char chs[] = "0123456789:";
    static const uint32_t ids[] = {
        RESOURCE_ID_IMG_GLYPH_BW_D0, RESOURCE_ID_IMG_GLYPH_BW_D1, RESOURCE_ID_IMG_GLYPH_BW_D2,
        RESOURCE_ID_IMG_GLYPH_BW_D3, RESOURCE_ID_IMG_GLYPH_BW_D4, RESOURCE_ID_IMG_GLYPH_BW_D5,
        RESOURCE_ID_IMG_GLYPH_BW_D6, RESOURCE_ID_IMG_GLYPH_BW_D7, RESOURCE_ID_IMG_GLYPH_BW_D8,
        RESOURCE_ID_IMG_GLYPH_BW_D9, RESOURCE_ID_IMG_GLYPH_BW_COLON
    };
    for (int i = 0; i < G_BIG_COUNT; i++) {
        s_glyph_big[i].ch = chs[i];
        s_glyph_big[i].bmp = gbitmap_create_with_resource(ids[i]);
    }
}

static void load_red(void) {
    static const char chs[] = "AEDFHIMNORSTUW";
    static const uint32_t ids[] = {
        RESOURCE_ID_IMG_GLYPH_MR_A, RESOURCE_ID_IMG_GLYPH_MR_E, RESOURCE_ID_IMG_GLYPH_MR_D,
        RESOURCE_ID_IMG_GLYPH_MR_F, RESOURCE_ID_IMG_GLYPH_MR_H, RESOURCE_ID_IMG_GLYPH_MR_I,
        RESOURCE_ID_IMG_GLYPH_MR_M, RESOURCE_ID_IMG_GLYPH_MR_N, RESOURCE_ID_IMG_GLYPH_MR_O,
        RESOURCE_ID_IMG_GLYPH_MR_R, RESOURCE_ID_IMG_GLYPH_MR_S, RESOURCE_ID_IMG_GLYPH_MR_T,
        RESOURCE_ID_IMG_GLYPH_MR_U, RESOURCE_ID_IMG_GLYPH_MR_W
    };
    for (int i = 0; i < 14; i++) {
        s_glyph_red[i].ch = chs[i];
        s_glyph_red[i].bmp = gbitmap_create_with_resource(ids[i]);
    }
}

static void load_green(void) {
    static const char chs[] = "0123456789/";
    static const uint32_t ids[] = {
        RESOURCE_ID_IMG_GLYPH_MG_D0, RESOURCE_ID_IMG_GLYPH_MG_D1, RESOURCE_ID_IMG_GLYPH_MG_D2,
        RESOURCE_ID_IMG_GLYPH_MG_D3, RESOURCE_ID_IMG_GLYPH_MG_D4, RESOURCE_ID_IMG_GLYPH_MG_D5,
        RESOURCE_ID_IMG_GLYPH_MG_D6, RESOURCE_ID_IMG_GLYPH_MG_D7, RESOURCE_ID_IMG_GLYPH_MG_D8,
        RESOURCE_ID_IMG_GLYPH_MG_D9, RESOURCE_ID_IMG_GLYPH_MG_SLASH
    };
    for (int i = 0; i < 11; i++) {
        s_glyph_green[i].ch = chs[i];
        s_glyph_green[i].bmp = gbitmap_create_with_resource(ids[i]);
    }
}

static void load_white_med(void) {
    static const char chs[] = "0123456789:.MIK";
    static const uint32_t ids[] = {
        RESOURCE_ID_IMG_GLYPH_MW_D0, RESOURCE_ID_IMG_GLYPH_MW_D1, RESOURCE_ID_IMG_GLYPH_MW_D2,
        RESOURCE_ID_IMG_GLYPH_MW_D3, RESOURCE_ID_IMG_GLYPH_MW_D4, RESOURCE_ID_IMG_GLYPH_MW_D5,
        RESOURCE_ID_IMG_GLYPH_MW_D6, RESOURCE_ID_IMG_GLYPH_MW_D7, RESOURCE_ID_IMG_GLYPH_MW_D8,
        RESOURCE_ID_IMG_GLYPH_MW_D9, RESOURCE_ID_IMG_GLYPH_MW_COLON,
        RESOURCE_ID_IMG_GLYPH_MW_DOT, RESOURCE_ID_IMG_GLYPH_MW_M, RESOURCE_ID_IMG_GLYPH_MW_I,
        RESOURCE_ID_IMG_GLYPH_MW_K
    };
    for (int i = 0; i < 15; i++) {
        s_glyph_white_med[i].ch = chs[i];
        s_glyph_white_med[i].bmp = gbitmap_create_with_resource(ids[i]);
    }
}

static void load_yellow(void) {
    static const char chs[] = "0123456789-*FC";
    static const uint32_t ids[] = {
        RESOURCE_ID_IMG_GLYPH_SY_D0, RESOURCE_ID_IMG_GLYPH_SY_D1, RESOURCE_ID_IMG_GLYPH_SY_D2,
        RESOURCE_ID_IMG_GLYPH_SY_D3, RESOURCE_ID_IMG_GLYPH_SY_D4, RESOURCE_ID_IMG_GLYPH_SY_D5,
        RESOURCE_ID_IMG_GLYPH_SY_D6, RESOURCE_ID_IMG_GLYPH_SY_D7, RESOURCE_ID_IMG_GLYPH_SY_D8,
        RESOURCE_ID_IMG_GLYPH_SY_D9, RESOURCE_ID_IMG_GLYPH_SY_DASH,
        RESOURCE_ID_IMG_GLYPH_SY_DEG, RESOURCE_ID_IMG_GLYPH_SY_F, RESOURCE_ID_IMG_GLYPH_SY_C
    };
    for (int i = 0; i < G_SMALL_COUNT; i++) {
        s_glyph_yellow[i].ch = chs[i];
        s_glyph_yellow[i].bmp = gbitmap_create_with_resource(ids[i]);
    }
}

static void unload_glyphs(void) {
    for (int i = 0; i < G_BIG_COUNT; i++) if (s_glyph_big[i].bmp) gbitmap_destroy(s_glyph_big[i].bmp);
    for (int i = 0; i < 14; i++) if (s_glyph_red[i].bmp) gbitmap_destroy(s_glyph_red[i].bmp);
    for (int i = 0; i < 11; i++) if (s_glyph_green[i].bmp) gbitmap_destroy(s_glyph_green[i].bmp);
    for (int i = 0; i < 15; i++) if (s_glyph_white_med[i].bmp) gbitmap_destroy(s_glyph_white_med[i].bmp);
    for (int i = 0; i < G_SMALL_COUNT; i++) if (s_glyph_yellow[i].bmp) gbitmap_destroy(s_glyph_yellow[i].bmp);
}

// ----- Window -----

static void window_load(Window *win) {
    Layer *root = window_get_root_layer(win);
    GRect b = layer_get_bounds(root);

    load_big();
    load_red();
    load_green();
    load_white_med();
    load_yellow();

    s_bg_layer = layer_create(b);
    layer_set_update_proc(s_bg_layer, bg_update_proc);
    layer_add_child(root, s_bg_layer);

    // MAIN VIEW
    s_time_layer = layer_create(GRect(0, 0, b.size.w, ROW_H));
    layer_set_update_proc(s_time_layer, time_update);
    layer_add_child(root, s_time_layer);

    int wx = (HALF_W - 90) / 2;
    int wy = ROW_H + (ROW_H - 62) / 2;
    s_weather_icon_layer = bitmap_layer_create(GRect(wx, wy, 90, 62));
    bitmap_layer_set_compositing_mode(s_weather_icon_layer, GCompOpSet);
    layer_add_child(root, bitmap_layer_get_layer(s_weather_icon_layer));

    s_temp_layer = layer_create(GRect(HALF_W, ROW_H, HALF_W, ROW_H));
    layer_set_update_proc(s_temp_layer, temp_update);
    layer_add_child(root, s_temp_layer);

    s_day_layer = layer_create(GRect(0, ROW_H * 2, HALF_W, ROW_H));
    layer_set_update_proc(s_day_layer, day_update);
    layer_add_child(root, s_day_layer);

    s_date_layer = layer_create(GRect(HALF_W, ROW_H * 2, HALF_W, ROW_H));
    layer_set_update_proc(s_date_layer, date_update);
    layer_add_child(root, s_date_layer);

    // TRACKING VIEW
    s_tracking_layer = layer_create(b);
    layer_set_update_proc(s_tracking_layer, tracking_update_proc);
    layer_add_child(root, s_tracking_layer);

    int rh = b.size.h / 4;
    int icon_x = 15;
    int icon_y_off = (rh - 42) / 2;

    s_steps_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_STEPS);
    s_dist_bitmap  = gbitmap_create_with_resource(RESOURCE_ID_IMG_DIST);
    s_am_bitmap    = gbitmap_create_with_resource(RESOURCE_ID_IMG_AM);
    s_cals_bitmap  = gbitmap_create_with_resource(RESOURCE_ID_IMG_CALS);

    s_steps_icon = bitmap_layer_create(GRect(icon_x, 0 * rh + icon_y_off, 42, 42));
    s_dist_icon  = bitmap_layer_create(GRect(icon_x, 1 * rh + icon_y_off, 42, 42));
    s_am_icon    = bitmap_layer_create(GRect(icon_x, 2 * rh + icon_y_off, 42, 42));
    s_cals_icon  = bitmap_layer_create(GRect(icon_x, 3 * rh + icon_y_off, 42, 42));

    bitmap_layer_set_compositing_mode(s_steps_icon, GCompOpSet);
    bitmap_layer_set_compositing_mode(s_dist_icon,  GCompOpSet);
    bitmap_layer_set_compositing_mode(s_am_icon,    GCompOpSet);
    bitmap_layer_set_compositing_mode(s_cals_icon,  GCompOpSet);

    bitmap_layer_set_bitmap(s_steps_icon, s_steps_bitmap);
    bitmap_layer_set_bitmap(s_dist_icon,  s_dist_bitmap);
    bitmap_layer_set_bitmap(s_am_icon,    s_am_bitmap);
    bitmap_layer_set_bitmap(s_cals_icon,  s_cals_bitmap);

    layer_add_child(root, bitmap_layer_get_layer(s_steps_icon));
    layer_add_child(root, bitmap_layer_get_layer(s_dist_icon));
    layer_add_child(root, bitmap_layer_get_layer(s_am_icon));
    layer_add_child(root, bitmap_layer_get_layer(s_cals_icon));

    int val_x = 65;
    int val_w = b.size.w - val_x - 8;

    s_steps_layer = layer_create(GRect(val_x, 0 * rh, val_w, rh));
    s_dist_layer  = layer_create(GRect(val_x, 1 * rh, val_w, rh));
    s_am_layer    = layer_create(GRect(val_x, 2 * rh, val_w, rh));
    s_cals_layer  = layer_create(GRect(val_x, 3 * rh, val_w, rh));
    layer_set_update_proc(s_steps_layer, steps_update);
    layer_set_update_proc(s_dist_layer,  dist_update);
    layer_set_update_proc(s_am_layer,    am_update);
    layer_set_update_proc(s_cals_layer,  cals_update);
    layer_add_child(root, s_steps_layer);
    layer_add_child(root, s_dist_layer);
    layer_add_child(root, s_am_layer);
    layer_add_child(root, s_cals_layer);

    // Battery indicator (added last so it draws on top of all views)
    s_battery_layer = layer_create(GRect(0, 0, b.size.w, BATTERY_LINE_HEIGHT));
    layer_set_update_proc(s_battery_layer, battery_update_proc);
    layer_set_hidden(s_battery_layer, !s_show_battery);
    layer_add_child(root, s_battery_layer);

    // Initial
    refresh_temperature_display();
    update_time();
    update_weather_icon();
    show_view(VIEW_MAIN);
}

static void window_unload(Window *win) {
    layer_destroy(s_time_layer);
    layer_destroy(s_temp_layer);
    layer_destroy(s_day_layer);
    layer_destroy(s_date_layer);
    layer_destroy(s_steps_layer);
    layer_destroy(s_dist_layer);
    layer_destroy(s_am_layer);
    layer_destroy(s_cals_layer);

    bitmap_layer_destroy(s_weather_icon_layer);
    if (s_weather_bitmap) gbitmap_destroy(s_weather_bitmap);

    bitmap_layer_destroy(s_steps_icon);
    bitmap_layer_destroy(s_dist_icon);
    bitmap_layer_destroy(s_am_icon);
    bitmap_layer_destroy(s_cals_icon);
    gbitmap_destroy(s_steps_bitmap);
    gbitmap_destroy(s_dist_bitmap);
    gbitmap_destroy(s_am_bitmap);
    gbitmap_destroy(s_cals_bitmap);

    layer_destroy(s_tracking_layer);
    layer_destroy(s_bg_layer);
    layer_destroy(s_battery_layer);

    unload_glyphs();
}

static void init(void) {
    s_tracking_display_timeout = persist_exists(MESSAGE_KEY_HEALTH_DISPLAY_TIMEOUT)
        ? clamp_timeout_seconds(persist_read_int(MESSAGE_KEY_HEALTH_DISPLAY_TIMEOUT))
        : 0;
    s_use_fahrenheit = persist_exists(MESSAGE_KEY_TEMP_UNIT_IS_F)
        ? persist_read_int(MESSAGE_KEY_TEMP_UNIT_IS_F) != 0
        : true;
    s_use_miles = persist_exists(MESSAGE_KEY_DIST_UNIT_IS_MI)
        ? persist_read_int(MESSAGE_KEY_DIST_UNIT_IS_MI) != 0
        : true;
    s_show_battery = persist_exists(MESSAGE_KEY_SHOW_BATTERY)
        ? persist_read_int(MESSAGE_KEY_SHOW_BATTERY) != 0
        : false;

    s_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_window_handlers(s_window, (WindowHandlers){
        .load = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    accel_tap_service_subscribe(tap_handler);
    battery_state_service_subscribe(battery_handler);
    s_battery_level = battery_state_service_peek().charge_percent;

    app_message_register_inbox_received(inbox_received);
    app_message_register_inbox_dropped(inbox_dropped);
    app_message_open(256, 64);
}

static void deinit(void) {
    cancel_tracking_timer();
    battery_state_service_unsubscribe();
    accel_tap_service_unsubscribe();
    tick_timer_service_unsubscribe();
    window_destroy(s_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}
