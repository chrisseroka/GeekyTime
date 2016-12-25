
#include "pebble.h"
#include "generated/appinfo.h"

//the below 2 lines disable logging
//#undef APP_LOG
//#define APP_LOG(...)

static Window *window;

static TextLayer *bat_perc_layer;
static TextLayer *time_layer;
static TextLayer *date_layer;
static TextLayer *temp_layer;
static TextLayer *weather_loc_layer;
static TextLayer *aqi_25_layer;
static TextLayer *aqi_10_layer;
static TextLayer *sync_count_layer;

static BitmapLayer *bt_layer;
static BitmapLayer *comm_layer;
static BitmapLayer *battery_layer;
static BitmapLayer *icon_layer;
static BitmapLayer *therm_layer;
static GBitmap *bt_bitmap = NULL;
static GBitmap *comm_bitmap = NULL;
static GBitmap *battery_bitmap = NULL;
static GBitmap *icon_bitmap = NULL;
static GBitmap *therm_bitmap = NULL;

static GFont custom_font_temp_30;
static GFont custom_font_temp_40;

static int sync_msg_count = 0;
static bool bt_connected = 1;
static AppSync sync;
static uint8_t sync_buffer[128];
static bool bt_vibrate = 1;
static char *date_format = "mmdd";
static char last_upd_time_text[] = "00:00";
static int32_t refresh_interval = 30;
static int minutes_since_refresh = 0;

enum TupleKey {
  WEATHER_ICON_KEY = 0x0,         // TUPLE_CSTRING
  WEATHER_TEMPERATURE_KEY = 0x1,  // TUPLE_CSTRING
  WEATHER_LOCATION_KEY = 0x2,     // TUPLE_CSTRING
  AQI_PM25 = 0x3,                 // TUPLE_CSTRING
  AQI_PM10 = 0x4,                 // TUPLE_CSTRING
  CONFIG_BT_VIBRATE = 0x64,       // TUPLE_CSTRING (100 in decimal)
  CONFIG_DATE_FORMAT = 0x65,      // TUPLE_CSTRING (101 in decimal)
  CONFIG_REFRESH_INTRVL = 0x66    // TUPLE_INTEGER (102 in decimal)
};

#ifdef PBL_COLOR
static const bool is_color_supported = 1;
#else
static const bool is_color_supported = 0;
#endif

static const uint32_t BATTERY_ICONS[] = {
  RESOURCE_ID_IMG_BATTERY_CHRG, //0
  RESOURCE_ID_IMG_BATTERY_20,   //1
  RESOURCE_ID_IMG_BATTERY_40,   //2
  RESOURCE_ID_IMG_BATTERY_60,   //3
  RESOURCE_ID_IMG_BATTERY_80,   //4
  RESOURCE_ID_IMG_BATTERY_100,  //5
};


static bool is_valid_temp(const char * st)
{
  int len = strlen(st);
  int ascii_code;
  int negative_count = -1;

  for (int i = 0; i < len; i++) {
    ascii_code = (int)st[i];
    switch (ascii_code)
    {

      case 45: // Allow a negative sign.
          negative_count++;
          if (negative_count || i != 0) {
              return false;
          }
          break;

      default:
          if (ascii_code < 48 || ascii_code > 57) {
              return false;
          }
          break;
    }
  }
  return true;
}

