# Partial draw buffer, and one feed in flight at a time

The ESP32-2432S028R has 520KB of SRAM and no PSRAM, of which roughly 290KB is
usable heap once the WiFi stack is up. A full 320x240 framebuffer at 16bpp is
153KB and a TLS handshake peaks near 45KB, so the two cannot coexist. LVGL is
therefore configured with a partial draw buffer of one tenth of the display,
double buffered, at about 30KB total — and feeds are polled strictly in
round-robin, never concurrently, with the client torn down as soon as the
response is parsed.

## Consequences

Both halves of this are load-bearing and neither is obvious from reading the
code. Enlarging the draw buffer "for smoothness", or firing the weather and
crypto polls together "because they're independent", will each reproduce the
same symptom: allocation failures that show up as random reboots minutes or
hours later, under whichever screen happens to be mounted. Any change to buffer
size or poll scheduling should be checked against free heap on the device, not
against the simulator, which has no such ceiling.
