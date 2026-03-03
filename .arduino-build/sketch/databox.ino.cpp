#include <Arduino.h>
#line 1 "/Users/hq/github_projects/arduino-projects/databox/databox.ino"
// Include I2S driver
#include <driver/i2s.h>
#include <driver/adc.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <base64.h>
#include <stdint.h>
#include "app_script_credentials.h"
#include "wifi_credentials.h"

// Connections to INMP441 I2S microphone
#define I2S_WS 19
#define I2S_SD 23
#define I2S_SCK 16
const int button_pin = 18;
const int led_pins[6] = {32, 33, 25, 26, 27, 14};
const int display_pins[4] = {32, 33, 25, 26};
const int warning_led_pin = 27;
const int error_led_pin = 14;
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

esp_err_t i2s_install() {
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

  return i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
}

esp_err_t i2s_setpin() {
  // Set I2S pin configuration
  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  return i2s_set_pin(I2S_PORT, &pin_config);
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
const int max_score = 15;
const int pot_sample_count = 16;
const int stable_score_reads = 4;
const int short_recording_samples = 1600;
const uint32_t low_memory_threshold = 32768;
const unsigned long status_blink_on_ms = 200;
const unsigned long status_blink_off_ms = 200;
const unsigned long status_blink_pause_ms = 1200;
uint8_t* audio_data = NULL;
int num_cycles = 0;
int new_scores[2] = {-1, -1};
int scores[2] = {-1, -1};
int pot_values[2];
int pending_scores[2] = {-1, -1};
int pending_score_counts[2] = {0, 0};

const adc1_channel_t pot_channels[2] = {
  ADC1_CHANNEL_6, // GPIO34
  ADC1_CHANNEL_7  // GPIO35
};

enum WarningCode {
  WARNING_NONE = 0,
  WARNING_WIFI_BOOT = 1,
  WARNING_UPLOAD_SKIPPED = 2,
  WARNING_RECORDING_TRUNCATED = 3,
  WARNING_RECORDING_SHORT = 4,
  WARNING_LOW_MEMORY = 5
};

enum ErrorCode {
  ERROR_NONE = 0,
  ERROR_AUDIO_ALLOC = 1,
  ERROR_I2S_READ = 2,
  ERROR_WAV_ALLOC = 3,
  ERROR_HTTP_BEGIN = 4,
  ERROR_HTTP_POST = 5,
  ERROR_I2S_INIT = 6
};

uint16_t warning_flags = 0;
uint16_t error_flags = 0;

uint16_t code_to_mask(int code) {
  return 1U << code;
}

WarningCode highest_active_warning() {
  for (int code = WARNING_WIFI_BOOT; code <= WARNING_LOW_MEMORY; code++) {
    if (warning_flags & code_to_mask(code)) {
      return static_cast<WarningCode>(code);
    }
  }
  return WARNING_NONE;
}

ErrorCode highest_active_error() {
  for (int code = ERROR_AUDIO_ALLOC; code <= ERROR_I2S_INIT; code++) {
    if (error_flags & code_to_mask(code)) {
      return static_cast<ErrorCode>(code);
    }
  }
  return ERROR_NONE;
}

void set_warning(WarningCode code) {
  if (code != WARNING_NONE) {
    warning_flags |= code_to_mask(code);
  }
}

void clear_warning(WarningCode code) {
  if (code != WARNING_NONE) {
    warning_flags &= ~code_to_mask(code);
  }
}

void set_error(ErrorCode code) {
  if (code != ERROR_NONE) {
    error_flags |= code_to_mask(code);
  }
}

void clear_error(ErrorCode code) {
  if (code != ERROR_NONE) {
    error_flags &= ~code_to_mask(code);
  }
}

void update_memory_warning() {
  if (ESP.getMaxAllocHeap() < low_memory_threshold) {
    set_warning(WARNING_LOW_MEMORY);
  } else {
    clear_warning(WARNING_LOW_MEMORY);
  }
}

void update_status_leds() {
  static bool leds_on = false;
  static bool in_pause = false;
  static int flashes_remaining = 0;
  static unsigned long last_change_ms = 0;
  static int active_code = -1;
  static int active_pin = -1;

  int target_pin = -1;
  int target_code = 0;

  ErrorCode active_error = highest_active_error();
  WarningCode active_warning = highest_active_warning();

  if (active_error != ERROR_NONE) {
    target_pin = error_led_pin;
    target_code = active_error;
  } else if (active_warning != WARNING_NONE) {
    target_pin = warning_led_pin;
    target_code = active_warning;
  }

  if (target_pin != active_pin || target_code != active_code) {
    digitalWrite(warning_led_pin, LOW);
    digitalWrite(error_led_pin, LOW);
    leds_on = false;
    in_pause = false;
    flashes_remaining = target_code;
    last_change_ms = millis();
    active_pin = target_pin;
    active_code = target_code;
  }

  if (target_code == 0) {
    digitalWrite(warning_led_pin, LOW);
    digitalWrite(error_led_pin, LOW);
    return;
  }

  unsigned long now = millis();

  if (in_pause) {
    if (now - last_change_ms >= status_blink_pause_ms) {
      in_pause = false;
      flashes_remaining = target_code;
      last_change_ms = now;
    } else {
      return;
    }
  }

  if (!leds_on) {
    if (flashes_remaining > 0 && now - last_change_ms >= status_blink_off_ms) {
      digitalWrite(active_pin, HIGH);
      leds_on = true;
      last_change_ms = now;
    }
    return;
  }

  if (now - last_change_ms >= status_blink_on_ms) {
    digitalWrite(active_pin, LOW);
    leds_on = false;
    flashes_remaining--;
    last_change_ms = now;
    if (flashes_remaining <= 0) {
      in_pause = true;
    }
  }
}

bool send_to_sheet(int score_one, int score_two, unsigned long hold_duration_ms, const String& base64_audio) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi NOT connected, skipping upload");
    set_warning(WARNING_UPLOAD_SKIPPED);
    return false;
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
    set_error(ERROR_HTTP_BEGIN);
    return false;
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
    clear_warning(WARNING_UPLOAD_SKIPPED);
    clear_error(ERROR_HTTP_BEGIN);
    clear_error(ERROR_HTTP_POST);
    http.end();
    return true;
  } else {
    Serial.println("error on sending POST:");
    Serial.println(http_response_code);
    set_error(ERROR_HTTP_POST);
  }

  http.end();
  return false;
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

