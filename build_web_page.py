#!/usr/bin/env python3
"""
Bundles web/index.html, web/style.css, and web/app.js into a single,
self-contained, ultra-optimized GZIP-compressed web_page.h stored in ESP32 Flash PROGMEM.
"""

import os
import re
import gzip

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
HTML_PATH = os.path.join(BASE_DIR, "web", "index.html")
CSS_PATH = os.path.join(BASE_DIR, "web", "style.css")
JS_PATH = os.path.join(BASE_DIR, "web", "app.js")
OUT_H = os.path.join(BASE_DIR, "web_page.h")
ARDUINO_OUT_H = "/Users/shivankvibes/Documents/Arduino/deskbuddy/web_page.h"

with open(HTML_PATH, "r", encoding="utf-8") as f:
    html = f.read()

with open(CSS_PATH, "r", encoding="utf-8") as f:
    css = f.read()

with open(JS_PATH, "r", encoding="utf-8") as f:
    js = f.read()

# Replace <link rel="stylesheet" href="style.css"> with <style>...</style>
css_tag = f"<style>\n{css}\n</style>"
html = re.sub(r'<link\s+rel="stylesheet"\s+href="style\.css"[^>]*>', lambda m: css_tag, html)

# In JS, ensure espBaseUrl defaults to "" (same-origin relative URL on ESP32)
# and ensure button clicks call /api/button?btn=1,2,3
js_modified = js.replace('let espBaseUrl = null;', 'let espBaseUrl = "";')

# Replace <script src="app.js"></script> with <script>...</script>
js_tag = f"<script>\n{js_modified}\n</script>"
html = re.sub(r'<script\s+src="app\.js"[^>]*></script>', lambda m: js_tag, html)

# GZIP Compress the bundled HTML/CSS/JS payload for ultra-fast serving (<30ms)
gz_data = gzip.compress(html.encode("utf-8"), compresslevel=9)
gz_len = len(gz_data)

# Format bytes into C array (16 bytes per line)
lines = []
for i in range(0, gz_len, 16):
    chunk = gz_data[i:i+16]
    lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")

hex_array_str = "\n".join(lines)

header_content = f'''#pragma once
#include <Arduino.h>

// DeskBot 2.0 Cyberpunk Web Dashboard (GZIP Compressed PROGMEM Flash)
// Raw: {len(html)} bytes -> Gzip: {gz_len} bytes ({gz_len / len(html) * 100:.1f}%)
const size_t INDEX_HTML_GZ_LEN = {gz_len};
const uint8_t INDEX_HTML_GZ[] PROGMEM = {{
{hex_array_str}
}};
'''

with open(OUT_H, "w", encoding="utf-8") as f:
    f.write(header_content)

if os.path.exists(os.path.dirname(ARDUINO_OUT_H)):
    with open(ARDUINO_OUT_H, "w", encoding="utf-8") as f:
        f.write(header_content)

print(f"✅ Generated {OUT_H} (Raw: {len(html)} bytes -> GZIP: {gz_len} bytes)")
