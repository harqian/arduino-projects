// Include I2S driver
#include <driver/i2s.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <base64.h> 
#include <mbedtls/base64.h>



// Connections to INMP441 I2S microphone
#define I2S_WS 19
#define I2S_SD 23
#define I2S_SCK 21
// 22 is button

// Use I2S Processor 0
#define I2S_PORT I2S_NUM_0

// Define input buffer length
#define bufferLen 64
int16_t sBuffer[bufferLen];

void i2s_install() {
  // Set up I2S Processor configuration
  const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 44100,
    .bits_per_sample = i2s_bits_per_sample_t(16),
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = bufferLen,
    .use_apll = false
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
}

void i2s_setpin() {
  // Set I2S pin configuration
  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_set_pin(I2S_PORT, &pin_config);
}

#include "wifi_credentials.h"

bool attempt_connect(const char* ssid, const char* password) {
  WiFi.begin(ssid, password);
  Serial.print("connecting to wifi");

  for (int i = 0; i < 10; i++) {
    delay(1000);

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("connected to wifi!");
      Serial.print("ip: ");
      Serial.println(WiFi.localIP());
      return true;
    }

    Serial.print(".");
  }

  Serial.print("failed to connect to ");
  Serial.println(ssid);
  return false;
}


const int total_samples = 44100 / 2;
int16_t *audio_data = NULL;
int num_cycles = 0;

void setup() {
  Serial.begin(115200);
  
  Serial.print("Total heap: ");
  Serial.println(ESP.getHeapSize());
  Serial.print("Free heap at startup: ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("Largest free block at startup: ");
  Serial.println(ESP.getMaxAllocHeap()); // This is the key one!
  
  attempt_connect(ssid_home, password_home);
  
  Serial.print("Free heap after WiFi: ");
  Serial.println(ESP.getFreeHeap());
    Serial.print("Largest free block after WiFi: ");
  Serial.println(ESP.getMaxAllocHeap()); // This is the key one!
  
  i2s_install();
  i2s_setpin();
  i2s_start(I2S_PORT);
  
  Serial.print("Free heap after I2S: ");
  Serial.println(ESP.getFreeHeap());    
  Serial.print("Largest free block after I2S: ");
  Serial.println(ESP.getMaxAllocHeap()); // This is the key one!

  
  pinMode(22, INPUT_PULLUP);
}

void loop() {
  if (digitalRead(22) == LOW) {
    if (!audio_data) {
      audio_data = (int16_t *)malloc(total_samples * sizeof(int16_t));
      if (!audio_data) {
        Serial.println("Failed to allocate memory");
        return;
      }
    }

    int samples_written = 0;
    while (digitalRead(22) == LOW && samples_written < total_samples) {
      size_t bytesRead = 0;
      esp_err_t res = i2s_read(I2S_PORT, sBuffer, bufferLen * sizeof(int16_t), &bytesRead, portMAX_DELAY);
      int samples_read = bytesRead / sizeof(int16_t);
      for (int i = 0; i < samples_read && samples_written < total_samples; ++i) {
        audio_data[samples_written++] = sBuffer[i];
      }
    }
    Serial.println("wrote " + String(samples_written) + " samples!");

    // POST audio
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.begin(client, "https://script.google.com/macros/s/AKfycby5_SqEBbaBh-yoAkCZ5N9goT7pXflNZfKg3BA0yBQmIqx7qSchW9dfl0zLsR2DQNbM/exec");
    http.addHeader("Content-Type", "application/octet-stream");

    for (int i = 0; i < total_samples; i++) {
      Serial.print(String(audio_data[i]) + " ");
    }
    Serial.println("");

    Serial.println("size: " + String(total_samples * sizeof(int16_t)));

    int http_response_code = http.POST((uint8_t*)audio_data, total_samples * sizeof(int16_t));

    Serial.print("Free heap after send: ");
    Serial.println(ESP.getFreeHeap());
    Serial.print("Largest free block after send: ");
    Serial.println(ESP.getMaxAllocHeap()); // This is the key one!

    if (http_response_code > 0) {
      String response = http.getString();

      Serial.println(http_response_code);
      Serial.println(response);
    } else {
      Serial.println("error on sending POST:");
      Serial.println(http_response_code);

    }
    if (audio_data) {

      free(audio_data);
      audio_data = NULL;
    }
  }
}