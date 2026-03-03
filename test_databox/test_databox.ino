using namespace std;

const int led_pins[6] = {32, 33, 25, 26, 27, 14};
const int pot_pins[2] = {34, 35};
const int button_pins[1] = {18};

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Booting");



  for (int led_pin : led_pins) {
    pinMode(led_pin, OUTPUT);
  }

  for (int button_pin : button_pins) {
    pinMode(button_pin, INPUT_PULLUP);
  }

  Serial.println("Setup complete");
}

void loop() {
  Serial.println("check leds in 10s:");
  delay(10000);

  for (int i = 0; i < 6; i++){
    Serial.print("writing led ");
    Serial.print(led_pins[i]);
    Serial.println(" to HIGH.");
    delay(5000);
    digitalWrite(led_pins[i], HIGH);
  }

  for (int i = 0; i < sizeof(led_pins) / sizeof(led_pins[0]); i++){
    Serial.print("writing led ");
    Serial.print(led_pins[i]);
    Serial.println(" to LOW.");
    delay(5000);
    digitalWrite(led_pins[i], LOW);
  }
  Serial.println("done checking leds");

  Serial.println("checking pots in 10s");
  delay(10000);
  
  for (int i = 0; i < sizeof(pot_pins) / sizeof(pot_pins[0]); i++){
    Serial.print("reading");
    Serial.println(pot_pins[i]);
    for (int j = 0; j < 10; j++){
      Serial.println(analogRead(pot_pins[i]));
      delay(500);
    }
  }
  Serial.println("done checking pots");

  // push button
  Serial.println("checking buttons");
  delay(10000);
   
  for (int i = 0; i < sizeof(button_pins) / sizeof(button_pins[0]); i++){
    Serial.print("reading");
    Serial.println(button_pins[i]);
    for (int j = 0; j < 10; j++){
      Serial.println(digitalRead(button_pins[i]));
      delay(1000);
    }
  }
  Serial.println("done checking button");
}
