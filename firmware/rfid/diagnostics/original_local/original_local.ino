#include <SPI.h>
#include <MFRC522.h>

constexpr byte SS_PIN    = 17; constexpr byte RST_PIN   = 23; constexpr byte SCK_PIN   = 18; constexpr byte MISO_PIN  = 21; constexpr byte MOSI_PIN  = 19; constexpr byte IRQ_PIN   = 22;  constexpr byte LED_VERDE = 25; constexpr byte LED_ROJO  = 26;

MFRC522 lector(SS_PIN, RST_PIN);

bool activado = false;

void actualizarEstado() {
  digitalWrite(LED_VERDE, activado ? LOW : HIGH);
  digitalWrite(LED_ROJO,  activado ? HIGH : LOW);

  Serial.println(activado ? "ACTIVADO" : "DESACTIVADO");
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);
  pinMode(IRQ_PIN, INPUT);

  actualizarEstado();

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  lector.PCD_Init();
  delay(50);

  // Muestra información para comprobar la comunicación con el lector.
  lector.PCD_DumpVersionToSerial();

  Serial.println("Acerca una tarjeta para cambiar de estado.");
}

void loop() {
  if (!lector.PICC_IsNewCardPresent()) {
    return;
  }

  if (!lector.PICC_ReadCardSerial()) {
    return;
  }

  Serial.print("Tarjeta UID:");
  for (byte i = 0; i < lector.uid.size; i++) {
    Serial.print(lector.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(lector.uid.uidByte[i], HEX);
  }
  Serial.println();

  activado = !activado;
  actualizarEstado();

  // Pausa esta tarjeta hasta retirarla y volver a acercarla.
  lector.PICC_HaltA();
  lector.PCD_StopCrypto1();

  delay(300);
}
