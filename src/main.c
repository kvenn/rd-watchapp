/*
 * main.c
 * Creates a Window and TextLayer, then subscribes to wakeup events.
 */

#include <pebble.h>
#include <stdlib.h> /* srand(), rand() */
#include <time.h>   /* time() */

#define START_HOUR 9 // 9am (09:00)
#define END_HOUR 20 // 8pm (20:00)
  
#define WAKEUP_REASON_REMINDER 0

#define PERSIST_KEY_WAKEUP_ID 42
#define PERSIST_KEY_MESSAGE 43

static Window *s_main_window;
static TextLayer *s_output_layer;

static WakeupId s_wakeup_id;

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
//   return min_to_sec(2); // For testing
}

static void vibrate_watch() {
  // Vibe pattern: ON for 1000ms, OFF for 200ms, ON for 1000ms:
  static const uint32_t const segments[] = { 1000, 200, 1000 };
  VibePattern pat = {
    .durations = segments,
    .num_segments = ARRAY_LENGTH(segments),
  };
  vibes_enqueue_custom_pattern(pat);
}

// Test code for printing stuff out on click
static void test_display(int num) {
  static char s_buffer[64];
  snprintf(s_buffer, sizeof(s_buffer), "Random time of: %d seconds", num);
  text_layer_set_text(s_output_layer, s_buffer);
}

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
    struct tm *now_gmtime = gmtime(&now_time);
    
    // Next time to wake and vibrate the watch
    time_t future_time;
    
    // If now is past END_TIME, schedule the next wake for START_HOUR 
    if(now_gmtime->tm_hour > END_HOUR) {
      struct tm tomorrow_morning; // 

      tomorrow_morning.tm_sec = 0;
      tomorrow_morning.tm_min = 0;
      tomorrow_morning.tm_hour = START_HOUR;
      tomorrow_morning.tm_mday = now_gmtime->tm_mday + 1; // TODO can I do this?
      tomorrow_morning.tm_mon = now_gmtime->tm_mon;
      tomorrow_morning.tm_year = now_gmtime->tm_year;
      tomorrow_morning.tm_isdst = 0; /* Daylight Saving not in affect (UTC) */

      // Increment morning time by somewhat random amount
      future_time = mktime(&tomorrow_morning) + get_random_time_increment();
    } else {
      // Current time + somewhat random seconds
      future_time = now_time + get_random_time_increment();
    }
    
    // Schedule wakeup event and keep the WakeupId
    s_wakeup_id = wakeup_schedule(future_time, WAKEUP_REASON_REMINDER, true);
    persist_write_int(PERSIST_KEY_WAKEUP_ID, s_wakeup_id);

//     test_display(gmtime(&future_time)->tm_hour);
  } else {
    // If schedule was called and wake already exists, delete the old one and start a new one
    cancel_all_wakes();
    schedule_next_wake();
  }
}

static void close_app_callback(void *data) {
//   window_stack_pop_all(false); // TODO
  window_stack_pop(s_main_window);
}

// Handler for wakeup of any kind
static void wakeup_handler(WakeupId id, int32_t reason) {
  // The app has woken!
  
  // Vibrate the watch
  vibrate_watch();
  
  // Set the message
  int buffer_size = 25 * sizeof(char);
  char *message;
  message = (char*)malloc(buffer_size);
  if (persist_exists(PERSIST_KEY_MESSAGE)) {
    persist_read_string(PERSIST_KEY_MESSAGE, message, buffer_size); // TODO
  } else {
    message = "Do this weeks exercise.";
  }
  text_layer_set_text(s_output_layer, message);

  // Delete the ID
  persist_delete(PERSIST_KEY_WAKEUP_ID);
  
  // Set the next wakeup
  schedule_next_wake();
  
  // Sleep for 20 seconds to give them time to check the message then close
  app_timer_register(20*1000, close_app_callback, NULL);
}

// This will be a simulation of if the phone sends a push
static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  // Write the message passed from the phone into the "persisted current message"
  persist_write_string(PERSIST_KEY_MESSAGE, "Take a deep breadth");
  schedule_next_wake();
}

static void click_config_provider(void *context) {
  // Register the ClickHandlers
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect window_bounds = layer_get_bounds(window_layer);

  // Create output TextLayer
  s_output_layer = text_layer_create(GRect(0, 0, window_bounds.size.w, window_bounds.size.h));
  text_layer_set_text_alignment(s_output_layer, GTextAlignmentCenter);
  text_layer_set_text(s_output_layer, "Please wait for your first exercise.");
  layer_add_child(window_layer, text_layer_get_layer(s_output_layer));
}

static void main_window_unload(Window *window) {
  // Destroy output TextLayer
  text_layer_destroy(s_output_layer);
}

static void init(void) {
  // Seed the pseudo random number generator for later use in get_random_time_increment
  srand(time(NULL));
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
  // Was this wakeup from the phone? -- TODO!!
  /*} else if(launch_reason() == APP_LAUNCH_PHONE){*/
}

static void deinit(void) {
  // Destroy main Window
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}