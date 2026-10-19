#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "FT6336.h"

// Pines de pantalla del PCB verificado
constexpr uint8_t LCD_CS   = 18;
constexpr uint8_t LCD_RST  = 19;
constexpr uint8_t LCD_DC   = 21;
constexpr uint8_t LCD_MOSI = 22;
constexpr uint8_t LCD_SCK  = 23;
constexpr uint8_t LCD_MISO = 33;
constexpr uint8_t LCD_BL   = 32;
constexpr uint8_t SD_CS    = 13;

// Pines del touch del PCB verificado
constexpr uint8_t TOUCH_SDA = 27;
constexpr uint8_t TOUCH_SCL = 25;
constexpr uint8_t TOUCH_INT = 14;
constexpr uint8_t TOUCH_RST = 26;

constexpr uint16_t ANCHO = 240;
constexpr uint16_t ALTO  = 320;

// Conservamos la velocidad con la que funcionó la pantalla.
constexpr uint32_t FRECUENCIA_SPI = 1000000;

SPIClass busLCD(HSPI);

// Orden: SDA, SCL, INT, RST, ancho, alto
FT6336 touch(
  TOUCH_SDA,
  TOUCH_SCL,
  TOUCH_INT,
  TOUCH_RST,
  ANCHO,
  ALTO
);

bool touchDisponible = false;
bool estabaTocando = false;
uint32_t ultimoReporte = 0;
uint32_t pulsaciones = 0;

struct Registro {
  uint8_t comando;
  uint8_t cantidad;
  uint8_t datos[15];
};

// Inicialización LCDWiki que ya funcionó
const Registro inicializacion[] = {
  {0xCF, 3, {0x00, 0xC1, 0x30}},
  {0xED, 4, {0x64, 0x03, 0x12, 0x81}},
  {0xE8, 3, {0x85, 0x00, 0x78}},
  {0xCB, 5, {0x39, 0x2C, 0x00, 0x34, 0x02}},
  {0xF7, 1, {0x20}},
  {0xEA, 2, {0x00, 0x00}},
  {0xC0, 1, {0x13}},
  {0xC1, 1, {0x13}},
  {0xC5, 2, {0x1C, 0x35}},
  {0xC7, 1, {0xC8}},
  {0x21, 0, {0x00}},
  {0x36, 1, {0x08}},
  {0xB6, 2, {0x0A, 0xA2}},
  {0x3A, 1, {0x55}},
  {0xF6, 2, {0x01, 0x30}},
  {0xB1, 2, {0x00, 0x1B}},
  {0xF2, 1, {0x00}},
  {0x26, 1, {0x01}},
  {0xE0, 15, {
    0x0F, 0x35, 0x31, 0x0B, 0x0E,
    0x06, 0x49, 0xA7, 0x33, 0x07,
    0x0F, 0x03, 0x0C, 0x0A, 0x00
  }},
  {0xE1, 15, {
    0x00, 0x0A, 0x0F, 0x04, 0x11,
    0x08, 0x36, 0x58, 0x4D, 0x07,
    0x10, 0x0C, 0x32, 0x34, 0x0F
  }}
};

void enviarComando(uint8_t comando,
                   const uint8_t* datos = nullptr,
                   uint8_t cantidad = 0) {
  busLCD.beginTransaction(
    SPISettings(FRECUENCIA_SPI, MSBFIRST, SPI_MODE0)
  );

  digitalWrite(LCD_CS, LOW);
  digitalWrite(LCD_DC, LOW);
  busLCD.transfer(comando);

  if (cantidad > 0) {
    digitalWrite(LCD_DC, HIGH);

    for (uint8_t i = 0; i < cantidad; i++) {
      busLCD.transfer(datos[i]);
    }
  }

  digitalWrite(LCD_CS, HIGH);
  busLCD.endTransaction();
}

