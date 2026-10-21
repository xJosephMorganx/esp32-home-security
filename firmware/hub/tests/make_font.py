"""Generate the compact UI font: python make_font.py /path/LiberationSans-Regular.ttf."""
from pathlib import Path
import sys
from PIL import Image, ImageDraw, ImageFont

font = ImageFont.truetype(sys.argv[1], 14)
bitmap, glyphs = [], []
for code in range(32, 127):
    char = chr(code)
    left, top, right, bottom = font.getbbox(char, anchor="ls")
    width, height = right-left, bottom-top
    image = Image.new("L", (max(width, 1), max(height, 1)))
    ImageDraw.Draw(image).text((-left, -top), char, font=font, fill=255, anchor="ls")
    offset = len(bitmap)
    bits = [image.getpixel((x,y)) >= 100 for y in range(height) for x in range(width)]
    for start in range(0,len(bits),8):
        byte = 0
        for index, bit in enumerate(bits[start:start+8]):
            if bit: byte |= 1 << (7-index)
        bitmap.append(byte)
    glyphs.append((offset,width,height,round(font.getlength(char)),left,top))
out = Path(__file__).resolve().parents[1] / "HubSmallFont.h"
lines = ["// Rasterized Liberation Sans, 14 px. See FONT_LICENSE.txt (SIL OFL).",
         "#pragma once", "#include <Adafruit_GFX.h>",
         "const uint8_t HubSmallBitmap[] PROGMEM = {"]
lines += [", ".join(f"0x{x:02x}" for x in bitmap[i:i+16])+"," for i in range(0,len(bitmap),16)]
lines += ["};", "const GFXglyph HubSmallGlyphs[] PROGMEM = {"]
lines += ["{"+", ".join(map(str,g))+"}," for g in glyphs]
lines += ["};", "const GFXfont HubSmallFont PROGMEM = {(uint8_t*)HubSmallBitmap, (GFXglyph*)HubSmallGlyphs, 32, 126, 17};"]
out.write_text("\n".join(lines)+"\n", encoding="utf-8")
print(f"Generated {len(bitmap)} bitmap bytes, 95 glyphs")
