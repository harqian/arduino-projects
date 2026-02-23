#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "wifi_credentials.h"

void send_notif() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("yay connected");
    Serial.flush();

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, "https://ntfy.sh/ytLAC4rxbzmlRLwV");
    http.addHeader("Content-Type", "text/plain");

    int httpResponseCode = http.POST("ESP32");

    if (httpResponseCode > 0) {
      String response = http.getString();

      Serial.println(httpResponseCode);
      Serial.println(response);
    } else {
      Serial.println("error on sending POST:");
      Serial.println(httpResponseCode);

    }

  } else {
    Serial.println("NOPE");
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.print("connecting to wifi");

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("connected to wifi!");
  Serial.print("ip: ");
  Serial.println(WiFi.localIP());

  pinMode(4, INPUT_PULLUP);
}

void loop() {
  send_nofit();

  delay(5000);
}

void loop() {
  Serial.println(digitalRead(4));
  delay(1000);
}