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

// The screen is three bands under the title, the same three the Weekly and
// Weather screens use: a hero card that pairs the figure with its picture, a
// row of three stat tiles, and one line of provenance. Same y, same heights,
// same pitch -- three grids that differ by a few pixels read as three products.
//
// It shows one coin at a time and a toggle at the top right steps between them.
// Three coins at once was the first plan and it does not survive the arithmetic:
// each coin owes a price, a percentage, a dollar move and a day's volume, and
// 202 pixels of page split three ways is 67 apiece -- a band shorter than the
// Weather screen's stat tiles, for four figures rather than one. Toggling costs
// a tap and keeps every figure at a size that can be read across a desk.
constexpr int16_t PAD = 8;
constexpr int16_t HERO_Y = 34;
constexpr int16_t HERO_W = display::WIDTH - 2 * PAD;
constexpr int16_t HERO_H = 94;
constexpr int16_t HERO_PAD = 10;

// The price leads and the coin mark answers it, the same way round as the
// Weather screen's temperature and its sky: the number is what the screen is
// for, so it gets the left margin every other screen's figure starts from.
constexpr int16_t PRICE_Y = 12;  // Inter 36's line box carries its leading above
                                 // the digits, so the ink lands level with the
                                 // 10px side padding
constexpr int16_t PAIR_Y = 58;   // 58..79, nine clear of the card's bottom edge

constexpr int16_t COIN_GLYPH = 48;  // the size the badges are generated at
constexpr int16_t GLYPH_X = HERO_W - HERO_PAD - COIN_GLYPH;
constexpr int16_t GLYPH_Y = (HERO_H - COIN_GLYPH) / 2;

// The badge beside the title is a second, smaller copy of the same artwork
// rather than the hero's scaled down. Scaling it at draw time is what sliced
// the top off the icon on every screen but Claude -- see the note over
// TITLE_ICON in widgets.h.

// The toggle. Three segments at the top right, wide enough for a ticker at
// Inter 12 with room either side, and 24 tall so they sit inside the title band
// rather than pushing the hero down.
constexpr int16_t SEG_W = 34;
constexpr int16_t SEG_H = 24;
constexpr int16_t SEG_GAP = 5;
constexpr int16_t SEG_COUNT = (int16_t)Coin::Count;
constexpr int16_t SEG_Y = 5;
constexpr int16_t SEG_X =
    display::WIDTH - PAD - SEG_COUNT * SEG_W - (SEG_COUNT - 1) * SEG_GAP;

// The touch target is larger than the segment it sits behind, the same way the
// navbar's is: a stylus on a resistive panel lands a couple of pixels off, and
// a 34x24 target in the very corner of the display is where that shows up
// worst. Overhangs into the gaps rather than past the screen edge -- the outer
// segments would otherwise want a target that starts off-panel.
constexpr int16_t HIT_W = SEG_W + 2 * SEG_GAP;
constexpr int16_t HIT_H = 34;

// Same geometry as the Weekly and Weather strips, to the pixel.
constexpr int16_t TILE_Y = 132;
constexpr int16_t TILE_H = 46;
constexpr int16_t TILE_GAP = 8;
constexpr int16_t TILE_W = (HERO_W - 2 * TILE_GAP) / 3;
constexpr int16_t FOOT_Y = 182;

struct Tile {
  lv_obj_t *value = nullptr;
};

lv_obj_t *titleIcon = nullptr;
lv_obj_t *titleLabel = nullptr;
lv_obj_t *segments[SEG_COUNT] = {nullptr};
lv_obj_t *segmentLabels[SEG_COUNT] = {nullptr};
lv_obj_t *hero = nullptr;
lv_obj_t *glyph = nullptr;
lv_obj_t *priceLabel = nullptr;
lv_obj_t *pairLabel = nullptr;
lv_obj_t *footnote = nullptr;
Tile tiles[3];  // 24h %, 24h change in dollars, 24h volume

Coin selected = Coin::BTC;

