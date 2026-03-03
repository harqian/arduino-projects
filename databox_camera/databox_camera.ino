#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <base64.h>
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

void setStatusLEDs(bool led1, bool led2)
{
  digitalWrite(LED1_PIN, led1);
  digitalWrite(LED2_PIN, led2);
}

void blinkError(int times, bool led1, bool led2)
{
  for (int i = 0; i < times; i++)
  {
    setStatusLEDs(led1, led2);
    delay(200);
    setStatusLEDs(LOW, LOW);
    delay(200);
  }
}

void send_capture()
{
  camera_fb_t *fb = NULL;

  fb = esp_camera_fb_get();

  if (!fb)
  {
    blinkError(5, false, true);
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  http.addHeader("Content-Type", "image/jpeg");

  http.addHeader("Content-Disposition", "inline; filename=capture.jpg");
  http.addHeader("Access-Control-Allow-Origin", "*");

  http.begin(client, app_script_url);

  char ts[32];
  snprintf(ts, 32, "%lld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
  http.addHeader("X-Timestamp", (const char *)ts);

  String image_data = base64::encode(fb->buf, fb->len);
  int http_response_code = http.POST(image_data);

  if (http_response_code <= 0)
  {
    blinkError(5, true, false);
  }

  esp_camera_fb_return(fb);
}

void setup()
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // Init status LEDs
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  setStatusLEDs(LOW, LOW); // startup state

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
    blinkError(5, true, true);
    return;
  }

  setStatusLEDs(HIGH, LOW); // camera OK

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

  setStatusLEDs(LOW, HIGH); // wifi connecting

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  setStatusLEDs(HIGH, HIGH); // fully ready

  Serial.print("Camera Ready! Use press button to send image");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void loop()
{
  delay(50);
  if (digitalRead(BUTTON_PIN) == LOW)
  {
    send_capture();
    delay(1000);
  }
}
