#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <base64.h>
#include <stdint.h>
#include "app_script_credentials.h"
#include "board_config.h"
#include "wifi_credentials.h"

// Status LEDs
#define LED1_PIN 2
#define LED2_PIN 13
#define BUTTON_PIN 14

// LED status encoding:
// 00 = off/off: startup
// 10 = on/off: camera init success
// 01 = off/on: wifi connecting
// 11 = on/on: fully ready (camera + wifi + server)
// blink both = error

const unsigned long status_blink_on_ms = 200;
const unsigned long status_blink_off_ms = 200;
const unsigned long status_blink_pause_ms = 1200;
const unsigned long wifi_connect_timeout_ms = 10000;
const unsigned long wifi_retry_interval_ms = 10000;
const uint32_t low_memory_threshold = 32768;

enum BootState
{
  BOOT_STARTUP,
  BOOT_CAMERA_OK,
  BOOT_WIFI_CONNECTING,
  BOOT_READY
};

enum WarningCode
{
  WARNING_NONE = 0,
  WARNING_WIFI_BOOT = 1,
  WARNING_UPLOAD_SKIPPED = 2,
  WARNING_NO_PSRAM = 3,
  WARNING_LOW_MEMORY = 4
};

enum ErrorCode
{
  ERROR_NONE = 0,
  ERROR_CAMERA_INIT = 1,
  ERROR_FRAME_CAPTURE = 2,
  ERROR_HTTP_BEGIN = 3,
  ERROR_HTTP_POST = 4
};

BootState boot_state = BOOT_STARTUP;
uint16_t warning_flags = 0;
uint16_t error_flags = 0;
bool status_blinking_enabled = false;
unsigned long last_wifi_retry_ms = 0;

void setStatusLEDs(bool led1, bool led2)
{
  digitalWrite(LED1_PIN, led1);
  digitalWrite(LED2_PIN, led2);
}

uint16_t code_to_mask(int code)
{
  return 1U << code;
}

WarningCode highest_active_warning()
{
  for (int code = WARNING_WIFI_BOOT; code <= WARNING_LOW_MEMORY; code++)
  {
    if (warning_flags & code_to_mask(code))
    {
      return static_cast<WarningCode>(code);
    }
  }
  return WARNING_NONE;
}

ErrorCode highest_active_error()
{
  for (int code = ERROR_CAMERA_INIT; code <= ERROR_HTTP_POST; code++)
  {
    if (error_flags & code_to_mask(code))
    {
      return static_cast<ErrorCode>(code);
    }
  }
  return ERROR_NONE;
}

void set_warning(WarningCode code)
{
  if (code != WARNING_NONE)
  {
    warning_flags |= code_to_mask(code);
  }
}

void clear_warning(WarningCode code)
{
  if (code != WARNING_NONE)
  {
    warning_flags &= ~code_to_mask(code);
  }
}

void set_error(ErrorCode code)
{
  if (code != ERROR_NONE)
  {
    error_flags |= code_to_mask(code);
  }
}

void clear_error(ErrorCode code)
{
  if (code != ERROR_NONE)
  {
    error_flags &= ~code_to_mask(code);
  }
}

void update_memory_warning()
{
  if (ESP.getMaxAllocHeap() < low_memory_threshold)
  {
    set_warning(WARNING_LOW_MEMORY);
  }
  else
  {
    clear_warning(WARNING_LOW_MEMORY);
  }
}

void showBootState()
{
  if (boot_state == BOOT_STARTUP)
  {
    setStatusLEDs(LOW, LOW);
  }
  else if (boot_state == BOOT_CAMERA_OK)
  {
    setStatusLEDs(HIGH, LOW);
  }
  else if (boot_state == BOOT_WIFI_CONNECTING)
  {
    setStatusLEDs(LOW, HIGH);
  }
  else
  {
    setStatusLEDs(HIGH, HIGH);
  }
}

bool attempt_connect(const char *target_ssid, const char *target_password)
{
  WiFi.begin(target_ssid, target_password);
  WiFi.setSleep(false);

  Serial.print("WiFi connecting");
  unsigned long start_ms = millis();
  while (millis() - start_ms < wifi_connect_timeout_ms)
  {
    delay(500);
    Serial.print(".");

    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("");
      Serial.println("WiFi connected");
      clear_warning(WARNING_WIFI_BOOT);
      clear_warning(WARNING_UPLOAD_SKIPPED);
      return true;
    }
  }

  Serial.println("");
  Serial.println("WiFi connect timed out");
  set_warning(WARNING_WIFI_BOOT);
  return false;
}