static void log_app_msg_result(AppMessageResult app_message_error)
{
  char *error_desc;
  switch (app_message_error) {
    case APP_MSG_OK:
      error_desc = "APP_MSG_OK";
      break;
    case APP_MSG_SEND_TIMEOUT:
      error_desc =  "APP_MSG_SEND_TIMEOUT";
      break;
    case APP_MSG_SEND_REJECTED:
      error_desc =  "APP_MSG_SEND_REJECTED";
      break;
    case APP_MSG_NOT_CONNECTED:
      error_desc =  "APP_MSG_NOT_CONNECTED";
      break;
    case APP_MSG_APP_NOT_RUNNING:
      error_desc =  "APP_MSG_APP_NOT_RUNNING";
      break;
    case APP_MSG_INVALID_ARGS:
      error_desc =  "APP_MSG_INVALID_ARGS";
      break;
    case APP_MSG_BUSY:
      error_desc =  "APP_MSG_BUSY";
      break;
    case APP_MSG_BUFFER_OVERFLOW:
      error_desc =  "APP_MSG_BUFFER_OVERFLOW";
      break;
    case APP_MSG_ALREADY_RELEASED:
      error_desc =  "APP_MSG_ALREADY_RELEASED";
      break;
    case APP_MSG_CALLBACK_ALREADY_REGISTERED:
      error_desc =  "APP_MSG_CALLBACK_ALREADY_REGISTERED";
      break;
    case APP_MSG_CALLBACK_NOT_REGISTERED:
      error_desc =  "APP_MSG_CALLBACK_NOT_REGISTERED";
      break;
    case APP_MSG_OUT_OF_MEMORY:
      error_desc =  "APP_MSG_OUT_OF_MEMORY";
      break;
    case APP_MSG_CLOSED:
      error_desc =  "APP_MSG_CLOSED";
      break;
    case APP_MSG_INTERNAL_ERROR:
      error_desc =  "APP_MSG_INTERNAL_ERROR";
      break;
    default:
      error_desc =  "UNKNOWN ERROR";
      break;
  }

  if (error_desc != NULL)
  {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message Sync Result: %s", error_desc);
  }
}

static void send_cmd(void) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "send_cmd called");
  
  if (comm_bitmap)
  {
    gbitmap_destroy(comm_bitmap);
  }

  comm_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_COMM_ON);
  bitmap_layer_set_bitmap(comm_layer, comm_bitmap);
  layer_mark_dirty(bitmap_layer_get_layer(comm_layer));

  DictionaryIterator *iter;
  AppMessageResult res = app_message_outbox_begin(&iter);

  if (!iter) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Dictionary was empty - quiting");
    log_app_msg_result(res);
    return;
   }

  int value = 1;
  dict_write_int(iter, 1, &value, sizeof(int), true);
  dict_write_end(iter);

  app_message_outbox_send();
  sync_msg_count++;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Sent sync message to phone...");
}

static void handle_time_tick(struct tm* tick_time, TimeUnits units_changed) {

  if(units_changed & MINUTE_UNIT) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Minute tick");
    static char time_text[] = "00:00"; // Needs to be static because it's used by the system later.
    static char date_text[] = "Sun 01-01"; // Needs to be static because it's used by the system later.
    char *time_format;
    time_format = "%I:%M";
    if (clock_is_24h_style())
    {
      time_format = "%R";
    }

    strftime(time_text, sizeof(time_text), time_format, tick_time);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Current time: %s", time_text);

    if (strcmp(date_format, "ddmm") == 0)
    {
      strftime(date_text, sizeof(date_text), "%a %d-%m", tick_time);
    }
    else
    {
      strftime(date_text, sizeof(date_text), "%a %m-%d", tick_time);
    }
  
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Current date: %s", date_text);

    text_layer_set_text(time_layer, time_text);
    text_layer_set_text(date_layer, date_text);
    
    //initialize the last upd time with current time if it is still default
    if (strcmp(last_upd_time_text, "00:00") == 0)
    {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "setting last update time to current time");
      strncpy(last_upd_time_text, time_text, sizeof(time_text));
    }
    
    //if the temp has not been refreshed yet ("--") and it has been more than a minute since last try do it now
    if(temp_layer &&
       text_layer_get_text(temp_layer) != NULL &&
       strcmp(last_upd_time_text, time_text) != 0 &&
       (strcmp("--", text_layer_get_text(temp_layer)) == 0 || !is_valid_temp(text_layer_get_text(temp_layer))))
    {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "invalid temp detected during minute tick. Request weather refresh");

      strncpy(last_upd_time_text, time_text, sizeof(time_text));
      send_cmd();
    }
    
    static char sync_count_text[] = "0000";
    APP_LOG(APP_LOG_LEVEL_DEBUG, "sync count: %i ", sync_msg_count);
    snprintf(sync_count_text, sizeof(sync_count_text), "%d", sync_msg_count);
    text_layer_set_text(sync_count_layer, sync_count_text);
    
    //see if we need to refresh the weather
    APP_LOG(APP_LOG_LEVEL_DEBUG, "%i minutes elapsed since last refresh", minutes_since_refresh);
    if (minutes_since_refresh >= refresh_interval)
    {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Weather refresh timeout reached. Refreshing.");
      minutes_since_refresh = 0;
      send_cmd();
    }
    else
    {
      minutes_since_refresh++;
    }
    
  }

  if(units_changed & HOUR_UNIT) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Hour tick");
    // send_cmd();
  }

}

