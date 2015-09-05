#include <pebble.h>

#define SAMPLES 10

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
      //text_layer_set_background_color(altitude_text_layer, GColorJazzberryJam);
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

    // TODO: pre-set altitude_sample_previous on first ?


    // TODO: add initial pre-population of all SAMPLES values to current on very first write?


    // TODO: fix buffer size
    static char altitude_buffer[20];
    snprintf(altitude_buffer, 20, "%u", altitude_sample_avg);  
    text_layer_set_text(altitude_text_layer, altitude_buffer);    
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

  altitude_timer = app_timer_register(5 * 1000, (AppTimerCallback) altitude_timer_callback, NULL);
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
  } else if (altitude_delta < 0) {
    graphics_context_set_fill_color(ctx, GColorJazzberryJam);
  } else {
    graphics_context_set_fill_color(ctx, GColorChromeYellow);
  }
  graphics_fill_rect(ctx, GRect(10, 10, 144 - 20, 168 - 40), 0, GCornerNone);
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
  altitude_text_layer = text_layer_create(GRect(0, (168 / 2) - 20 - (50 / 2), 144, 50));
  text_layer_set_font(altitude_text_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(altitude_text_layer, GTextAlignmentCenter);
  text_layer_set_text(altitude_text_layer, "*");
  //text_layer_set_background_color(altitude_text_layer, GColorRichBrilliantLavender);
  text_layer_set_background_color(altitude_text_layer, GColorClear);
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(altitude_text_layer));

  // current time
  time_layer = text_layer_create(GRect(0, 90, 144, 36));
  text_layer_set_font(time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28));
  text_layer_set_text_alignment(time_layer, GTextAlignmentCenter);
  text_layer_set_text(time_layer, "-");
  //text_layer_set_background_color(time_layer, GColorCobaltBlue);
  text_layer_set_background_color(time_layer, GColorClear);
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(time_layer));
  
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

  // update how high you are every 5 seconds vs. using events
  altitude_timer = app_timer_register(5 * 1000, (AppTimerCallback) altitude_timer_callback, NULL);

  // Make sure the time is displayed from the start (before waiting for tick)
  update_time();

  // Register with TickTimerService
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void deinit(void) {
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