void update_status_leds()
{
  static bool leds_on = false;
  static bool in_pause = false;
  static int flashes_remaining = 0;
  static unsigned long last_change_ms = 0;
  static int active_code = -1;
  static int active_pin = -1;

  if (!status_blinking_enabled)
  {
    showBootState();
    return;
  }

  int target_pin = -1;
  int target_code = 0;

  ErrorCode active_error = highest_active_error();
  WarningCode active_warning = highest_active_warning();

  if (active_error != ERROR_NONE)
  {
    target_pin = LED2_PIN;
    target_code = active_error;
  }
  else if (active_warning != WARNING_NONE)
  {
    target_pin = LED1_PIN;
    target_code = active_warning;
  }

  if (target_pin != active_pin || target_code != active_code)
  {
    setStatusLEDs(LOW, LOW);
    leds_on = false;
    in_pause = false;
    flashes_remaining = target_code;
    last_change_ms = millis();
    active_pin = target_pin;
    active_code = target_code;
  }

  if (target_code == 0)
  {
    showBootState();
    return;
  }

  unsigned long now = millis();

  if (in_pause)
  {
    if (now - last_change_ms >= status_blink_pause_ms)
    {
      in_pause = false;
      flashes_remaining = target_code;
      last_change_ms = now;
    }
    else
    {
      return;
    }
  }

  if (!leds_on)
  {
    if (flashes_remaining > 0 && now - last_change_ms >= status_blink_off_ms)
    {
      digitalWrite(active_pin, HIGH);
      leds_on = true;
      last_change_ms = now;
    }
    return;
  }

  if (now - last_change_ms >= status_blink_on_ms)
  {
    digitalWrite(active_pin, LOW);
    leds_on = false;
    flashes_remaining--;
    last_change_ms = now;
    if (flashes_remaining <= 0)
    {
      in_pause = true;
    }
  }
}

void maybe_retry_wifi()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    clear_warning(WARNING_WIFI_BOOT);
    clear_warning(WARNING_UPLOAD_SKIPPED);
    return;
  }

  unsigned long now = millis();
  if (now - last_wifi_retry_ms < wifi_retry_interval_ms)
  {
    return;
  }

  last_wifi_retry_ms = now;
  boot_state = BOOT_WIFI_CONNECTING;
  status_blinking_enabled = false;
  showBootState();

  if (attempt_connect(ssid, password))
  {
    boot_state = BOOT_READY;
  }

  status_blinking_enabled = true;
}

bool send_capture()
{
  camera_fb_t *fb = NULL;

  fb = esp_camera_fb_get();

  if (!fb)
  {
    set_error(ERROR_FRAME_CAPTURE);
    return false;
  }
  clear_error(ERROR_FRAME_CAPTURE);

  if (WiFi.status() != WL_CONNECTED)
  {
    set_warning(WARNING_UPLOAD_SKIPPED);
    esp_camera_fb_return(fb);
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  http.addHeader("Content-Type", "image/jpeg");

  http.addHeader("Content-Disposition", "inline; filename=capture.jpg");
  http.addHeader("Access-Control-Allow-Origin", "*");

  if (!http.begin(client, app_script_url))
  {
    set_error(ERROR_HTTP_BEGIN);
    esp_camera_fb_return(fb);
    return false;
  }

  char ts[32];
  snprintf(ts, 32, "%lld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
  http.addHeader("X-Timestamp", (const char *)ts);

  String image_data = base64::encode(fb->buf, fb->len);
  int http_response_code = http.POST(image_data);

  if (http_response_code <= 0)
  {
    set_error(ERROR_HTTP_POST);
    http.end();
    esp_camera_fb_return(fb);
    return false;
  }

  clear_warning(WARNING_UPLOAD_SKIPPED);
  clear_error(ERROR_HTTP_BEGIN);
  clear_error(ERROR_HTTP_POST);
  http.end();
  esp_camera_fb_return(fb);
  return true;
}

void setup()
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // Init status LEDs
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  showBootState();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (config.pixel_format == PIXFORMAT_JPEG)
  {
    if (psramFound())
    {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    }
    else
    {
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
      set_warning(WARNING_NO_PSRAM);
    }
  }
  else
  {
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    set_error(ERROR_CAMERA_INIT);
    status_blinking_enabled = true;
    return;
  }
  clear_error(ERROR_CAMERA_INIT);

  boot_state = BOOT_CAMERA_OK;
  showBootState();

  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID)
  {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  if (config.pixel_format == PIXFORMAT_JPEG)
  {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

  boot_state = BOOT_WIFI_CONNECTING;
  showBootState();
  if (attempt_connect(ssid, password))
  {
    boot_state = BOOT_READY;
  }
  else
  {
    boot_state = BOOT_READY;
  }
  update_memory_warning();
  status_blinking_enabled = true;
  update_status_leds();

  Serial.print("Camera Ready! Use press button to send image");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void loop()
{
  update_status_leds();
  update_memory_warning();
  maybe_retry_wifi();
  delay(50);
  if (digitalRead(BUTTON_PIN) == LOW)
  {
    send_capture();
    delay(1000);
  }
}
