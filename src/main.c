/*
 * main.c
 * Creates a Window and TextLayer, then subscribes to wakeup events.
 */

#include <pebble.h>
#include <stdlib.h> /* srand(), rand() */
#include "PDUtils.h" /* p_mktime */

#define START_HOUR 9 // 9am (09:00)
#define END_HOUR 20 // 8pm (20:00)
  
#define MESSAGE_DURATION 20 // Number of seconds to show the message for
#define MESSAGE_LENGTH 64

#define WAKEUP_REASON_REMINDER 0

#define PERSIST_KEY_WAKEUP_ID 42
#define PERSIST_KEY_MESSAGE 43

static Window *s_main_window;
static TextLayer *s_output_layer;

static WakeupId s_wakeup_id;

DataLoggingSessionRef data_log;

enum {
  // This key specifies which type of message it is (of type enum below)
  MESSAGE_TYPE_KEY = 0x0,
  // This key specifies the message
  MESSAGE_KEY = 0x1
};

// The different message types (only one right now)
enum {
  SET_MESSAGE = 0x0
};

/******************************************
 ******* Utility Methods
 ******************************************/
// Convert minutes to seconds
static int min_to_sec(int min) {
  return min * 60;
}

// Get the amount of time we should wait before next wakeup
// This will be between 1 hour and 30 minutes and 2 hours
static int get_random_time_increment() {
  // 60 minutes * 60 seconds = 1 hour;
  int hour_30 = min_to_sec(60) + min_to_sec(30); // 3600 + 1800 = 5400
  int minute_increment = min_to_sec(rand() % 30); // 0 -> 1800
  return hour_30 + minute_increment; // 5400 -> 7200
//   return min_to_sec(1); // For testing
}

static void vibrate_watch() {
  // Vibe pattern: ON for 1000ms, OFF for 200ms, ON for 1000ms:
  static const uint32_t const segments[] = { 1000, 200, 1000 };
  VibePattern pat = {
    .durations = segments,
    .num_segments = ARRAY_LENGTH(segments),
  };
  vibes_enqueue_custom_pattern(pat);
  
  // Create data log session to send to phone
  data_log = data_logging_create(42, DATA_LOGGING_UINT, 4, false);
  time_t now = time(NULL);
  // Output log data to phone
  DataLoggingResult dlresult = data_logging_log(data_log, (uint8_t *)&now, 1);
}

static void print_time_t(time_t time) {
  char buff[20]; 
  struct tm * timeinfo;
  
  timeinfo = localtime(&time); 
  strftime(buff, 20, "%b %d %H:%M:%S", timeinfo); 
  APP_LOG(APP_LOG_LEVEL_DEBUG, buff);
}

// Test code for printing stuff out on click
static void test_display(int num) {
  static char s_buffer[64];
  snprintf(s_buffer, sizeof(s_buffer), "Random time of: %d seconds", num);
  text_layer_set_text(s_output_layer, s_buffer);
}

/******************************************
 ******* Wake Methods
 ******************************************/

// Cancel all currently schedule wakeups
static void cancel_all_wakes() {
  wakeup_cancel_all();
  persist_delete(PERSIST_KEY_WAKEUP_ID);
}

// Schedule the next wakeup/vibrate
// If it's currently past END_HOUR, then schedule the morning vibration
// Otherwise schedule the next wakeup/vibrate for get_random_time_increment() in the future
static void schedule_next_wake()
{
  // Check the event is not already scheduled
  if (!wakeup_query(s_wakeup_id, NULL)) {
    time_t now_time = time(NULL); // Now
    struct tm *now_localtime = localtime(&now_time);
    
    // Next time to wake and vibrate the watch
    time_t future_time;
    
    // If now is past END_TIME, schedule the next wake for START_HOUR 
    if(now_localtime->tm_hour > END_HOUR) {
      struct tm tomorrow_morning;

      tomorrow_morning.tm_sec = 0;
      tomorrow_morning.tm_min = 0;
      tomorrow_morning.tm_hour = START_HOUR;
      tomorrow_morning.tm_mday = now_localtime->tm_mday + 1;
      tomorrow_morning.tm_mon = now_localtime->tm_mon;
      tomorrow_morning.tm_year = now_localtime->tm_year;
      tomorrow_morning.tm_isdst = now_localtime->tm_isdst;
      
      // Increment morning time by somewhat random amount
      // NOTE: using p_mktime from PDUtils because mktime() is broken on Pebble...
      future_time = p_mktime(&tomorrow_morning) + get_random_time_increment();
      
      APP_LOG(APP_LOG_LEVEL_DEBUG, "It's currently past END_HOUR. Scheduling next alarm for:");
    } else {
      // Current time + somewhat random seconds
      future_time = now_time + get_random_time_increment();
      APP_LOG(APP_LOG_LEVEL_DEBUG, "It's currently before END_HOUR. Scheduling next alarm for:");
    }
    // Log the time of the next scheduled alarm
    print_time_t(future_time);
    
    // Schedule wakeup event and keep the WakeupId
    s_wakeup_id = wakeup_schedule(future_time, WAKEUP_REASON_REMINDER, true);
    persist_write_int(PERSIST_KEY_WAKEUP_ID, s_wakeup_id);
  } else {
    // If schedule was called and wake already exists, delete the old one and start a new one
    cancel_all_wakes();
    schedule_next_wake();
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Wake existed. Cancelling old wake...");
  }
}

