#include <pebble.h>

#define SAMPLES 10

#define SPLASH RESOURCE_ID_HACKDADS

static const SmartstrapServiceId SERVICE_ID = 0x1001;
static const SmartstrapAttributeId LED_ATTRIBUTE_ID = 0x0001;
static const size_t LED_ATTRIBUTE_LENGTH = 1;
static const SmartstrapAttributeId UPTIME_ATTRIBUTE_ID = 0x0002;
static const size_t UPTIME_ATTRIBUTE_LENGTH = 4;

static const SmartstrapAttributeId ALTITUDE_ATTRIBUTE_ID = 0x0003;
static const size_t ALTITUDE_ATTRIBUTE_LENGTH = 4;

static SmartstrapAttribute *led_attribute;
static SmartstrapAttribute *uptime_attribute;

static SmartstrapAttribute *altitude_attribute;

static Window *window;
static TextLayer *status_text_layer;
static TextLayer *uptime_text_layer;

static TextLayer *altitude_text_layer;
static AppTimer *altitude_timer;

static TextLayer *time_layer;

static int altitude_samples[SAMPLES];
static int altitude_sample_index = 0;
static bool first_altitude_sample_index = true;
static int altitude_sample_previous = 0;
static int altitude_delta = 0;

static Layer *canvas_layer;


static GPath *up_path_ptr = NULL;
static const GPathInfo UP_PATH_INFO = {
  .num_points = 3,
  .points = (GPoint []) {{5, 40}, {72, 5}, {138, 40}}
};

static GPath *down_path_ptr = NULL;
static const GPathInfo DOWN_PATH_INFO = {
  .num_points = 3,
  .points = (GPoint []) {{5, 108}, {72, 133}, {138, 108}}
};
// TODO: add rectangle to path too? or make "real" arrow even?


static AppTimer *splash_timer;

static BitmapLayer *splash_layer;
static GBitmap *splash_bitmap;


static Window *graph_window;
static AppTimer *graph_timer;



static void prv_availability_changed(SmartstrapServiceId service_id, bool available) {
  if (service_id != SERVICE_ID) {
    return;
  }

  if (available) {
    text_layer_set_background_color(status_text_layer, GColorGreen);
    text_layer_set_text(status_text_layer, "Connected!");
  } else {
    text_layer_set_background_color(status_text_layer, GColorRed);
    text_layer_set_text(status_text_layer, "Disonnected!");
  }
}

static void prv_did_read(SmartstrapAttribute *attr, SmartstrapResult result,
                         const uint8_t *data, size_t length) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "prv_did_read()");

  /*
  TODO: restore
  if ((attr != uptime_attribute) & (attr != altitude_attribute)) {
    return;
  }
  */
  if (result != SmartstrapResultOk) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Read failed with result %d", result);
    return;
  }
  if (length != UPTIME_ATTRIBUTE_LENGTH) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Got response of unexpected length (%d)", length);
    return;
  }

  if (attr == uptime_attribute) {
    static char uptime_buffer[20];
    snprintf(uptime_buffer, 20, "%u", (unsigned int)*(uint32_t *)data);
    text_layer_set_text(uptime_text_layer, uptime_buffer);
  }

  APP_LOG(APP_LOG_LEVEL_DEBUG, "attr: %u", smartstrap_attribute_get_attribute_id(attr));

  if (attr == altitude_attribute) {

    APP_LOG(APP_LOG_LEVEL_DEBUG, "altitude_attribute!");
    APP_LOG(APP_LOG_LEVEL_DEBUG, "data: %u", (unsigned int)*(uint32_t *)data);

    int current_altitude = (unsigned int)*(uint32_t *)data;

    // on first read, fill the entire array with that value in order to not avg w/ 0
    if (first_altitude_sample_index) {
      for (int j = 0; j < SAMPLES; j++) {
        altitude_samples[j] = current_altitude;
      }
      first_altitude_sample_index = false;

      // no previous value to compare, so set equal initially
      altitude_sample_previous = current_altitude;
    }
    else {
      // just write the one value as usual
      altitude_samples[altitude_sample_index] = current_altitude;
      altitude_sample_index++;

      // start over at end of buffer
      if (altitude_sample_index > 9)
          altitude_sample_index = 0;
    }
    

    // calculate average
    int altitude_sample_avg = 0;
    for (int i = 0; i < SAMPLES; i++) {
      altitude_sample_avg += altitude_samples[i];
      APP_LOG(APP_LOG_LEVEL_DEBUG, "altitude_samples[%d]: %u", i, altitude_samples[i]);
    }
    altitude_sample_avg /= SAMPLES;

    APP_LOG(APP_LOG_LEVEL_DEBUG, "altitude_sample_previous: %u", altitude_sample_previous);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "altitude_sample_avg: %u", altitude_sample_avg);

    if (altitude_sample_previous > altitude_sample_avg) {
      // going down
      //text_layer_set_background_color(altitude_text_layer, GColorFolly);
      altitude_delta = -1;

    } else if (altitude_sample_previous < altitude_sample_avg) {
      // going up
      //text_layer_set_background_color(altitude_text_layer, GColorMediumSpringGreen);
      altitude_delta = 1;

    } else {
      // no change
      //text_layer_set_background_color(altitude_text_layer, GColorChromeYellow);
      altitude_delta = 0;

    }

    // store value for comparison next time
    altitude_sample_previous = altitude_sample_avg;


    // TODO: fix buffer size
    static char altitude_buffer[20];
    snprintf(altitude_buffer, 20, "%u", altitude_sample_avg);  
    text_layer_set_text(altitude_text_layer, altitude_buffer);    

    // force canvas refresh to keep in sync with text
    layer_mark_dirty(canvas_layer);
  }
}