static void update_date_time() {
  time_t now = time(NULL);
  struct tm *current_time = localtime(&now);
  handle_time_tick(current_time, MINUTE_UNIT);
}

static void handle_tap(AccelAxisType axis, int32_t direction)
{
  APP_LOG(APP_LOG_LEVEL_DEBUG, "tap direction=%i", (int)direction);
  switch (axis) {
    case ACCEL_AXIS_X:
      APP_LOG(APP_LOG_LEVEL_DEBUG, "tap axis=X");
      break;
    case ACCEL_AXIS_Y:
      APP_LOG(APP_LOG_LEVEL_DEBUG, "tap axis=Y");
      break;
    case ACCEL_AXIS_Z:
      APP_LOG(APP_LOG_LEVEL_DEBUG, "tap axis=Z");
      send_cmd();
      break;
  }
}

static void handle_bluetooth(bool connected) {
  if (bt_bitmap)
  {
    gbitmap_destroy(bt_bitmap);
  }
  if (connected)
  {
    bt_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_BT_ON);

    if (!bt_connected)
    {
      bt_connected = 1;
      if (bt_vibrate)
      {
        vibes_double_pulse();
      }
    }
  }
  else
  {
    bt_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_ICON_CLEAR);
    if (bt_connected)
    {
      bt_connected = 0;
      if (bt_vibrate)
      {
        vibes_long_pulse();
      }
    }
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "handle_bluetooth connected=%i", connected);
  bitmap_layer_set_bitmap(bt_layer, bt_bitmap);
  layer_mark_dirty(bitmap_layer_get_layer(bt_layer));
}

static void handle_battery(BatteryChargeState charge_state) {
  static char battery_text[] = "100%";
  if (battery_bitmap) {
    gbitmap_destroy(battery_bitmap);
  }
  if (charge_state.is_charging || charge_state.is_plugged) {

    battery_bitmap = gbitmap_create_with_resource(BATTERY_ICONS[0]);
  }
  else {
    if (charge_state.charge_percent > 80) //80 - 100% charge
    {
      battery_bitmap = gbitmap_create_with_resource(BATTERY_ICONS[5]);
    }
    else if (charge_state.charge_percent > 60 && charge_state.charge_percent <= 80) //60 - 80% charge
    {
      battery_bitmap = gbitmap_create_with_resource(BATTERY_ICONS[4]);
    }
    else if (charge_state.charge_percent > 40 && charge_state.charge_percent <= 60) //40 - 60% charge
    {
      battery_bitmap = gbitmap_create_with_resource(BATTERY_ICONS[3]);
    }
    else if (charge_state.charge_percent > 20 && charge_state.charge_percent <= 40) //20 - 40% charge
    {
      battery_bitmap = gbitmap_create_with_resource(BATTERY_ICONS[2]);
    }
    else  //less than 20% charge
    {
      battery_bitmap = gbitmap_create_with_resource(BATTERY_ICONS[1]);
    }

  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "handle_battery: %i remaining", charge_state.charge_percent);
  snprintf(battery_text, sizeof(battery_text), "%d%%", charge_state.charge_percent);
  text_layer_set_text(bat_perc_layer, battery_text);
  bitmap_layer_set_bitmap(battery_layer, battery_bitmap);
  layer_mark_dirty(bitmap_layer_get_layer(battery_layer));
}


