#pragma once
#include <stdint.h>
void displayBegin();
// Logical landscape coordinates. Source pixels are row-major RGB565.
// Height must be <= 2; caller owns clipping to 320 x 240.
void displayStrip(int x,int y,int width,int height,const uint16_t* pixels);
