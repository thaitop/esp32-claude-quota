# Partial draw buffer, and one feed in flight at a time

The ESP32-2432S028R has 520KB of SRAM and no PSRAM, of which roughly 290KB is
usable heap once the WiFi stack is up. A full 320x240 framebuffer at 16bpp is
153KB and a TLS handshake peaks near 45KB, so the two cannot coexist. LVGL is
therefore configured with a partial draw buffer of one tenth of the display,
double buffered, at about 30KB total — and feeds are polled strictly in
round-robin, never concurrently, with the client torn down as soon as the
response is parsed.

## Consequences

There is a third consumer that is easy to miss, because it is neither the draw
buffer nor a feed: LVGL's own pool, `LV_MEM_SIZE`. It holds every object and
style on all five screens, and it also holds the transient *layers* LVGL
allocates for anything it cannot draw straight into the frame — the Claude
screen's gradient-under-a-radius progress bar wants 9088 bytes of layer on every
repaint of that screen. Adding widgets shrinks the largest free block until that
allocation stops fitting, and the failure mode is not a crash: LVGL logs
"Allocating layer buffer failed. Try later" and retries on every refresh
forever, starving the main loop so that feeds stop being serviced and the panel
freezes on its last frame. The Crypto screen's redesign landed exactly there
with 9780 bytes free against a 9088-byte layer, and the pool went to 56KB.
`main.cpp` prints `lv_mem_monitor()` at boot for this reason; the number to read
is the largest contiguous block, not the total free.

Both halves of this are load-bearing and neither is obvious from reading the
code. Enlarging the draw buffer "for smoothness", or firing the weather and
crypto polls together "because they're independent", will each reproduce the
same symptom: allocation failures that show up as random reboots minutes or
hours later, under whichever screen happens to be mounted. Any change to buffer
size or poll scheduling should be checked against free heap on the device, not
against the simulator, which has no such ceiling.