static void sync_error_callback(DictionaryResult dict_error, AppMessageResult app_message_error, void *context) {
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Sync Error CallBack!");
  log_app_msg_result(app_message_error);

  if (comm_bitmap)
  {
    gbitmap_destroy(comm_bitmap);
  }
  comm_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_COMM_ERR);
  bitmap_layer_set_bitmap(comm_layer, comm_bitmap);
  layer_mark_dirty(bitmap_layer_get_layer(comm_layer));

}

static void sync_tuple_changed_callback(const uint32_t key, const Tuple* new_tuple, const Tuple* old_tuple, void* context) {
  static char pm25text[] = "PM2.5: ---";
  static char pm10text[] = "PM10: ---";
  APP_LOG(APP_LOG_LEVEL_DEBUG, "CallBack. Key=%i", (int)key);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Callback. Tuple Value=%s", new_tuple->value->cstring);
  
  //callback called so communication was successful -> remove the comm icon
  if (comm_bitmap)
  {
    gbitmap_destroy(comm_bitmap);
  }
  comm_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_ICON_CLEAR);
  bitmap_layer_set_bitmap(comm_layer, comm_bitmap);
  layer_mark_dirty(bitmap_layer_get_layer(comm_layer));
  
  switch (key) {
    case WEATHER_ICON_KEY:
      if (icon_bitmap)
      {
        gbitmap_destroy(icon_bitmap);
      }

      if (strcmp("01d", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_01d);
      }
      else if (strcmp("01n", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_01n);
      }
      else if (strcmp("02d", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_02d);
      }
      else if (strcmp("02n", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_02n);
      }
      else if (strcmp("03d", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_03d);
      }
      else if (strcmp("03n", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_03n);
      }
      else if (strcmp("04d", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_04d);
      }
      else if (strcmp("04n", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_04n);
      }
      else if (strcmp("09d", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_09d);
      }
      else if (strcmp("09n", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_09n);
      }
      else if (strcmp("10d", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_10d);
      }
      else if (strcmp("10n", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_10n);
      }
      else if (strcmp("11d", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_11d);
      }
      else if (strcmp("11n", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_11n);
      }
      else if (strcmp("13d", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_13d);
      }
      else if (strcmp("13n", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_13n);
      }
      else if (strcmp("50d", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_50d);
      }
      else if (strcmp("50n", new_tuple->value->cstring) == 0)
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_50n);
      }
      else
      {
        icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_00);
      }


      bitmap_layer_set_bitmap(icon_layer, icon_bitmap);
      layer_mark_dirty(bitmap_layer_get_layer(icon_layer));
      break;

    case WEATHER_TEMPERATURE_KEY:
      if (is_valid_temp(new_tuple->value->cstring) || strcmp("--", new_tuple->value->cstring) == 0)
      {
        if (strlen(new_tuple->value->cstring) > 2)
        {
          APP_LOG(APP_LOG_LEVEL_DEBUG, "3 digit temp detected. Setting font to 30");
          text_layer_set_font(temp_layer, custom_font_temp_30);
        }
        else
        {
          APP_LOG(APP_LOG_LEVEL_DEBUG, "2 digit temp detected. Setting font to 40");
          text_layer_set_font(temp_layer, custom_font_temp_40);
        }
        text_layer_set_text(temp_layer, new_tuple->value->cstring);
      }
      else
      {
        if (is_valid_temp(old_tuple->value->cstring))
        {
          APP_LOG(APP_LOG_LEVEL_DEBUG, "invalid temp detected. Will keep current value = %s",
            old_tuple->value->cstring);
          text_layer_set_text(temp_layer, old_tuple->value->cstring);
        }
        else
        {
          APP_LOG(APP_LOG_LEVEL_DEBUG, "invalid temp detected and the previous value is bad too. Using -- ");
          text_layer_set_font(temp_layer, custom_font_temp_40);
          text_layer_set_text(temp_layer, "--");
        }
      }
      break;

    case WEATHER_LOCATION_KEY:
      text_layer_set_text(weather_loc_layer, new_tuple->value->cstring);
      break;
    case AQI_PM25:
      APP_LOG(APP_LOG_LEVEL_DEBUG, "PM25 from JS: %ld", new_tuple->value->int32);
      snprintf(pm25text, sizeof(pm25text), "PM25: %ld", new_tuple->value->int32);
      text_layer_set_text(aqi_25_layer, pm25text);
      break;
    case AQI_PM10:
      APP_LOG(APP_LOG_LEVEL_DEBUG, "PM10 from JS: %ld", new_tuple->value->int32);
      snprintf(pm10text, sizeof(pm10text), "PM10: %ld", new_tuple->value->int32);
      text_layer_set_text(aqi_10_layer, pm10text);
      break;
    case CONFIG_BT_VIBRATE:
      if (strcmp(new_tuple->value->cstring, "On") == 0)
      {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Setting BT Vibrate to On");
        bt_vibrate = 1;
      }
      else
      {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Setting BT Vibrate to Off");
        bt_vibrate = 0;
      }
      break;
    case CONFIG_DATE_FORMAT:
      if (strcmp(new_tuple->value->cstring, "ddmm") == 0)
      {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Setting date format to dd-mm");
        date_format = "ddmm";
      }
      else
      {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Setting date format to mm-dd");
        date_format = "mmdd";
      }
      //update date to reflect the config change
      update_date_time();
      break;
    case CONFIG_REFRESH_INTRVL:
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Refresh interval: %ld", new_tuple->value->int32);
      refresh_interval = new_tuple->value->int32;
      break;
  }
}