// What was last drawn, kept whole and kept beside the coin it belongs to. The
// first pass folded the figures into one int with hand-picked strides and they
// aliased: a change of +1.00% and a price move of a few thousand dollars
// produced the same number, so the screen kept the older of the two readings.
CoinQuote shown;
Coin shownCoin = Coin::Count;
bool everShown = false;

// The provenance line moves on its own clock -- "4m ago" becomes "9m ago" while
// the price has not budged -- so it is gated on its own text rather than on the
// quote. Setting an identical string still marks the label dirty, and a repaint
// every second is visible as a stutter on a panel this size.
char shownFootnote[64] = {0};

// Where the toggle's tap handler reads the world from.
//
// The handler has to repaint immediately -- a toggle that takes up to a second
// to answer reads as a broken button, and the main loop's tick is what would
// otherwise get to it. It cannot be handed the model through the event's user
// data, because LVGL keeps that pointer for the life of the widget and the
// screen has no promise about the model's address. So the last model passed to
// update() is recorded here, on the same pass that drew from it.
const AppModel *source = nullptr;

// The hero's badge, at the 48px the artwork is generated at.
const lv_image_dsc_t *badgeFor(Coin coin) {
  switch (coin) {
    case Coin::BTC: return &coin_btc;
    case Coin::ETH: return &coin_eth;
    case Coin::BNB: return &coin_bnb;
    default:        return &coin_btc;
  }
}

// The title's, at 26.
const lv_image_dsc_t *titleBadgeFor(Coin coin) {
  switch (coin) {
    case Coin::BTC: return &coin_btc_hdr;
    case Coin::ETH: return &coin_eth_hdr;
    case Coin::BNB: return &coin_bnb_hdr;
    default:        return &coin_btc_hdr;
  }
}

// Compared at the resolution the screen draws -- cents, hundredths of a
// percent, and whole thousands of dollars of volume. Comparing the raw floats
// would repaint on noise nobody can see, and a day's volume is a figure that
// moves in the low digits continuously.
bool sameAsShown(const CoinQuote &quote, Coin coin) {
  if (!everShown || coin != shownCoin) return false;
  if (quote.trusted != shown.trusted) return false;
  if (!quote.trusted) return true;  // one Unknown looks like any other
  return (int32_t)(quote.priceUsd * 100) == (int32_t)(shown.priceUsd * 100) &&
         (int32_t)(quote.change24hPct * 100) ==
             (int32_t)(shown.change24hPct * 100) &&
         (int32_t)(quote.volume24hUsd / 1000.0f) ==
             (int32_t)(shown.volume24hUsd / 1000.0f);
}

void setFootnote(const char *text, uint32_t colour) {
  if (strncmp(text, shownFootnote, sizeof(shownFootnote)) == 0) return;
  snprintf(shownFootnote, sizeof(shownFootnote), "%s", text);
  lv_label_set_text(footnote, shownFootnote);
  lv_obj_set_style_text_color(footnote, theme::colour(colour), LV_PART_MAIN);
}

void setTile(Tile &tile, const char *text, uint32_t colour) {
  lv_label_set_text(tile.value, text);
  lv_obj_set_style_text_color(tile.value, theme::colour(colour), LV_PART_MAIN);
  // Re-centred on every write: "-$1,204" is wider than "--", and a label
  // aligned once around the Unknown marker sits off-centre for every real
  // figure after it.
  lv_obj_align(tile.value, LV_ALIGN_TOP_MID, 0, 18);
}

void buildTile(Tile &tile, lv_obj_t *parent, int index, const char *caption) {
  lv_obj_t *card =
      makeCard(parent, PAD + index * (TILE_W + TILE_GAP), TILE_Y, TILE_W, TILE_H);

  // Caption above figure, both centred -- the arrangement the other two strips
  // use, for the same reason: at 42px tall there is no room to lead with the
  // figure, and three left-aligned pairs read as ragged fragments rather than
  // as one strip.
  lv_obj_t *label = makeLabel(card, &font_inter_12, theme::MUTED);
  lv_label_set_text(label, caption);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 3);

  tile.value = makeLabel(card, &font_inter_17, theme::TEXT);
  setTile(tile, format::UNKNOWN, theme::MUTED);
}