int read_pot_average(int index) {
  long total = 0;

  for (int sample = 0; sample < pot_sample_count; sample++) {
    total += read_pot_raw(index);
  }

  return total / pot_sample_count;
}

int raw_to_score(int raw_value) {
  int score = (raw_value * (max_score + 1)) / 4096;
  if (score > max_score) {
    score = max_score;
  }
  return score;
}

void update_scores() {
  int last_changed_index = -1;

  for (int i = 0; i < 2; i++) {
    pot_values[i] = read_pot_average(i);
    new_scores[i] = raw_to_score(pot_values[i]);

    if (new_scores[i] != scores[i]) {
      if (pending_scores[i] != new_scores[i]) {
        pending_scores[i] = new_scores[i];
        pending_score_counts[i] = 1;
      } else {
        pending_score_counts[i]++;
      }

      if (pending_score_counts[i] >= stable_score_reads) {
        Serial.print("Score changed: ");
        Serial.print(scores[i]);
        Serial.print(" -> ");
        Serial.println(new_scores[i]);

        scores[i] = new_scores[i];
        pending_score_counts[i] = 0;
        last_changed_index = i;
      }
    } else {
      pending_scores[i] = -1;
      pending_score_counts[i] = 0;
    }
  }

  if (last_changed_index != -1) {
    Serial.print("Displaying most recent score from pot ");
    Serial.println(last_changed_index);
    send_display(scores[last_changed_index]);
  }

}