static void prv_notified(SmartstrapAttribute *attribute) {
  if (attribute != uptime_attribute) {
    return;
  }
  smartstrap_attribute_read(uptime_attribute);
}


static void prv_set_led_attribute(bool on) {
  SmartstrapResult result;
  uint8_t *buffer;
  size_t length;
  result = smartstrap_attribute_begin_write(led_attribute, &buffer, &length);
  if (result != SmartstrapResultOk) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Begin write failed with error %d", result);
    return;
  }

  buffer[0] = on;

  result = smartstrap_attribute_end_write(led_attribute, 1, false);
  if (result != SmartstrapResultOk) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "End write failed with error %d", result);
    return;
  }
}

static void altitude_timer_callback(void *data) {

  smartstrap_attribute_read(altitude_attribute);

  altitude_timer = app_timer_register(1 * 1000, (AppTimerCallback) altitude_timer_callback, NULL);
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  prv_set_led_attribute(true);
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  prv_set_led_attribute(false);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}


static void canvas_update_proc(Layer *this_layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(this_layer);

  if (altitude_delta > 0) {
    graphics_context_set_fill_color(ctx, GColorMediumSpringGreen);
    graphics_fill_rect(ctx, GRect(5, 40, 144 - 10, 168 - 70), 0, GCornerNone);
    
    graphics_context_set_fill_color(ctx, GColorMediumSpringGreen);
    gpath_draw_filled(ctx, up_path_ptr);
    graphics_context_set_stroke_color(ctx, GColorMediumSpringGreen);
    gpath_draw_outline(ctx, up_path_ptr);

  } else if (altitude_delta < 0) {
    graphics_context_set_fill_color(ctx, GColorFolly);
    graphics_fill_rect(ctx, GRect(5, 10, 144 - 10, 168 - 70), 0, GCornerNone);

    graphics_context_set_fill_color(ctx, GColorFolly);
    gpath_draw_filled(ctx, down_path_ptr);
    graphics_context_set_stroke_color(ctx, GColorFolly);
    gpath_draw_outline(ctx, down_path_ptr);
  } else {
    graphics_context_set_fill_color(ctx, GColorChromeYellow);
    graphics_fill_rect(ctx, GRect(5, 25, 144 - 10, 168 - 70), 0, GCornerNone);
  }
}


static void graph_timer_callback(void *data) {
  // hide graph after 10s
  window_stack_pop(graph_window);
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
  // user shook or tapped Pebble (ignore axis/direction)

  if (!window_is_loaded(graph_window)) {
    // ignore if already loaded

    window_stack_push(graph_window, true);

    // kick off window-closing timer
    graph_timer = app_timer_register(10 * 1000, (AppTimerCallback) graph_timer_callback, NULL);
  }

}


