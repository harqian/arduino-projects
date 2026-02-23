// Include I2S driver
#include <driver/i2s.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <base64.h>

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

// Add this function to create WAV header
void writeWavHeader(uint8_t* header, int sample_rate, int bits_per_sample, int num_channels, int data_size) {
  int byte_rate = sample_rate * num_channels * bits_per_sample / 8;
  int block_align = num_channels * bits_per_sample / 8;
  int chunk_size = 36 + data_size;

  // RIFF header
  memcpy(header, "RIFF", 4);
  header[4] = chunk_size & 0xFF;
  header[5] = (chunk_size >> 8) & 0xFF;
  header[6] = (chunk_size >> 16) & 0xFF;
  header[7] = (chunk_size >> 24) & 0xFF;
  memcpy(header + 8, "WAVE", 4);

  // fmt subchunk
  memcpy(header + 12, "fmt ", 4);
  header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0; // subchunk1 size (16 for PCM)
  header[20] = 1; header[21] = 0; // audio format (1 = PCM)
  header[22] = num_channels; header[23] = 0;
  header[24] = sample_rate & 0xFF;
  header[25] = (sample_rate >> 8) & 0xFF;
  header[26] = (sample_rate >> 16) & 0xFF;
  header[27] = (sample_rate >> 24) & 0xFF;
  header[28] = byte_rate & 0xFF;
  header[29] = (byte_rate >> 8) & 0xFF;
  header[30] = (byte_rate >> 16) & 0xFF;
  header[31] = (byte_rate >> 24) & 0xFF;
  header[32] = block_align; header[33] = 0;
  header[34] = bits_per_sample; header[35] = 0;

  // data subchunk
  memcpy(header + 36, "data", 4);
  header[40] = data_size & 0xFF;
  header[41] = (data_size >> 8) & 0xFF;
  header[42] = (data_size >> 16) & 0xFF;
  header[43] = (data_size >> 24) & 0xFF;
}

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
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.begin(client, "https://script.google.com/macros/s/AKfycbwbHz1PzlatN9bivAvQh_Oef0ZFGlkZJL4BozrumBX5FIbjoEn7v7STM_jgdd1swnx4Tw/exec");
    http.addHeader("Content-Type", "audio/wav");

    int data_size = samples_written * sizeof(int16_t);
    int wav_size = 44 + data_size;
    uint8_t* wav_buffer = (uint8_t*)malloc(wav_size);
    if (!wav_buffer) {
      Serial.println("Failed to allocate WAV buffer");
      free(audio_data);
      audio_data = NULL;
      return;
    }


    writeWavHeader(wav_buffer, 44100, 16, 1, data_size);
    memcpy(wav_buffer + 44, audio_data, data_size);

    String base64Audio = base64::encode(wav_buffer, wav_size);
    free(wav_buffer);

    int http_response_code = http.POST(base64Audio);

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