void setup() {
  Serial.begin(115200);

  Serial.print("Largest free block at startup: ");
  Serial.println(ESP.getMaxAllocHeap());  // This is the key one!

  if (!attempt_connect(ssid, password)) {
    set_warning(WARNING_WIFI_BOOT);
  } else {
    clear_warning(WARNING_WIFI_BOOT);
  }

  Serial.print("Free heap after WiFi: ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("Largest free block after WiFi: ");
  Serial.println(ESP.getMaxAllocHeap());  // This is the key one!
  update_memory_warning();

  if (i2s_install() != ESP_OK || i2s_setpin() != ESP_OK || i2s_start(I2S_PORT) != ESP_OK) {
    set_error(ERROR_I2S_INIT);
  } else {
    clear_error(ERROR_I2S_INIT);
  }

  Serial.print("Free heap after I2S: ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("Largest free block after I2S: ");
  Serial.println(ESP.getMaxAllocHeap());  // This is the key one!
  update_memory_warning();


  pinMode(button_pin, INPUT_PULLUP);
  init_pots();

  for (int led_pin : led_pins) {
    pinMode(led_pin, OUTPUT);
  }

  digitalWrite(warning_led_pin, LOW);
  digitalWrite(error_led_pin, LOW);

}
void loop() {
  update_status_leds();
  update_scores();
  update_memory_warning();

  if (digitalRead(button_pin) == LOW) {
    if (!audio_data) {
      audio_data = (uint8_t*)malloc(total_samples * sizeof(uint8_t));
      if (!audio_data) {
        Serial.println("Failed to allocate memory");
        set_error(ERROR_AUDIO_ALLOC);
        return;
      }
      clear_error(ERROR_AUDIO_ALLOC);
    }

    unsigned long press_start_ms = millis();
    int samples_written = 0;
    bool hit_recording_cap = false;

    while (digitalRead(button_pin) == LOW && samples_written < total_samples) {
      update_status_leds();
      size_t bytesRead = 0;
      esp_err_t res = i2s_read(I2S_PORT, sBuffer, bufferLen * sizeof(int16_t), &bytesRead, portMAX_DELAY);
      if (res != ESP_OK) {
        Serial.print("i2s_read failed: ");
        Serial.println(res);
        set_error(ERROR_I2S_READ);
        break;
      }
      clear_error(ERROR_I2S_READ);

      int samples_read = bytesRead / sizeof(int16_t);
      for (int i = 0; i < samples_read && samples_written < total_samples; ++i) {
        audio_data[samples_written++] = (sBuffer[i] >> 8) + 128;
      }
    }

    if (samples_written >= total_samples && digitalRead(button_pin) == LOW) {
      hit_recording_cap = true;
      set_warning(WARNING_RECORDING_TRUNCATED);
    } else {
      clear_warning(WARNING_RECORDING_TRUNCATED);
    }

    while (digitalRead(button_pin) == LOW) {
      update_status_leds();
      delay(1);
    }

    unsigned long hold_duration_ms = millis() - press_start_ms;
    update_scores();

    Serial.print("wrote ");
    Serial.print(samples_written);
    Serial.println(" samples!");

    if (!hit_recording_cap) {
      clear_warning(WARNING_RECORDING_TRUNCATED);
    }

    if (samples_written < short_recording_samples) {
      set_warning(WARNING_RECORDING_SHORT);
    } else {
      clear_warning(WARNING_RECORDING_SHORT);
    }

    int data_size = samples_written * sizeof(int8_t);
    int wav_size = 44 + data_size;
    uint8_t* wav_buffer = (uint8_t*)malloc(wav_size);
    if (!wav_buffer) {
      Serial.println("Failed to allocate WAV buffer");
      set_error(ERROR_WAV_ALLOC);
      free(audio_data);
      audio_data = NULL;
      return;
    }
    clear_error(ERROR_WAV_ALLOC);
    Serial.print("Largest free block after wav_buffer: ");

    Serial.println(ESP.getMaxAllocHeap());  // This is the key one!
    update_memory_warning();


    writeWavHeader(wav_buffer, 16000, 8, 1, data_size);
    memcpy(wav_buffer + 44, audio_data, data_size);

    free(audio_data);
    audio_data = NULL;


    String base64Audio = base64::encode(wav_buffer, wav_size);
    Serial.print("Largest free block after encode: ");
    Serial.println(ESP.getMaxAllocHeap());  // This is the key one!
    update_memory_warning();

    free(wav_buffer);
    wav_buffer = NULL;

    send_to_sheet(scores[0], scores[1], hold_duration_ms, base64Audio);
  }
}