// Callback responsible for closing WatchApp once the message has been shown
// for MESSAGE_DURATION seconds
static void close_app_callback(void *data) {
//   window_stack_pop_all(false); // If animation is weird, uncommend this
  window_stack_pop(s_main_window);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "The app has closed! at:");
  time_t now; time(&now); print_time_t(now);
}

// Handler for wakeup of any kind
static void wakeup_handler(WakeupId id, int32_t reason) {
  // The app has woken!
  APP_LOG(APP_LOG_LEVEL_DEBUG, "The app has woken! at: ");
  time_t now; time(&now); print_time_t(now);
  
  // Vibrate the watch
  vibrate_watch();
  
  // Set the message
  int buffer_size = MESSAGE_LENGTH * sizeof(char);
  char *message;
  message = (char*)malloc(buffer_size);
  if (persist_exists(PERSIST_KEY_MESSAGE)) {
    persist_read_string(PERSIST_KEY_MESSAGE, message, buffer_size);
  } else { 
    // If couldn't find message, just recommend they do the exercise
    message = "Do this weeks exercise.";
  }
  text_layer_set_text(s_output_layer, message);

  // Delete the ID
  persist_delete(PERSIST_KEY_WAKEUP_ID);
  
  // Set the next wakeup
  schedule_next_wake();
  
  // Sleep for MESSAGE_DURATION seconds to give them time to check the message then auto-close
  app_timer_register(MESSAGE_DURATION*1000, close_app_callback, NULL);
}

/******************************************
 ******* AppMessage Methods
 ******************************************/

static void in_received_handler(DictionaryIterator *iter, void *context) {
//   Tuple *msg_type_tuple = dict_find(iter, MESSAGE_TYPE_KEY);
  Tuple *msg_tuple = dict_find(iter, MESSAGE_KEY);
  static char s_msg[MESSAGE_LENGTH];

  // The only time the persistent message is empty is the first time the watch
  // sends a message - which means you must schedule the first wake
  // which will in turn set the rest
  if (!persist_exists(PERSIST_KEY_MESSAGE)) {
    schedule_next_wake();
  }
  
  // On receive of msg tuple, set the current message to be passed in message
  if (msg_tuple) {
    strncpy(s_msg, msg_tuple->value->cstring, MESSAGE_LENGTH);
    // Write the message passed from the phone into the "persisted current message"
    persist_write_string(PERSIST_KEY_MESSAGE, s_msg);
  }
}

static void in_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message Dropped!");
}

/******************************************
 ******* Init Methods
 ******************************************/

// TODO: This will be a simulation of the vibrate
static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  vibrate_watch();
}
// TODO: This will be a simulation of if the phone sends a push
static void down_single_click_handler(ClickRecognizerRef recognizer, void *context) {
  // Write the message passed from the phone into the "persisted current message"
  persist_write_string(PERSIST_KEY_MESSAGE, "Take a deep breadth");
  schedule_next_wake();
}
static void click_config_provider(void *context) {
  // Register the ClickHandlers
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_single_click_handler);
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect window_bounds = layer_get_bounds(window_layer);

  // Create output TextLayer
  s_output_layer = text_layer_create(GRect(0, 0, window_bounds.size.w, window_bounds.size.h));
  text_layer_set_text_alignment(s_output_layer, GTextAlignmentCenter);
  text_layer_set_text(s_output_layer, "Please wait for your next exercise.");
  layer_add_child(window_layer, text_layer_get_layer(s_output_layer));
}

static void main_window_unload(Window *window) {
  // Destroy output TextLayer
  text_layer_destroy(s_output_layer);
}

static void init(void) {
  // Seed the pseudo random number generator for later use in get_random_time_increment
  srand(time(NULL));
  
  // Register message handlers
  app_message_register_inbox_received(in_received_handler);
  app_message_register_inbox_dropped(in_dropped_handler);
  // Init buffers
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
  
  // Create main Window
  s_main_window = window_create();
  window_set_click_config_provider(s_main_window, click_config_provider);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true);

  // Subscribe to Wakeup API
  wakeup_service_subscribe(wakeup_handler);

  // Was this a wakeup launch?
  if (launch_reason() == APP_LAUNCH_WAKEUP) {
    // The app was started by a wakeup
    WakeupId id = 0;
    int32_t reason = 0;

    // Get details and handle the wakeup
    wakeup_get_launch_event(&id, &reason);
    wakeup_handler(id, reason);
  }
}

static void deinit(void) {
  // Destroy main Window
  window_destroy(s_main_window);
  data_logging_finish(data_log);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}