// Which segment looks pressed. Filled and full-strength for the live coin,
// the card's own surface and the navbar's idle grey for the others -- the same
// contrast the navbar uses to say which tile is live, so the two controls on
// the screen are read the same way.
void paintSegments() {
  for (int i = 0; i < SEG_COUNT; i++) {
    const bool live = i == (int)selected;
    lv_obj_set_style_bg_color(
        segments[i], theme::colour(live ? theme::PILL_SESSION_BG : theme::CARD),
        LV_PART_MAIN);
    lv_obj_set_style_border_color(
        segments[i],
        theme::colour(live ? theme::PILL_SESSION_TX : theme::CARD_EDGE),
        LV_PART_MAIN);
    lv_obj_set_style_text_color(
        segmentLabels[i],
        theme::colour(live ? theme::PILL_SESSION_TX : theme::NAV_INACTIVE),
        LV_PART_MAIN);
  }
}

// Everything that names the coin rather than reporting its market: the title,
// both badges and the pair line. Separate from the figures because it changes
// on a tap and they change on a poll.
void paintIdentity() {
  lv_label_set_text(titleLabel, coinName(selected));
  lv_image_set_src(titleIcon, titleBadgeFor(selected));
  lv_image_set_src(glyph, badgeFor(selected));

  char pair[24];
  snprintf(pair, sizeof(pair), "%s / USD", coinTicker(selected));
  lv_label_set_text(pairLabel, pair);

  paintSegments();
}

void onSegmentPressed(lv_event_t *event) {
  const Coin coin = (Coin)(intptr_t)lv_event_get_user_data(event);
  if (coin == selected) return;

  selected = coin;
  paintIdentity();

  // Straight to the figures rather than waiting for the next tick. update() is
  // change-detecting and the coin has just changed, so it repaints in full.
  if (source != nullptr) updateCryptoScreen(*source);
}

void buildToggle(lv_obj_t *parent) {
  for (int i = 0; i < SEG_COUNT; i++) {
    lv_obj_t *segment = makePanel(parent, SEG_W, SEG_H, SEG_H / 2, theme::CARD);
    lv_obj_set_pos(segment, SEG_X + i * (SEG_W + SEG_GAP), SEG_Y);
    lv_obj_set_style_border_width(segment, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(segment, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *label = makeLabel(segment, &font_inter_12, theme::NAV_INACTIVE);
    lv_label_set_text(label, coinTicker((Coin)i));
    lv_obj_center(label);

    segments[i] = segment;
    segmentLabels[i] = label;

    // Added after the segment so it sits above it in the z-order and takes the
    // press. It carries no fill, so nothing about it is visible.
    lv_obj_t *hit = lv_obj_create(parent);
    lv_obj_remove_style_all(hit);
    lv_obj_set_size(hit, HIT_W, HIT_H);
    lv_obj_set_pos(hit, SEG_X + i * (SEG_W + SEG_GAP) - SEG_GAP,
                   SEG_Y + (SEG_H - HIT_H) / 2);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit, onSegmentPressed, LV_EVENT_CLICKED,
                        (void *)(intptr_t)i);
  }
}

}  // namespace