static void init() {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Entering Init");
  
  if (is_color_supported)
  {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Running on Color Pebble");
  }
  else
  {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Running on Classic Pebble");
  }
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "WatchFace version: %s", VERSION_LABEL);
  
  window = window_create();
  window_stack_push(window, true /* Animated */);
  window_set_background_color(window, GColorBlack);

  Layer *window_layer = window_get_root_layer(window);
  
  //SYNC COUNT (used for troubleshooting)
  GFont custom_font_sync_count = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TINY_10));
  sync_count_layer = text_layer_create(GRect(1, 1, 33, 15));
  text_layer_set_font(sync_count_layer, custom_font_sync_count);
  text_layer_set_text_color(sync_count_layer, GColorBlack); //black to hide the debug info
  text_layer_set_background_color(sync_count_layer, GColorClear);
  text_layer_set_text_alignment(sync_count_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(sync_count_layer));
  
  //PHONE COMM
  comm_layer = bitmap_layer_create(GRect(35, 3, 10, 10));
  layer_add_child(window_layer, bitmap_layer_get_layer(comm_layer));

  comm_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_COMM_ON);
  bitmap_layer_set_bitmap(comm_layer, comm_bitmap);
  layer_mark_dirty(bitmap_layer_get_layer(comm_layer));

  //BLUETOOTH
  bt_layer = bitmap_layer_create(GRect(55, 3, 10, 10));
  layer_add_child(window_layer, bitmap_layer_get_layer(bt_layer));

  //BATTERY PERCENT
  GFont custom_font_bat_perc = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TINY_10));
  bat_perc_layer = text_layer_create(GRect(65, 1, 33, 15));
  text_layer_set_font(bat_perc_layer, custom_font_bat_perc);
  text_layer_set_text_color(bat_perc_layer, GColorWhite);
  text_layer_set_background_color(bat_perc_layer, GColorClear);
  text_layer_set_text_alignment(bat_perc_layer, GTextAlignmentRight);

  layer_add_child(window_layer, text_layer_get_layer(bat_perc_layer));

  //BATTERY
  battery_layer = bitmap_layer_create(GRect(144-44, 3, 36, 10));
  layer_add_child(window_layer, bitmap_layer_get_layer(battery_layer));

  //TIME
  GFont custom_font_time = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TIME_42));
  time_layer = text_layer_create(GRect(2, 15, 144-2 /* width */, 45 /* 168 max height */));
  text_layer_set_text_alignment(time_layer, GTextAlignmentCenter);
  text_layer_set_font(time_layer, custom_font_time);
  text_layer_set_background_color(time_layer, GColorClear);
  text_layer_set_text_color(time_layer, GColorWhite);

  layer_add_child(window_layer, text_layer_get_layer(time_layer));

  //DATE
  GFont custom_font_date = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DATE_22));
  date_layer = text_layer_create(GRect(2, 62, 144-2 /* width */, 25 /* 168 max height */));
  text_layer_set_text_alignment(date_layer, GTextAlignmentCenter);
  text_layer_set_font(date_layer, custom_font_date);
  text_layer_set_background_color(date_layer, GColorClear);
  text_layer_set_text_color(date_layer, GColorWhite);

  layer_add_child(window_layer, text_layer_get_layer(date_layer));

  //WEATHER ICON
  icon_layer = bitmap_layer_create(GRect(5, 87, 60, 60));
  layer_add_child(window_layer, bitmap_layer_get_layer(icon_layer));

  //THERM
  therm_layer = bitmap_layer_create(GRect(65, 99, 16, 36));
  therm_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_THERM);
  bitmap_layer_set_bitmap(therm_layer, therm_bitmap);

  layer_add_child(window_layer, bitmap_layer_get_layer(therm_layer));

  //TEMP
  custom_font_temp_30 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEMP_30));
  custom_font_temp_40 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEMP_40));

  temp_layer = text_layer_create(GRect(81, 92, 144-85 /* width */, 55 /* 168 max height */));
  text_layer_set_font(temp_layer, custom_font_temp_40);
  text_layer_set_text_alignment(temp_layer, GTextAlignmentCenter);
  text_layer_set_background_color(temp_layer, GColorClear);
  text_layer_set_text_color(temp_layer, GColorWhite);

  layer_add_child(window_layer, text_layer_get_layer(temp_layer));

  //WEATHER LOCATION
  GFont custom_font_weather_loc = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TINY_10));
  weather_loc_layer = text_layer_create(GRect(10, 139, 134 /* width */, 12 /* 168 max height */));
  text_layer_set_text_alignment(weather_loc_layer, GTextAlignmentCenter);
  text_layer_set_font(weather_loc_layer, custom_font_weather_loc);
  text_layer_set_background_color(weather_loc_layer, GColorClear);
  text_layer_set_text_color(weather_loc_layer, GColorWhite);

  layer_add_child(window_layer, text_layer_get_layer(weather_loc_layer));

  //AQI 2,5 LOCATION
  GFont custom_font_2 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TINY_10));
  aqi_25_layer = text_layer_create(GRect(9, 150, 59 /* width */, 12 /* 168 max height */));
  text_layer_set_text_alignment(aqi_25_layer, GTextAlignmentCenter);
  text_layer_set_font(aqi_25_layer, custom_font_2);
  text_layer_set_background_color(aqi_25_layer, GColorClear);
  text_layer_set_text_color(aqi_25_layer, GColorWhite);

  layer_add_child(window_layer, text_layer_get_layer(aqi_25_layer));

  //AQI 10 LOCATION
  GFont custom_font_3 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TINY_10));
  aqi_10_layer = text_layer_create(GRect(65, 150, 73 /* width */, 12 /* 168 max height */));
  text_layer_set_text_alignment(aqi_10_layer, GTextAlignmentCenter);
  text_layer_set_font(aqi_10_layer, custom_font_2);
  text_layer_set_background_color(aqi_10_layer, GColorClear);
  text_layer_set_text_color(aqi_10_layer, GColorWhite);

  layer_add_child(window_layer, text_layer_get_layer(aqi_10_layer));

  //TEST DUMMY Stuff
  // text_layer_set_text(bat_perc_layer, "100%");
  // battery_bitmap = gbitmap_create_with_resource(BATTERY_ICONS[0]);
  // bitmap_layer_set_bitmap(battery_layer, battery_bitmap);
  // icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_WEATHER_00);
  // bitmap_layer_set_bitmap(icon_layer, icon_bitmap);
  // text_layer_set_text(time_layer, "88:88");
  // text_layer_set_text(date_layer, "Sun 12:22");
  // text_layer_set_text(temp_layer, "74");
  // text_layer_set_text(weather_loc_layer, "Denver, CO");

  //update the date and time for the first time
  update_date_time();

  //EVENT SUBSCRIBTIONS
  handle_battery(battery_state_service_peek());
  handle_bluetooth(bluetooth_connection_service_peek());
  tick_timer_service_subscribe(MINUTE_UNIT|HOUR_UNIT, &handle_time_tick);
  battery_state_service_subscribe(&handle_battery);
  bluetooth_connection_service_subscribe(&handle_bluetooth);
  accel_tap_service_subscribe(&handle_tap);


  //GET WEATHER FROM THE WEB
  const int inbound_size = 64;
  const int outbound_size = 64;
  app_message_open(inbound_size, outbound_size);

  char *bt_vibrate_str;
  if (bt_vibrate)
  {
    bt_vibrate_str = "On";
  }
  else
  {
    bt_vibrate_str = "Off";
  }

  Tuplet initial_values[] = {
    TupletCString(WEATHER_ICON_KEY, "00"),
    TupletCString(WEATHER_TEMPERATURE_KEY, "--"),
    TupletCString(WEATHER_LOCATION_KEY, "Unknown"),
    TupletCString(AQI_PM25, "--"),
    TupletCString(AQI_PM10, "--"),
    TupletCString(CONFIG_BT_VIBRATE, bt_vibrate_str),
    TupletCString(CONFIG_DATE_FORMAT, date_format),
    TupletInteger(CONFIG_REFRESH_INTRVL, refresh_interval)
  };

  app_sync_init(&sync, sync_buffer, sizeof(sync_buffer), initial_values,
      ARRAY_LENGTH(initial_values),
      sync_tuple_changed_callback, sync_error_callback, NULL);

}

static void deinit() {
  app_sync_deinit(&sync);
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();
  accel_tap_service_unsubscribe();
  text_layer_destroy(bat_perc_layer);
  text_layer_destroy(time_layer);
  text_layer_destroy(date_layer);
  text_layer_destroy(temp_layer);
  text_layer_destroy(weather_loc_layer);
  text_layer_destroy(aqi_25_layer);
  text_layer_destroy(aqi_10_layer);
  text_layer_destroy(sync_count_layer);
  gbitmap_destroy(comm_bitmap);
  bitmap_layer_destroy(comm_layer);
  gbitmap_destroy(bt_bitmap);
  bitmap_layer_destroy(bt_layer);
  gbitmap_destroy(battery_bitmap);
  bitmap_layer_destroy(battery_layer);
  gbitmap_destroy(icon_bitmap);
  bitmap_layer_destroy(icon_layer);
  gbitmap_destroy(therm_bitmap);
  bitmap_layer_destroy(therm_layer);
  window_destroy(window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