void iniciarPantalla() {
  digitalWrite(LCD_RST, HIGH);
  delay(50);
  digitalWrite(LCD_RST, LOW);
  delay(100);
  digitalWrite(LCD_RST, HIGH);
  delay(50);

  for (const Registro& r : inicializacion) {
    enviarComando(r.comando, r.datos, r.cantidad);
  }

  enviarComando(0x11);  // Salir del modo reposo
  delay(120);
  enviarComando(0x29);  // Encender imagen
  delay(50);
}

void rellenarPantalla(uint16_t color) {
  const uint8_t columnas[] = {
    0x00, 0x00,
    uint8_t((ANCHO - 1) >> 8),
    uint8_t((ANCHO - 1) & 0xFF)
  };

  const uint8_t filas[] = {
    0x00, 0x00,
    uint8_t((ALTO - 1) >> 8),
    uint8_t((ALTO - 1) & 0xFF)
  };

  enviarComando(0x2A, columnas, 4);
  enviarComando(0x2B, filas, 4);

  uint8_t bloque[ANCHO * 2];

  for (uint16_t x = 0; x < ANCHO; x++) {
    bloque[x * 2]     = color >> 8;
    bloque[x * 2 + 1] = color & 0xFF;
  }

  busLCD.beginTransaction(
    SPISettings(FRECUENCIA_SPI, MSBFIRST, SPI_MODE0)
  );

  digitalWrite(LCD_CS, LOW);
  digitalWrite(LCD_DC, LOW);
  busLCD.transfer(0x2C);
  digitalWrite(LCD_DC, HIGH);

  for (uint16_t y = 0; y < ALTO; y++) {
    busLCD.writeBytes(bloque, sizeof(bloque));
  }

  digitalWrite(LCD_CS, HIGH);
  busLCD.endTransaction();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(LCD_CS, OUTPUT);
  digitalWrite(LCD_CS, HIGH);

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  pinMode(LCD_DC, OUTPUT);
  digitalWrite(LCD_DC, HIGH);

  pinMode(LCD_RST, OUTPUT);
  digitalWrite(LCD_RST, HIGH);

  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  busLCD.begin(LCD_SCK, LCD_MISO, LCD_MOSI, -1);

  Serial.println("\nPrueba HUB: pantalla y touch");
  Serial.println("LCD: CS18 RST19 DC21 MOSI22 SCK23 BL32 MISO33");

  iniciarPantalla();
  Serial.println("Inicializacion de pantalla enviada");

  Serial.println("Touch: SDA27 SCL25 INT14 RST26");
  touch.begin();
  Wire.setClock(100000);
  touch.setRotation(ROTATION_NORMAL);

  Wire.beginTransmission(FT6336_ADDR);
  touchDisponible = (Wire.endTransmission() == 0);

  if (touchDisponible) {
    Serial.println("Touch responde en 0x38.");
    Serial.println("Toca y suelta para cambiar de color.");
    rellenarPantalla(0xFFFF);  // Blanco
  } else {
    Serial.println("ERROR: el touch no responde en 0x38.");
    rellenarPantalla(0xF800);  // Rojo
  }
}

void loop() {
  if (!touchDisponible) {
    delay(100);
    return;
  }

  touch.read();
  bool tocando = touch.isTouched;

  if (tocando) {
    uint16_t x = touch.points[0].x;
    uint16_t y = touch.points[0].y;

    if (x >= ANCHO || y >= ALTO) {
      if (millis() - ultimoReporte >= 500) {
        ultimoReporte = millis();
        Serial.print("Coordenadas fuera de rango: ");
        Serial.print(x);
        Serial.print(", ");
        Serial.println(y);
      }

      delay(20);
      return;
    }

    if (!estabaTocando) {
      pulsaciones++;

      Serial.print("Pulsacion: ");
      Serial.println(pulsaciones);

      uint16_t color =
        (pulsaciones % 2) ? 0x07E0 : 0x001F;

      rellenarPantalla(color);
    }

    if (millis() - ultimoReporte >= 100) {
      ultimoReporte = millis();

      Serial.print("X: ");
      Serial.print(x);
      Serial.print("  Y: ");
      Serial.print(y);
      Serial.print("  Dedos: ");
      Serial.println(touch.touches);
    }
  }

  estabaTocando = tocando;
  delay(20);
}