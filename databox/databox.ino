// Include I2S driver
#include <driver/i2s.h>
#include <driver/adc.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <base64.h>
#include "app_script_credentials.h"
#include "wifi_credentials.h"

// Connections to INMP441 I2S microphone
#define I2S_WS 19
#define I2S_SD 23
#define I2S_SCK 16
const int button_pin = 18;
const int led_pins[6] = {32, 33, 25, 26, 27, 14};
const int display_pins[4] = {32, 33, 25, 26};
const int pot_pins[2] = {34, 35};

const int configurations[16][4] = {
  {0, 0, 0, 0}, // 0
  {0, 0, 0, 1}, // 1
  {0, 0, 1, 0}, // 2
  {0, 0, 1, 1}, // 3
  {0, 1, 0, 0}, // 4
  {0, 1, 0, 1}, // 5
  {0, 1, 1, 0}, // 6
  {0, 1, 1, 1}, // 7
  {1, 0, 0, 0}, // 8
  {1, 0, 0, 1}, // 9
  {1, 0, 1, 0}, // 10
  {1, 0, 1, 1}, // 11
  {1, 1, 0, 0}, // 12
  {1, 1, 0, 1}, // 13
  {1, 1, 1, 0}, // 14
  {1, 1, 1, 1}  // 15
};

// Use I2S Processor 0
#define I2S_PORT I2S_NUM_0

// Define input buffer length
#define bufferLen 64
int16_t sBuffer[bufferLen];

void i2s_install() {
  // Set up I2S Processor configuration
  const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
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


const int total_samples = 16000 * 4;
uint8_t* audio_data = NULL;
int num_cycles = 0;
int new_scores[2] = {-1, -1};
int scores[2] = {-1, -1};
int pot_values[2];

const adc1_channel_t pot_channels[2] = {
  ADC1_CHANNEL_6, // GPIO34
  ADC1_CHANNEL_7  // GPIO35
};

void send_to_sheet(int score_one, int score_two, unsigned long hold_duration_ms, const String& base64_audio) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi NOT connected, skipping upload");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(10000);

  Serial.print("Uploading scores/audio with duration: ");
  Serial.println(hold_duration_ms);

  if (!http.begin(client, app_script_url)) {
    Serial.println("HTTP begin failed");
    return;
  }

  http.addHeader("Content-Type", "application/json");

  String payload;
  payload.reserve(base64_audio.length() + 128);
  payload += "{\"score_1\":";
  payload += String(score_one);
  payload += ",\"score_2\":";
  payload += String(score_two);
  payload += ",\"hold_duration_ms\":";
  payload += String(hold_duration_ms);
  payload += ",\"audio_base64\":\"";
  payload += base64_audio;
  payload += "\"}";

  int http_response_code = http.POST(payload);

  Serial.print("Largest free block after send: ");
  Serial.println(ESP.getMaxAllocHeap());

  if (http_response_code > 0) {
    String response = http.getString();
    Serial.println(http_response_code);
    Serial.println(response);
  } else {
    Serial.println("error on sending POST:");
    Serial.println(http_response_code);
  }

  http.end();
}

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
  header[16] = 16;
  header[17] = 0;
  header[18] = 0;
  header[19] = 0;  // subchunk1 size (16 for PCM)
  header[20] = 1;
  header[21] = 0;  // audio format (1 = PCM)
  header[22] = num_channels;
  header[23] = 0;
  header[24] = sample_rate & 0xFF;
  header[25] = (sample_rate >> 8) & 0xFF;
  header[26] = (sample_rate >> 16) & 0xFF;
  header[27] = (sample_rate >> 24) & 0xFF;
  header[28] = byte_rate & 0xFF;
  header[29] = (byte_rate >> 8) & 0xFF;
  header[30] = (byte_rate >> 16) & 0xFF;
  header[31] = (byte_rate >> 24) & 0xFF;
  header[32] = block_align;
  header[33] = 0;
  header[34] = bits_per_sample;
  header[35] = 0;

  // data subchunk
  memcpy(header + 36, "data", 4);
  header[40] = data_size & 0xFF;
  header[41] = (data_size >> 8) & 0xFF;
  header[42] = (data_size >> 16) & 0xFF;
  header[43] = (data_size >> 24) & 0xFF;
}

