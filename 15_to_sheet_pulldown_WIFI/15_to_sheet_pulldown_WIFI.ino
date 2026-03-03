#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "app_script_credentials.h"
#include "wifi_credentials.h"

using namespace std;

const int attempts = 20;
const int ms_per_attempt = 500;

const int pins[] = {15, 16, 17};
const int pin_count = 3;

void send_to_sheet(int pin) {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("yay connected");

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.begin(client, String(app_script_url_base) + String(pin));
    http.addHeader("Content-Type", "text/plain");

    int httpResponseCode = http.GET();
    http.end();

    if (httpResponseCode > 0) {
      String response = http.getString();

      Serial.println(httpResponseCode);
      Serial.println(response);
    } else {
      Serial.println("error on sending GET:");
      Serial.println(httpResponseCode);

    }

  } else {
    Serial.println("NOPE");
  }
}

bool attempt_connect(const char* ssid, const char* password) {
  WiFi.begin(ssid, password);
  Serial.print("connecting to wifi");

  for (int i = 0; i < attempts; i++) {
    delay(ms_per_attempt);

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

void setup() {
  Serial.begin(115200);
  if (!attempt_connect(ssid_home, password_home)) {
    attempt_connect(ssid_nueva, password_nueva);
  }
  for (int i = 0; i < pin_count; i++) {
    pinMode(pins[i], INPUT_PULLDOWN);
  }
}

void loop() {
  for (int i = 0; i < pin_count; i++) {
    if (digitalRead(pins[i]) == HIGH) {
      send_to_sheet(pins[i]);
      delay(1000);
    } 
  }

  delay(50);
}
