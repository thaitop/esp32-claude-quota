#include "screen_crypto.h"

#include <Arduino.h>

#include "../config.h"
#include "../display.h"
#include "fonts/ui_fonts.h"
#include "format.h"
#include "theme.h"
#include "ui_icons.h"
#include "widgets.h"

namespace ui {
namespace {

constexpr int16_t PAD = 12;
constexpr int16_t CARD_Y = 42;
constexpr int16_t CARD_H = 118;
constexpr int16_t PILL_H = 26;
constexpr int16_t PILL_PAD = 14;

lv_obj_t *card = nullptr;
lv_obj_t *priceLabel = nullptr;
lv_obj_t *changePill = nullptr;
lv_obj_t *changeLabel = nullptr;
lv_obj_t *caption = nullptr;
lv_obj_t *footnote = nullptr;

// What was last drawn, kept whole. The first pass folded both figures into one
// int with hand-picked strides and they aliased: a change of +1.00% and a price
// move of a few thousand dollars produced the same number, so the screen kept
// the older of the two readings.
CryptoSnapshot shown;
bool everShown = false;

// Compared at the resolution the screen draws -- cents, and hundredths of a
// percent. Comparing the raw floats would repaint on noise nobody can see.
bool sameAsShown(const CryptoSnapshot &crypto) {
  if (!everShown) return false;
  if (crypto.trusted != shown.trusted) return false;
  if (!crypto.trusted) return true;  // one Unknown looks like any other
  return (int32_t)(crypto.priceUsd * 100) == (int32_t)(shown.priceUsd * 100) &&
         (int32_t)(crypto.change24hPct * 100) ==
             (int32_t)(shown.change24hPct * 100);
}

}  // namespace

void buildCryptoScreen(lv_obj_t *parent) {
  // The coin's name rather than "Crypto": the screen shows one coin, and the
  // navbar tile already said which category this is. It comes from the same
  // place as the id that is fetched, so changing the tracked coin cannot leave
  // the title naming the old one.
  //
  // The tile that used to sit at the top right moved to the left of the title.
  // It said the same thing in both places, and the one beside the word is the
  // one that reads as a heading rather than as a second, unexplained marker.
  makeTitle(parent, CRYPTO_COIN_NAME, &icon_crypto);

  card = makeCard(parent, PAD, CARD_Y, display::WIDTH - 2 * PAD, CARD_H);

  priceLabel = makeLabel(card, &font_inter_36, theme::TEXT);
  lv_label_set_text(priceLabel, format::UNKNOWN);
  lv_obj_set_pos(priceLabel, 20, 18);

  // Measured, not content-sized. The same collapse that swallowed the Claude
  // screen's pills would swallow this one, and silently.
  changePill = makePanel(card, 10, PILL_H, PILL_H / 2, theme::TRACK);
  changeLabel = makeLabel(changePill, &font_inter_17, theme::MUTED);
  lv_label_set_text(changeLabel, format::UNKNOWN);
  lv_obj_update_layout(changeLabel);
  lv_obj_set_width(changePill, lv_obj_get_width(changeLabel) + 2 * PILL_PAD);
  lv_obj_set_pos(changePill, 20, 74);
  lv_obj_center(changeLabel);

  caption = makeLabel(card, &font_inter_14, theme::MUTED);
  lv_label_set_text(caption, "24h change");
  lv_obj_align_to(caption, changePill, LV_ALIGN_OUT_RIGHT_MID, 12, 0);

  footnote = makeLabel(parent, &font_inter_14, theme::AMBER);
  lv_label_set_text(footnote, "");
  lv_obj_set_pos(footnote, PAD + 10, CARD_Y + CARD_H + 10);

  lv_obj_update_layout(parent);
  warnIfCollapsed("crypto pill", changePill);
}

void updateCryptoScreen(const AppModel &model) {
  const CryptoSnapshot &crypto = model.crypto;

  // While the Feed is broken the footnote tracks it rather than the price, so
  // it is rewritten even though the price has not moved. It says whether the
  // Feed has ever worked, which is what separates a wrong coin id from an
  // outage.
  if (!crypto.trusted) {
    char note[56];
    format::feedNote(model.status(Feed::Crypto), millis(), note, sizeof(note));
    lv_label_set_text(footnote, note);
  }

  if (sameAsShown(crypto)) return;
  shown = crypto;
  everShown = true;

  if (crypto.trusted) lv_label_set_text(footnote, "");

  char text[24];
  format::price(crypto.priceUsd, crypto.trusted, text, sizeof(text));
  lv_label_set_text(priceLabel, text);

  format::signedPercent(crypto.change24hPct, crypto.trusted, text, sizeof(text));
  lv_label_set_text(changeLabel, text);

  // Direction before digits: the colour is readable from further away than the
  // number is. Untrusted takes the muted track colour rather than either, so a
  // missing figure never reads as a flat day.
  const uint32_t tint = !crypto.trusted ? theme::MUTED
                        : crypto.change24hPct >= 0.0f ? theme::GREEN
                                                      : theme::RED;
  lv_obj_set_style_text_color(changeLabel, theme::colour(tint), LV_PART_MAIN);

  // The pill has to grow with its text: "+12.34%" is wider than "--", and a
  // width fixed at build time clips whichever one it was not measured against.
  lv_obj_update_layout(changeLabel);
  lv_obj_set_width(changePill, lv_obj_get_width(changeLabel) + 2 * PILL_PAD);
  lv_obj_center(changeLabel);

  // ...and the caption beside it has to move when it does. Aligning once at
  // build time placed the caption to the right of a pill measured around "--",
  // so the first real figure grew the pill straight through the words.
  lv_obj_update_layout(changePill);
  lv_obj_align_to(caption, changePill, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
}

}  // namespace ui