void send_display(int number) {
  Serial.print("Updating display with number: ");
  Serial.println(number);

  for (int i = 0; i < 4; i++) {
    digitalWrite(display_pins[i],
      configurations[number][i] ? HIGH : LOW);
  }
}

void init_pots() {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(pot_channels[0], ADC_ATTEN_DB_11);
  adc1_config_channel_atten(pot_channels[1], ADC_ATTEN_DB_11);
}

int read_pot_raw(int index) {
  return adc1_get_raw(pot_channels[index]);
}

void update_scores() {
  int last_changed_index = -1;

  for (int i = 0; i < 2; i++) {
    pot_values[i] = read_pot_raw(i);
    new_scores[i] = pot_values[i] * 16 / 4095;

    if (new_scores[i] != scores[i]) {
      Serial.print("Score changed: ");
      Serial.print(scores[i]);
      Serial.print(" -> ");
      Serial.println(new_scores[i]);

      scores[i] = new_scores[i];
      last_changed_index = i;

      if (last_changed_index != -1) {
        Serial.print("Displaying most recent score from pot ");
        Serial.println(last_changed_index);
        send_display(scores[last_changed_index]);
      }
    }
  }

}


void setup() {
  Serial.begin(115200);

  Serial.print("Largest free block at startup: ");
  Serial.println(ESP.getMaxAllocHeap());  // This is the key one!

  attempt_connect(ssid, password);

  Serial.print("Free heap after WiFi: ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("Largest free block after WiFi: ");
  Serial.println(ESP.getMaxAllocHeap());  // This is the key one!

  i2s_install();
  i2s_setpin();
  i2s_start(I2S_PORT);

  Serial.print("Free heap after I2S: ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("Largest free block after I2S: ");
  Serial.println(ESP.getMaxAllocHeap());  // This is the key one!


  pinMode(button_pin, INPUT_PULLUP);
  init_pots();

  for (int led_pin : led_pins) {
    pinMode(led_pin, OUTPUT);
  }

}
void loop() {
  update_scores();

  if (digitalRead(button_pin) == LOW) {
    if (!audio_data) {
      audio_data = (uint8_t*)malloc(total_samples * sizeof(uint8_t));
      if (!audio_data) {
        Serial.println("Failed to allocate memory");
        return;
      }
    }

    unsigned long press_start_ms = millis();
    int samples_written = 0;

    while (digitalRead(button_pin) == LOW && samples_written < total_samples) {
      size_t bytesRead = 0;
      esp_err_t res = i2s_read(I2S_PORT, sBuffer, bufferLen * sizeof(int16_t), &bytesRead, portMAX_DELAY);
      if (res != ESP_OK) {
        Serial.print("i2s_read failed: ");
        Serial.println(res);
        break;
      }

      int samples_read = bytesRead / sizeof(int16_t);
      for (int i = 0; i < samples_read && samples_written < total_samples; ++i) {
        audio_data[samples_written++] = (sBuffer[i] >> 8) + 128;
      }
    }

    while (digitalRead(button_pin) == LOW) {
      delay(1);
    }

    unsigned long hold_duration_ms = millis() - press_start_ms;
    update_scores();

    Serial.print("wrote ");
    Serial.print(samples_written);
    Serial.println(" samples!");

    int data_size = samples_written * sizeof(int8_t);
    int wav_size = 44 + data_size;
    uint8_t* wav_buffer = (uint8_t*)malloc(wav_size);
    if (!wav_buffer) {
      Serial.println("Failed to allocate WAV buffer");
      free(audio_data);
      audio_data = NULL;
      return;
    }
    Serial.print("Largest free block after wav_buffer: ");

    Serial.println(ESP.getMaxAllocHeap());  // This is the key one!


    writeWavHeader(wav_buffer, 16000, 8, 1, data_size);
    memcpy(wav_buffer + 44, audio_data, data_size);

    free(audio_data);
    audio_data = NULL;


    String base64Audio = base64::encode(wav_buffer, wav_size);
    Serial.print("Largest free block after encode: ");
    Serial.println(ESP.getMaxAllocHeap());  // This is the key one!

    free(wav_buffer);
    wav_buffer = NULL;

    send_to_sheet(scores[0], scores[1], hold_duration_ms, base64Audio);
  }
}
