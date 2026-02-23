#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

using namespace std;

const int attempts = 20;
const int ms_per_attempt = 500;

#include "wifi_credentials.h"

const int button_pin = 23;
const int pot_pin = 34;

const int display_pins[6] = {13, 12, 14, 27, 26, 25};

const int configurations[10][6] = {
  {1, 1, 1, 1, 1, 1}, // 0
  {0, 0, 1, 0, 0, 1}, // 1
  {0, 1, 1, 1, 1, 0}, // 2
  {0, 1, 1, 0, 1, 1}, // 3
  {1, 0, 1, 0, 0, 1}, // 4
  {1, 1, 0, 0, 1, 1}, // 5
  {1, 1, 0, 1, 1, 1}, // 6
  {0, 1, 1, 0, 0, 1}, // 7
  {1, 1, 1, 1, 1, 1}, // 8
  {1, 1, 1, 0, 0, 1}  // 9
};

void send_to_sheet(int pin) {
  Serial.print("send_to_sheet called with value: ");
  Serial.println(pin);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected, sending HTTP request");

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    String url =
      "https://script.google.com/macros/s/AKfycbwh0mD5DdjrGN1r1nbDAw7lwyJxS-ldom4Yj41_LqRGQ_OEEZIKdDw-FgdGJ16LAICcmA/exec?value1="
      + String(pin);

    Serial.print("URL: ");
    Serial.println(url);

    http.begin(client, url);
    http.addHeader("Content-Type", "text/plain");

    int httpResponseCode = http.GET();
    Serial.print("HTTP response code: ");
    Serial.println(httpResponseCode);

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("Response body: ");
      Serial.println(response);
    } else {
      Serial.println("HTTP GET failed");
    }

    http.end();
  } else {
    Serial.println("WiFi NOT connected, skipping HTTP request");
  }
}

bool attempt_connect(const char* ssid, const char* password) {
  Serial.print("Attempting WiFi SSID: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  for (int i = 0; i < attempts; i++) {
    delay(ms_per_attempt);

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      return true;
    }

    Serial.print(".");
  }

  Serial.println();
  Serial.print("Failed to connect to ");
  Serial.println(ssid);
  return false;
}

void send_display(int number) {
  Serial.print("Updating display with number: ");
  Serial.println(number);

  for (int i = 0; i < 6; i++) {
    digitalWrite(display_pins[i],
      configurations[number][i] ? HIGH : LOW);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Booting");

  if (!attempt_connect(ssid_home, password_home)) {
    attempt_connect(ssid_nueva, password_nueva);
  }

  pinMode(button_pin, INPUT_PULLUP);

  for (int pin : display_pins) {
    pinMode(pin, OUTPUT);
  }

  Serial.println("Setup complete");
}

int new_score = -1;
int score = -1;
int pot_value;

void loop() {
  pot_value = analogRead(pot_pin);
  new_score = pot_value * 9 / 4095;

  Serial.print("Pot raw: ");
  Serial.print(pot_value);
  Serial.print(" -> score: ");
  Serial.println(new_score);

  if (new_score != score) {
    Serial.print("Score changed: ");
    Serial.print(score);
    Serial.print(" -> ");
    Serial.println(new_score);

    score = new_score;
    send_display(score);
  }

  if (digitalRead(button_pin) == LOW) {
    Serial.println("Button pressed");
    send_to_sheet(score);
    delay(1000); // crude debounce
  }

  delay(100);
}
