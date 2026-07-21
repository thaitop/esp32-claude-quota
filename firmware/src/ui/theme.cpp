#include "theme.h"

#include <Preferences.h>

namespace theme {
namespace {

// The two palettes, one column per Role in the exact order of the Role enum.
// Dark is the original board palette, untouched. Light was tuned for legibility
// on a bright surface, not derived by inverting Dark: the neutrals swap, but the
// Ramp and the pills are chosen afresh so a green still reads as a green against
// white rather than washing out. Verify contrast on the panel, not on an sRGB
// display -- the ILI9341 shifts these a little.
//
// Both arrays are ROLE_COUNT wide. A Role added to the enum without a value
// here reads whatever the initializer leaves as zero (black), which shows up as
// a black patch on the first Mode that lands on it -- loud enough to catch.
constexpr uint32_t DARK[ROLE_COUNT] = {
    /* BG              */ 0x000000,
    /* CARD            */ 0x141416,
    /* CARD_EDGE       */ 0x26262A,
    /* TEXT            */ 0xFFFFFF,
    /* MUTED           */ 0x9A9AA0,
    /* TRACK           */ 0x2E2740,
    /* PILL_SESSION_BG */ 0x3B2E52,
    /* PILL_SESSION_TX */ 0xCDBCEC,
    /* PILL_WEEKLY_BG  */ 0x2C4A10,
    /* PILL_WEEKLY_TX  */ 0xB6E86A,
    /* LIME            */ 0xC7E93B,
    /* GREEN           */ 0x7FD93C,
    /* AMBER           */ 0xFFB020,
    /* ORANGE          */ 0xFF8A3D,
    /* RED             */ 0xF04A2A,
    /* ACCENT          */ 0xF5822E,
    /* NAV_EDGE        */ 0x1E1E22,
    /* NAV_INACTIVE    */ 0x8E8E94,
    /* GLYPH_DISC      */ 0x141416,  // = CARD, so the disc is invisible here
};

constexpr uint32_t LIGHT[ROLE_COUNT] = {
    /* BG              */ 0xFFFFFF,
    /* CARD            */ 0xFAFAFA,
    /* CARD_EDGE       */ 0xD8D8DE,
    /* TEXT            */ 0x18181B,
    /* MUTED           */ 0x6B6B72,
    /* TRACK           */ 0xDED7EC,
    /* PILL_SESSION_BG */ 0xE7DCFA,
    /* PILL_SESSION_TX */ 0x5B3E8E,
    /* PILL_WEEKLY_BG  */ 0xDCEFC0,
    /* PILL_WEEKLY_TX  */ 0x3F6710,
    /* LIME            */ 0x8FBF1E,
    /* GREEN           */ 0x4CAF3A,
    /* AMBER           */ 0xE08A00,
    /* ORANGE          */ 0xE86A20,
    /* RED             */ 0xD8341A,
    /* ACCENT          */ 0xE86A1C,
    /* NAV_EDGE        */ 0xD8D8DE,
    /* NAV_INACTIVE    */ 0xA0A0A6,
    /* GLYPH_DISC      */ 0x2A2A30,  // dark, so pale glyphs read against it
};

Mode curMode = Mode::Dark;
const uint32_t *active = DARK;
uint8_t gen = 0;

// A namespace of our own rather than sharing display's: two NVS handles on one
// namespace is legal but needless, and the keys are unrelated concerns.
Preferences prefs;
constexpr char NVS_NAMESPACE[] = "ui";
constexpr char NVS_KEY_MODE[] = "mode";

// One shared style per (property, Role), lazily created. The `*Ready` flags
// keep an untouched lv_style_t from being handed out uninitialised and let
// rewrite() skip the Roles nothing ever asked for.
lv_style_t bgStyles[ROLE_COUNT];
bool bgReady[ROLE_COUNT] = {false};
lv_style_t textStyles[ROLE_COUNT];
bool textReady[ROLE_COUNT] = {false};
lv_style_t borderStyles[ROLE_COUNT];
bool borderReady[ROLE_COUNT] = {false};

// Push the active palette into every style that exists. lv_style_set_* on a
// property the style already carries overwrites in place -- the property list
// is not regrown -- so this allocates nothing, which is the whole reason the
// structural colours ride styles instead of a rebuild (ADR-0003).
void rewriteStyles() {
  for (uint32_t r = 0; r < ROLE_COUNT; r++) {
    if (bgReady[r]) lv_style_set_bg_color(&bgStyles[r], colour(r));
    if (textReady[r]) lv_style_set_text_color(&textStyles[r], colour(r));
    if (borderReady[r]) lv_style_set_border_color(&borderStyles[r], colour(r));
  }
  // One walk of the object tree refreshes every widget that carries any of the
  // rewritten styles.
  lv_obj_report_style_change(nullptr);
}

}  // namespace

uint32_t valueOf(uint32_t role) {
  return active[role < ROLE_COUNT ? role : 0];
}

void init() {
  prefs.begin(NVS_NAMESPACE, false);
  const uint8_t stored = prefs.getUChar(NVS_KEY_MODE, (uint8_t)Mode::Dark);
  curMode = stored == (uint8_t)Mode::Light ? Mode::Light : Mode::Dark;
  active = curMode == Mode::Light ? LIGHT : DARK;
}

void toggle() {
  curMode = curMode == Mode::Dark ? Mode::Light : Mode::Dark;
  active = curMode == Mode::Light ? LIGHT : DARK;
  prefs.putUChar(NVS_KEY_MODE, (uint8_t)curMode);
  gen++;
  rewriteStyles();
}

Mode mode() { return curMode; }

const char *modeName() { return curMode == Mode::Light ? "Light" : "Dark"; }

uint8_t generation() { return gen; }

lv_style_t *bgStyle(uint32_t role) {
  if (!bgReady[role]) {
    lv_style_init(&bgStyles[role]);
    lv_style_set_bg_color(&bgStyles[role], colour(role));
    bgReady[role] = true;
  }
  return &bgStyles[role];
}

lv_style_t *textStyle(uint32_t role) {
  if (!textReady[role]) {
    lv_style_init(&textStyles[role]);
    lv_style_set_text_color(&textStyles[role], colour(role));
    textReady[role] = true;
  }
  return &textStyles[role];
}

lv_style_t *borderStyle(uint32_t role) {
  if (!borderReady[role]) {
    lv_style_init(&borderStyles[role]);
    lv_style_set_border_color(&borderStyles[role], colour(role));
    borderReady[role] = true;
  }
  return &borderStyles[role];
}

}  // namespace theme
