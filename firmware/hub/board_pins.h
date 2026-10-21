#pragma once
#include <stdint.h>
namespace Board {
constexpr uint8_t LCD_CS=18, LCD_RST=19, LCD_DC=21;
constexpr uint8_t LCD_MOSI=22, LCD_SCK=23, LCD_MISO=33, LCD_BL=32;
constexpr uint8_t CTP_SCL=25, CTP_RST=26, CTP_SDA=27, CTP_INT=14, SD_CS=13;
constexpr int WIDTH=320, HEIGHT=240;
constexpr uint32_t SPI_HZ=1000000;
}