static void window_load(Window *window) {
  window_set_background_color(window, GColorWhite);
  

  Layer *window_layer = window_get_root_layer(window);
  GRect window_bounds = layer_get_bounds(window_layer);

  // Create Layer
  canvas_layer = layer_create(GRect(0, 0, window_bounds.size.w, window_bounds.size.h));
  layer_add_child(window_layer, canvas_layer);

  // Set the update_proc
  layer_set_update_proc(canvas_layer, canvas_update_proc);


  // text layer for connection status
  status_text_layer = text_layer_create(GRect(0, 148, 72, 20));
  text_layer_set_font(status_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(status_text_layer, GTextAlignmentCenter);
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(status_text_layer));
  prv_availability_changed(SERVICE_ID, smartstrap_service_is_available(SERVICE_ID));

  // text layer for showing the uptime attribute
  uptime_text_layer = text_layer_create(GRect(72, 148, 72, 20));
  text_layer_set_font(uptime_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(uptime_text_layer, GTextAlignmentCenter);
  text_layer_set_text(uptime_text_layer, "-");
  text_layer_set_background_color(uptime_text_layer, GColorCyan);
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(uptime_text_layer));

  // text layer for showing how high you are RN
  altitude_text_layer = text_layer_create(GRect(0, (168 / 2) - 20 - (50 / 2) - 5, 144, 50));
  text_layer_set_font(altitude_text_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(altitude_text_layer, GTextAlignmentCenter);
  text_layer_set_text(altitude_text_layer, "*");
  //text_layer_set_background_color(altitude_text_layer, GColorRichBrilliantLavender);
  text_layer_set_background_color(altitude_text_layer, GColorClear);
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(altitude_text_layer));

  // current time
  time_layer = text_layer_create(GRect(0, 80 - 5, 144, 36));
  text_layer_set_font(time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28));
  text_layer_set_text_alignment(time_layer, GTextAlignmentCenter);
  text_layer_set_text(time_layer, "-");
  //text_layer_set_background_color(time_layer, GColorCobaltBlue);
  text_layer_set_background_color(time_layer, GColorClear);
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(time_layer));
  

  // load splash (last!)

  // Create GBitmap, then set to created BitmapLayer
  splash_bitmap = gbitmap_create_with_resource(SPLASH);
  splash_layer = bitmap_layer_create(GRect(0, 0, 144, 168));
  bitmap_layer_set_bitmap(splash_layer, splash_bitmap);
  layer_add_child(window_get_root_layer(window), bitmap_layer_get_layer(splash_layer));


  // path setup(s)
  up_path_ptr = gpath_create(&UP_PATH_INFO);
  down_path_ptr = gpath_create(&DOWN_PATH_INFO);


  // Register with Tap Event Service
  accel_tap_service_subscribe(tap_handler);

}

static void window_unload(Window *window) {
  text_layer_destroy(status_text_layer);
  text_layer_destroy(uptime_text_layer);
}



static void update_time(void) {
  // Get a tm structure
  time_t temp = time(NULL); 
  struct tm *tick_time = localtime(&temp);

  // TODO: fix buffer size
  static char buffer[20];
  strftime(buffer, 20, "%H:%M", tick_time);  
  text_layer_set_text(time_layer, buffer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
}


static void splash_timer_callback(void *data) {
  // hide splash after 3s
  layer_set_hidden((Layer *)splash_layer, true);
}


static void init(void) {
  // setup window
  window = window_create();
  window_set_click_config_provider(window, click_config_provider);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  const bool animated = true;
  window_stack_push(window, animated);

  // create window for graph
  graph_window = window_create();

  // setup smartstrap
  SmartstrapHandlers handlers = (SmartstrapHandlers) {
    .availability_did_change = prv_availability_changed,
    .did_read = prv_did_read,
    .notified = prv_notified
  };
  smartstrap_subscribe(handlers);
  led_attribute = smartstrap_attribute_create(SERVICE_ID, LED_ATTRIBUTE_ID, LED_ATTRIBUTE_LENGTH);
  uptime_attribute = smartstrap_attribute_create(SERVICE_ID, UPTIME_ATTRIBUTE_ID,
                                                 UPTIME_ATTRIBUTE_LENGTH);
  altitude_attribute = smartstrap_attribute_create(SERVICE_ID, ALTITUDE_ATTRIBUTE_ID, ALTITUDE_ATTRIBUTE_LENGTH);

  // update how high you are every 1 second(s) vs. using events
  altitude_timer = app_timer_register(1 * 1000, (AppTimerCallback) altitude_timer_callback, NULL);

  // start splash timer
  splash_timer = app_timer_register(3 * 1000, (AppTimerCallback) splash_timer_callback, NULL);  

  // Make sure the time is displayed from the start (before waiting for tick)
  update_time();

  // Register with TickTimerService
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void deinit(void) {

  // TODO: review code for other things that need to be detroyed!

  window_destroy(window);
  smartstrap_attribute_destroy(led_attribute);
  smartstrap_attribute_destroy(uptime_attribute);
  smartstrap_attribute_destroy(altitude_attribute);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
