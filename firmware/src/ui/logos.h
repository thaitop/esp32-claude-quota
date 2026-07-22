// The bitmap logo for a coin id or a stock symbol.
//
// The Crypto and Stock screens used to pick their artwork by the enum slot --
// row 0 always drew Bitcoin, row 0 of the stock list always drew Apple. Config
// Mode broke that: a slot can now hold any catalogued coin or stock, so the
// logo has to follow the id/symbol, not the position. These look it up by
// string against the set of logos actually baked into ui_icons, and fall back
// to the screen's generic glyph for a catalog entry whose artwork is not yet
// added -- a generic mark, never a wrong one (ADR-0005).
//
// Keep the tables in logos.cpp in step with config_store's catalog and with
// tools/mkicons' MANIFEST: an entry gains a real logo when all three name it.
#pragma once

#include <lvgl.h>

namespace ui {

// The hero/toggle badge for a coin, and the smaller header copy. `id` is the
// CoinGecko id the settings store (e.g. "bitcoin").
const lv_image_dsc_t *coinLogo(const char *id);
const lv_image_dsc_t *coinLogoHdr(const char *id);

// The row logo for a stock symbol (e.g. "AAPL"), and whether it is a white
// silhouette that must be recoloured to read on the light card -- true for the
// generic fallback and for marks baked as monochrome.
const lv_image_dsc_t *stockLogo(const char *symbol, bool &recolorToText);

}  // namespace ui
