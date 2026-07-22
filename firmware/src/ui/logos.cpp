#include "logos.h"

#include <cstring>

#include "ui_icons.h"

namespace ui {
namespace {

// One row per coin whose logo is baked, keyed by CoinGecko id. A catalogued
// coin missing here is not an error -- it draws the generic crypto glyph until
// its artwork is added to tools/mkicons and a row lands here.
struct CoinLogo {
  const char *id;
  const lv_image_dsc_t *badge;
  const lv_image_dsc_t *header;
};
const CoinLogo COIN_LOGOS[] = {
    {"bitcoin", &coin_btc, &coin_btc_hdr},
    {"ethereum", &coin_eth, &coin_eth_hdr},
    {"binancecoin", &coin_bnb, &coin_bnb_hdr},
    {"solana", &coin_sol, &coin_sol_hdr},
    {"ripple", &coin_xrp, &coin_xrp_hdr},
    {"cardano", &coin_ada, &coin_ada_hdr},
    {"dogecoin", &coin_doge, &coin_doge_hdr},
    {"polkadot", &coin_dot, &coin_dot_hdr},
    {"litecoin", &coin_ltc, &coin_ltc_hdr},
    {"chainlink", &coin_link, &coin_link_hdr},
    {"tron", &coin_trx, &coin_trx_hdr},
    {"avalanche-2", &coin_avax, &coin_avax_hdr},
};

// One row per stock whose logo is baked, keyed by Finnhub symbol. `recolor` is
// for the marks baked as a single flat tone (a white or a near-black
// silhouette) that would otherwise vanish on one of the two cards: those are
// retinted to the theme's TEXT at runtime. The rest carry their own brand
// colour, which reads on both cards -- and must NOT be recoloured, because
// runtime recolour over a multi-colour RGB565A8 image fills the whole box (it
// ignores the alpha plane), which is what turned the coloured marks into solid
// white blocks. Recolour only the flat silhouettes; leave the colourful ones.
struct StockLogo {
  const char *symbol;
  const lv_image_dsc_t *logo;
  bool recolor;
};
const StockLogo STOCK_LOGOS[] = {
    {"AAPL", &logo_aapl, true},   // white silhouette
    {"NVDA", &logo_nvda, false},  {"TSLA", &logo_tsla, false},
    {"GOOG", &logo_goog, false},  {"MSFT", &logo_msft, false},
    {"AMZN", &logo_amzn, true},   // near-black silhouette
    {"META", &logo_meta, false},  {"NFLX", &logo_nflx, false},
    {"AMD", &logo_amd, false},    {"INTC", &logo_intc, false},
    {"COIN", &logo_coin, false},  {"PYPL", &logo_pypl, false},
};

const CoinLogo *findCoin(const char *id) {
  for (const CoinLogo &c : COIN_LOGOS)
    if (strcmp(c.id, id) == 0) return &c;
  return nullptr;
}

}  // namespace

const lv_image_dsc_t *coinLogo(const char *id) {
  const CoinLogo *c = findCoin(id);
  return c ? c->badge : &icon_crypto;  // generic coins glyph as the fallback
}

const lv_image_dsc_t *coinLogoHdr(const char *id) {
  const CoinLogo *c = findCoin(id);
  return c ? c->header : &icon_crypto_hdr;
}

const lv_image_dsc_t *stockLogo(const char *symbol, bool &recolorToText) {
  for (const StockLogo &s : STOCK_LOGOS) {
    if (strcmp(s.symbol, symbol) == 0) {
      recolorToText = s.recolor;
      return s.logo;
    }
  }
  recolorToText = true;      // the generic glyph is a flat mark, safe to retint
  return &icon_stock;        // generic stock glyph as the fallback
}

}  // namespace ui
