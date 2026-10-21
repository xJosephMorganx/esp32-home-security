// User-supplied diagnostic reference. Preserved unchanged below.
// This copies the electrical level; it is not the final global-state LED rule.
const int PIR_PIN = 27;
const int LED_PIN = 25;

void setup() {
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  digitalWrite(LED_PIN, digitalRead(PIR_PIN));
  delay(20);
}