void buildCryptoScreen(lv_obj_t *parent) {
  // Built by hand rather than through makeTitle(), because both the word and
  // the badge beside it change when the toggle is tapped and makeTitle() keeps
  // no handle on either. Same face, same size, same baseline as every other
  // screen's heading -- the geometry is copied from widgets.h deliberately.
  titleIcon = lv_image_create(parent);
  lv_image_set_src(titleIcon, titleBadgeFor(selected));
  lv_obj_set_pos(titleIcon, 10, 3);

  titleLabel = makeLabel(parent, &font_inter_22_bold, theme::TEXT);
  lv_label_set_text(titleLabel, coinName(selected));
  lv_obj_set_pos(titleLabel, 44, 4);

  buildToggle(parent);

  hero = makeCard(parent, PAD, HERO_Y, HERO_W, HERO_H);

  priceLabel = makeLabel(hero, &font_inter_36_bold, theme::TEXT);
  lv_label_set_text(priceLabel, format::UNKNOWN);
  lv_obj_set_pos(priceLabel, HERO_PAD, PRICE_Y);

  pairLabel = makeLabel(hero, &font_inter_17, theme::MUTED);
  lv_label_set_text(pairLabel, format::UNKNOWN);
  lv_obj_set_pos(pairLabel, HERO_PAD, PAIR_Y);

  glyph = lv_image_create(hero);
  lv_image_set_src(glyph, badgeFor(selected));
  lv_obj_set_pos(glyph, GLYPH_X, GLYPH_Y);

  // The three figures a day of trading is read by. The percentage says how far
  // it moved, the dollars say what that was worth, and the volume says how much
  // conviction was behind it -- one without the others is half a sentence.
  buildTile(tiles[0], parent, 0, "24h %");
  buildTile(tiles[1], parent, 1, "24h change");
  buildTile(tiles[2], parent, 2, "24h volume");

  footnote = makeLabel(parent, &font_inter_12, theme::MUTED);
  lv_label_set_text(footnote, "");
  lv_obj_set_pos(footnote, PAD + 4, FOOT_Y);

  paintIdentity();

  lv_obj_update_layout(parent);
  warnIfCollapsed("crypto hero", hero);
  warnIfCollapsed("crypto toggle", segments[0]);
}

void updateCryptoScreen(const AppModel &model) {
  // Recorded before anything is drawn, so the toggle can repaint from it even
  // if the first thing that happens after boot is a tap.
  source = &model;

  const CoinQuote &quote = model.crypto.coin(selected);
  const FeedStatus &status = model.status(Feed::Crypto);

  // The provenance line is rewritten whether or not the price moved: while the
  // Feed is broken it tracks the outage rather than the market, and while it is
  // healthy it ages. setFootnote() does the comparing.
  //
  // ASCII only, separator included. The generated faces carry 0x20..0x7E plus
  // the degree sign and nothing else -- see tools/mkfont_lvgl.py -- so a middle
  // dot would render as the missing-glyph box on the panel while looking
  // perfectly correct here.
  char note[64];
  if (quote.trusted) {
    char age[24];
    format::since(status.lastSuccessMs, millis(), age, sizeof(age));
    snprintf(note, sizeof(note), "CoinGecko - updated %s", age);
    setFootnote(note, theme::MUTED);
  } else {
    format::feedNote(status, millis(), note, sizeof(note));
    setFootnote(note, theme::AMBER);
  }

  if (sameAsShown(quote, selected)) return;
  shown = quote;
  shownCoin = selected;
  everShown = true;

  char text[24];
  format::price(quote.priceUsd, quote.trusted, text, sizeof(text));
  lv_label_set_text(priceLabel, text);

  if (!quote.trusted) {
    for (Tile &tile : tiles) setTile(tile, format::UNKNOWN, theme::MUTED);
    return;
  }

  // Direction before digits: the colour is readable from further away than the
  // number is, and the two figures that carry it are the same fact told in two
  // units, so they take the same tint rather than being coloured separately.
  const uint32_t tint = quote.change24hPct >= 0.0f ? theme::GREEN : theme::RED;

  format::signedPercent(quote.change24hPct, true, text, sizeof(text));
  setTile(tiles[0], text, tint);

  format::signedPrice(format::change24hUsd(quote.priceUsd, quote.change24hPct),
                      quote.priceUsd, true, text, sizeof(text));
  setTile(tiles[1], text, tint);

  // Volume has no direction, so it stays in the text colour. Tinting it to
  // match its neighbours would claim the day's trading was itself up or down.
  format::compactUsd(quote.volume24hUsd, true, text, sizeof(text));
  setTile(tiles[2], text, theme::TEXT);
}

}  // namespace ui
