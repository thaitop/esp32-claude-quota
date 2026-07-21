#include "screen_stock.h"

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

// Five rows down the page, each a card the same fill and edge every other
// screen's cards wear. The title band keeps its 36px; what is left, 158px,
// splits into five 28px rows on a 32px pitch and lands one clear of the navbar.
//
// One coin gets a hero and three tiles because a screen showing one thing can
// spend the whole page on it. Five symbols cannot each have that, and a glance
// across a desk wants them side by side to compare rather than stepped through
// -- so each keeps the three figures that fit on a line: the symbol, the price,
// and the day's move, coloured by its sign.
constexpr int16_t PAD = 8;
constexpr int16_t ROW_Y0 = 36;
constexpr int16_t ROW_H = 28;
constexpr int16_t ROW_PITCH = 32;  // ROW_H plus a 4px gap
constexpr int16_t ROW_W = display::WIDTH - 2 * PAD;

// Inside a row: the symbol reads from the left margin every card uses, the
// percentage sits hard against the right edge where its colour is easiest to
// find, and the price is right-aligned into the column between them so the
// decimal points line up down the list.
constexpr int16_t SYM_X = 14;
constexpr int16_t PCT_X = -14;
constexpr int16_t PRICE_X = -104;

// The market badge, top right of the title band. Wide enough for "MARKET
// CLOSED" at Inter 12 with room either side, 22 tall to sit inside the band.
constexpr int16_t BADGE_W = 108;
constexpr int16_t BADGE_H = 22;
constexpr int16_t BADGE_X = display::WIDTH - PAD - BADGE_W;
constexpr int16_t BADGE_Y = 6;

lv_obj_t *badge = nullptr;
lv_obj_t *badgeLabel = nullptr;
lv_obj_t *priceLabels[(size_t)Ticker::Count] = {nullptr};
lv_obj_t *pctLabels[(size_t)Ticker::Count] = {nullptr};

// What each row last drew, beside the trust that produced it. Compared at the
// resolution the row shows -- cents and hundredths of a percent -- so noise
// below what the eye can read does not cost a repaint over SPI.
StockQuote shown[(size_t)Ticker::Count];
bool shownValid[(size_t)Ticker::Count] = {false};
MarketSession shownSession = MarketSession::Unknown;

bool sameAsShown(const StockQuote &quote, size_t i) {
  if (!shownValid[i]) return false;
  if (quote.trusted != shown[i].trusted) return false;
  if (!quote.trusted) return true;  // one Unknown looks like any other
  return (int32_t)(quote.priceUsd * 100) == (int32_t)(shown[i].priceUsd * 100) &&
         (int32_t)(quote.changePct * 100) == (int32_t)(shown[i].changePct * 100);
}

// The badge only ever asserts "closed". Open needs no label -- the prices say
// it -- and Unknown must not claim closed before the clock is even set, so both
// simply hide the pill.
void paintBadge(MarketSession session) {
  if (session == MarketSession::Closed) {
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
  }
}

void buildRow(lv_obj_t *parent, size_t i) {
  lv_obj_t *card =
      makeCard(parent, PAD, ROW_Y0 + (int16_t)i * ROW_PITCH, ROW_W, ROW_H);

  lv_obj_t *symbol = makeLabel(card, &font_inter_17, theme::TEXT);
  lv_label_set_text(symbol, stockSymbol((Ticker)i));
  lv_obj_align(symbol, LV_ALIGN_LEFT_MID, SYM_X, 0);

  priceLabels[i] = makeLabel(card, &font_inter_17, theme::TEXT);
  lv_label_set_text(priceLabels[i], format::UNKNOWN);
  lv_obj_align(priceLabels[i], LV_ALIGN_RIGHT_MID, PRICE_X, 0);

  pctLabels[i] = makeLabel(card, &font_inter_17, theme::MUTED);
  lv_label_set_text(pctLabels[i], format::UNKNOWN);
  lv_obj_align(pctLabels[i], LV_ALIGN_RIGHT_MID, PCT_X, 0);
}

}  // namespace

void buildStockScreen(lv_obj_t *parent) {
  makeTitle(parent, "Stocks", &icon_stock_hdr);

  badge = makePanel(parent, BADGE_W, BADGE_H, BADGE_H / 2, theme::CARD);
  lv_obj_set_pos(badge, BADGE_X, BADGE_Y);
  lv_obj_set_style_border_color(badge, theme::colour(theme::CARD_EDGE),
                                LV_PART_MAIN);
  lv_obj_set_style_border_width(badge, 1, LV_PART_MAIN);
  lv_obj_set_style_border_opa(badge, LV_OPA_COVER, LV_PART_MAIN);

  badgeLabel = makeLabel(badge, &font_inter_12, theme::MUTED);
  lv_label_set_text(badgeLabel, "MARKET CLOSED");
  lv_obj_center(badgeLabel);

  // Hidden until the first update decides the session -- an Unknown session
  // must not flash "closed" between boot and the clock arriving.
  lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);

  for (size_t i = 0; i < (size_t)Ticker::Count; i++) buildRow(parent, i);

  lv_obj_update_layout(parent);
  warnIfCollapsed("stock badge", badge);
}

void updateStockScreen(const AppModel &model) {
  const MarketSession session = model.stocks.session;
  if (session != shownSession) {
    shownSession = session;
    paintBadge(session);
  }

  for (size_t i = 0; i < (size_t)Ticker::Count; i++) {
    const StockQuote &quote = model.stocks.quote((Ticker)i);
    if (sameAsShown(quote, i)) continue;
    shown[i] = quote;
    shownValid[i] = true;

    char text[24];
    format::price(quote.priceUsd, quote.trusted, text, sizeof(text));
    lv_label_set_text(priceLabels[i], text);

    if (!quote.trusted) {
      lv_label_set_text(pctLabels[i], format::UNKNOWN);
      lv_obj_set_style_text_color(pctLabels[i], theme::colour(theme::MUTED),
                                  LV_PART_MAIN);
      lv_obj_align(pctLabels[i], LV_ALIGN_RIGHT_MID, PCT_X, 0);
      continue;
    }

    // Direction carries in the colour, which reads from further than the digits
    // do. Green up, red down -- the same tint the Crypto screen gives a move.
    const uint32_t tint = quote.changePct >= 0.0f ? theme::GREEN : theme::RED;
    format::signedPercent(quote.changePct, true, text, sizeof(text));
    lv_label_set_text(pctLabels[i], text);
    lv_obj_set_style_text_color(pctLabels[i], theme::colour(tint), LV_PART_MAIN);

    // Re-aligned on every write: "+1.20%" and "-12.34%" differ in width, and a
    // label aligned once sits off the right edge for every value but the first.
    lv_obj_align(pctLabels[i], LV_ALIGN_RIGHT_MID, PCT_X, 0);
    lv_obj_align(priceLabels[i], LV_ALIGN_RIGHT_MID, PRICE_X, 0);
  }
}

}  // namespace ui
