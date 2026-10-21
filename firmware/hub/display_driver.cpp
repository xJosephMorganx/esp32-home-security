#include <Arduino.h>
#include <SPI.h>
#include "board_pins.h"
#include "display_driver.h"
#include "hub_config.h"
#include "geometry.h"
using namespace Board;
static SPIClass busLCD(HSPI);
constexpr uint32_t FRECUENCIA_SPI=Board::SPI_HZ;
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

void displayBegin() {
  pinMode(LCD_CS,OUTPUT); digitalWrite(LCD_CS,HIGH);
  pinMode(SD_CS,OUTPUT); digitalWrite(SD_CS,HIGH);
  pinMode(LCD_DC,OUTPUT); digitalWrite(LCD_DC,HIGH);
  pinMode(LCD_RST,OUTPUT); digitalWrite(LCD_RST,HIGH);
  pinMode(LCD_BL,OUTPUT); digitalWrite(LCD_BL,HIGH);
  busLCD.begin(LCD_SCK,LCD_MISO,LCD_MOSI,-1);
  iniciarPantalla();
}

void displayStrip(int x,int y,int w,int h,const uint16_t* pixels) {
  if(x<0 || y<0 || w<=0 || h<=0 || h>2 || x+w>320 || y+h>240)return;
  const auto a=logicalToNative(x,y,Config::LANDSCAPE_FLIP);
  const auto b=logicalToNative(x+w-1,y+h-1,Config::LANDSCAPE_FLIP);
  const int left=min(a.x,b.x),right=max(a.x,b.x);
  const int top=min(a.y,b.y),bottom=max(a.y,b.y);
  const uint8_t col[]={uint8_t(left>>8),uint8_t(left),uint8_t(right>>8),uint8_t(right)};
  const uint8_t row[]={uint8_t(top>>8),uint8_t(top),uint8_t(bottom>>8),uint8_t(bottom)};
  enviarComando(0x2A,col,4); enviarComando(0x2B,row,4);
  // Transpose into native portrait RAM order. 1280 bytes, not a framebuffer.
  static uint8_t wirePixels[320*2*2];
  int pos=0;
  for(int ny=top;ny<=bottom;++ny)for(int nx=left;nx<=right;++nx) {
    auto p=nativeToLogical(nx,ny,Config::LANDSCAPE_FLIP);
    uint16_t c=pixels[(p.y-y)*w+p.x-x];
    wirePixels[pos++]=c>>8;wirePixels[pos++]=c&255;
  }
  busLCD.beginTransaction(SPISettings(SPI_HZ,MSBFIRST,SPI_MODE0));
  digitalWrite(LCD_CS,LOW);digitalWrite(LCD_DC,LOW);busLCD.transfer(0x2C);
  digitalWrite(LCD_DC,HIGH);busLCD.writeBytes(wirePixels,pos);
  digitalWrite(LCD_CS,HIGH);busLCD.endTransaction();
}


