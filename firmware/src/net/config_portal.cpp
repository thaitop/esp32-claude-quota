#include "config_portal.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "../config.h"
#include "../model.h"
#include "../secrets.h"
#include "config_store.h"
#include "https.h"

namespace net {
namespace {

// ---------------------------------------------------------------------------
// The page. One file, inlined CSS and JS, no external requests -- the device
// serves it to a browser on a LAN it does not trust to reach a CDN, and a
// blocked stylesheet would leave the form unusable. The inputs validate on blur
// and Save is disabled until at least the browser has authenticated; a field is
// only written if its validation came back green (script keeps that state).
// ---------------------------------------------------------------------------
const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Display Config</title>
<style>
:root{color-scheme:dark}
body{font-family:system-ui,sans-serif;margin:0;background:#0f1115;color:#e6e6e6}
main{max-width:520px;margin:0 auto;padding:20px}
h1{font-size:1.3rem}h2{font-size:1rem;margin:1.4em 0 .4em;color:#9aa4b2}
.row{display:flex;align-items:center;gap:8px;margin:6px 0}
label{flex:0 0 84px;color:#9aa4b2;font-size:.85rem}
input,select{flex:1;padding:8px;border:1px solid #2a2f3a;border-radius:8px;
background:#161a22;color:#e6e6e6;font-size:1rem}
.st{flex:0 0 28px;text-align:center;font-size:1.1rem}
.msg{font-size:.8rem;color:#9aa4b2;margin:2px 0 8px 92px;min-height:1em}
button{padding:10px 16px;border:0;border-radius:8px;font-size:1rem;
cursor:pointer;margin:6px 6px 0 0}
.pri{background:#4f7cff;color:#fff}.sec{background:#2a2f3a;color:#e6e6e6}
#gate{margin:20px 0}#app{display:none}
.err{color:#ff6b6b}.ok{color:#4fd18b}
</style></head><body><main>
<h1>Display Config</h1>
<div id="gate">
<div class="row"><label>PIN</label>
<input id="pin" inputmode="numeric" maxlength="4" placeholder="shown on device">
<button class="pri" onclick="auth()">Unlock</button></div>
<div id="gmsg" class="msg"></div>
</div>
<div id="app">
<h2>Weather location</h2>
<div class="row"><label>City</label>
<input id="city" placeholder="type to change, e.g. Chiang Mai"><span class="st" id="s_city"></span></div>
<div class="msg" id="m_city"></div>
<h2>Coins</h2>
<div id="coins"></div>
<h2>Stocks</h2>
<div id="stocks"></div>
<div style="margin-top:18px">
<button class="pri" onclick="saveAll()">Save &amp; reboot</button>
<button class="sec" onclick="cancel()">Cancel</button></div>
<div class="msg" id="foot"></div>
</div>
<script>
let tok="",cat=null;
const $=id=>document.getElementById(id);
async function post(path,body){body.t=tok;
const r=await fetch(path,{method:"POST",body:new URLSearchParams(body)});return r;}
async function auth(){
const r=await fetch("/auth",{method:"POST",
body:new URLSearchParams({pin:$("pin").value})});
if(r.status!=200){$("gmsg").textContent="Wrong PIN";$("gmsg").className="msg err";return;}
tok=(await r.json()).token;$("gate").style.display="none";$("app").style.display="block";
await loadCatalog();loadState();}
async function loadCatalog(){cat=await(await fetch("/catalog?t="+tok)).json();}
function opts(list,valKey,cur){return list.map(o=>
`<option value="${o[valKey]}"${o[valKey]==cur?" selected":""}>${o.label}</option>`).join("");}
async function loadState(){
const s=await(await fetch("/state?t="+tok)).json();
$("city").value="";
$("m_city").textContent="now: "+s.lat.toFixed(3)+","+s.lon.toFixed(3)+"  "+s.wtz+"  "+s.ctz;
const cd=$("coins");cd.innerHTML="";
s.coins.forEach((id,i)=>{cd.insertAdjacentHTML("beforeend",
`<div class="row"><label>Coin ${i+1}</label>
<select id="co${i}">${opts(cat.coins,"id",id)}</select></div>`);});
const sd=$("stocks");sd.innerHTML="";
s.stocks.forEach((sym,i)=>{sd.insertAdjacentHTML("beforeend",
`<div class="row"><label>Stock ${i+1}</label>
<select id="st${i}">${opts(cat.stocks,"symbol",sym)}</select></div>`);});}
async function validateCity(value){
if(!value.trim()){$("s_city").textContent="";$("m_city").textContent="";return;}
$("s_city").textContent="…";$("m_city").textContent="checking…";$("m_city").className="msg";
const r=await post("/validate",{type:"city",value:value});
if(r.status==409){$("m_city").textContent="busy, try again";return;}
for(let i=0;i<40;i++){await new Promise(z=>setTimeout(z,400));
const st=await(await fetch("/status?t="+tok)).json();
if(st.state=="pending")continue;
if(st.state=="ok"){$("s_city").textContent="✓";$("s_city").className="st ok";
$("m_city").textContent=st.result;$("m_city").className="msg ok";}
else{$("s_city").textContent="✗";$("s_city").className="st err";
$("m_city").textContent=st.error||"not found";$("m_city").className="msg err";}
return;}
$("m_city").textContent="timed out";$("m_city").className="msg err";}
$("city").addEventListener("blur",e=>validateCity(e.target.value));
async function saveAll(){
$("foot").textContent="saving…";
const body={};
document.querySelectorAll("select").forEach(el=>{
const k=el.id.startsWith("co")?"coin"+el.id.slice(2):"stock"+el.id.slice(2);
body[k]=el.value;});
await post("/save",body);await post("/reboot",{});
$("foot").textContent="Saved. Device is rebooting — this page will stop responding.";
$("foot").className="msg ok";}
async function cancel(){await post("/reboot",{});
$("foot").textContent="Rebooting without saving…";}
</script></main></body></html>)HTML";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
WebServer server(80);
bool active = false;
bool rebootRequested = false;
char pin[5] = {0};
char token[9] = {0};
Settings draft;

// One queued validation. The only field that still needs one is the city: the
// coins and stocks are picked from a catalog of known-good ids/symbols (no typo
// to catch), so they are applied straight from the catalog at save time. The
// city is free text turned into coordinates, so it is confirmed against
// Open-Meteo. Set by /validate, run by service() between clients so the
// outbound TLS never overlaps an inbound request.
enum class Job : uint8_t { None, City };
enum class JobState : uint8_t { Idle, Pending, Ok, Fail };
Job jobType = Job::None;
JobState jobState = JobState::Idle;
char jobValue[48] = {0};
char jobResult[96] = {0};
char jobError[48] = {0};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
// Percent-encodes everything that is not an unreserved URL character, so a city
// with a space or an accent survives into the geocoding query.
void urlEncode(const char *in, char *out, size_t n) {
  static const char hex[] = "0123456789ABCDEF";
  size_t o = 0;
  for (; *in && o + 4 < n; in++) {
    const unsigned char c = (unsigned char)*in;
    const bool safe = isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      out[o++] = (char)c;
    } else {
      out[o++] = '%';
      out[o++] = hex[c >> 4];
      out[o++] = hex[c & 0xF];
    }
  }
  out[o] = '\0';
}

// utc_offset_seconds -> the "UTC+7" / "UTC+5:30" / "UTC" shorthand timesync
// parses. A prefill only: the zone's current offset, with no DST rule, which is
// exactly the limitation the clock parser already documents (ADR-0005, Q12).
void offsetToClockTz(long seconds, char *out, size_t n) {
  if (seconds == 0) {
    snprintf(out, n, "UTC");
    return;
  }
  const char sign = seconds < 0 ? '-' : '+';
  long mins = labs(seconds) / 60;
  const long h = mins / 60, m = mins % 60;
  if (m)
    snprintf(out, n, "UTC%c%ld:%02ld", sign, h, m);
  else
    snprintf(out, n, "UTC%c%ld", sign, h);
}

bool tokenOk() {
  if (!server.hasArg("t")) return false;
  return strcmp(server.arg("t").c_str(), token) == 0;
}

// ---------------------------------------------------------------------------
// The validation fetches. Each runs with the feeds stopped and no inbound
// request in flight, so it is the only connection open (ADR-0003/0005).
// ---------------------------------------------------------------------------

// Geocode the city, then probe its current UTC offset. Two GETs, sequential --
// still one connection at a time. On success writes lat/lon/weatherTz/clockTz
// into the draft and a human summary into jobResult.
bool runCity(const char *city) {
  char esc[96];
  urlEncode(city, esc, sizeof(esc));
  char path[160];
  snprintf(path, sizeof(path), GEOCODE_PATH_FMT, esc);

  JsonDocument filter;
  filter["results"][0]["name"] = true;
  filter["results"][0]["latitude"] = true;
  filter["results"][0]["longitude"] = true;
  filter["results"][0]["country"] = true;
  filter["results"][0]["timezone"] = true;

  JsonDocument doc;
  if (fetchJson(GEOCODE_HOST, path, doc, filter) != FetchOutcome::Ok) {
    snprintf(jobError, sizeof(jobError), "geocode failed");
    return false;
  }
  JsonArray results = doc["results"];
  if (results.isNull() || results.size() == 0) {
    snprintf(jobError, sizeof(jobError), "city not found");
    return false;
  }
  JsonObject hit = results[0];
  const float lat = hit["latitude"] | 1000.0f;
  const float lon = hit["longitude"] | 1000.0f;
  const char *tz = hit["timezone"] | "";
  if (lat > 900.0f || lon > 900.0f || tz[0] == '\0') {
    snprintf(jobError, sizeof(jobError), "incomplete result");
    return false;
  }

  // Second GET: the same forecast host, asking only for the offset the zone is
  // on right now, to prefill the clock. A failure here is not fatal -- the
  // location is already good; fall back to a plain "UTC" the user can correct.
  char clockTz[16] = "UTC";
  char probe[224];
  snprintf(probe, sizeof(probe),
           "/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m"
           "&timezone=%s",
           lat, lon, tz);
  JsonDocument offFilter;
  offFilter["utc_offset_seconds"] = true;
  JsonDocument offDoc;
  if (fetchJson(WEATHER_HOST, probe, offDoc, offFilter) == FetchOutcome::Ok) {
    if (offDoc["utc_offset_seconds"].is<long>())
      offsetToClockTz(offDoc["utc_offset_seconds"].as<long>(), clockTz, sizeof(clockTz));
  }

  draft.weatherLat = lat;
  draft.weatherLon = lon;
  strncpy(draft.weatherTz, tz, sizeof(draft.weatherTz) - 1);
  draft.weatherTz[sizeof(draft.weatherTz) - 1] = '\0';
  strncpy(draft.clockTz, clockTz, sizeof(draft.clockTz) - 1);
  draft.clockTz[sizeof(draft.clockTz) - 1] = '\0';

  const char *country = hit["country"] | "";
  snprintf(jobResult, sizeof(jobResult), "%s, %s  (%.3f,%.3f)  %s",
           (const char *)(hit["name"] | "?"), country, lat, lon, clockTz);
  return true;
}

void runPendingJob() {
  jobResult[0] = '\0';
  jobError[0] = '\0';
  bool ok = false;
  switch (jobType) {
    case Job::City: ok = runCity(jobValue); break;
    default: break;
  }
  jobState = ok ? JobState::Ok : JobState::Fail;
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------
void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleAuth() {
  if (server.arg("pin") == pin) {
    JsonDocument out;
    out["token"] = token;
    String body;
    serializeJson(out, body);
    server.send(200, "application/json", body);
  } else {
    server.send(401, "application/json", "{\"error\":\"pin\"}");
  }
}

void handleState() {
  if (!tokenOk()) return server.send(401, "application/json", "{}");
  JsonDocument out;
  out["lat"] = draft.weatherLat;
  out["lon"] = draft.weatherLon;
  out["wtz"] = draft.weatherTz;
  out["ctz"] = draft.clockTz;
  // The current selection per slot: the id the coin dropdown should preselect,
  // and the symbol the stock dropdown should.
  JsonArray coins = out["coins"].to<JsonArray>();
  for (uint8_t i = 0; i < (uint8_t)Coin::Count; i++) coins.add(draft.coinId[i]);
  JsonArray stocks = out["stocks"].to<JsonArray>();
  for (uint8_t i = 0; i < (uint8_t)Ticker::Count; i++) stocks.add(draft.stockSym[i]);
  String body;
  serializeJson(out, body);
  server.send(200, "application/json", body);
}

// The pickable options: every catalogued coin and stock, for the dropdowns to
// list. Separate from /state (the current selection) so the option list is
// fetched once and the selection can be re-read without it.
void handleCatalog() {
  if (!tokenOk()) return server.send(401, "application/json", "{}");
  JsonDocument out;
  size_t n = 0;
  const CoinCatalogEntry *coins = coinCatalog(n);
  JsonArray jc = out["coins"].to<JsonArray>();
  for (size_t i = 0; i < n; i++) {
    JsonObject o = jc.add<JsonObject>();
    o["id"] = coins[i].id;
    o["label"] = coins[i].name;   // shown in the dropdown
    o["ticker"] = coins[i].ticker;
  }
  const StockCatalogEntry *stocks = stockCatalog(n);
  JsonArray js = out["stocks"].to<JsonArray>();
  for (size_t i = 0; i < n; i++) {
    JsonObject o = js.add<JsonObject>();
    o["symbol"] = stocks[i].symbol;
    o["label"] = stocks[i].name;
  }
  String body;
  serializeJson(out, body);
  server.send(200, "application/json", body);
}

void handleValidate() {
  if (!tokenOk()) return server.send(401, "application/json", "{}");
  if (jobState == JobState::Pending)
    return server.send(409, "application/json", "{\"state\":\"busy\"}");

  // Only the city is validated on the wire; coins and stocks are catalog picks.
  if (server.arg("type") != "city")
    return server.send(400, "application/json", "{\"error\":\"type\"}");
  strncpy(jobValue, server.arg("value").c_str(), sizeof(jobValue) - 1);
  jobValue[sizeof(jobValue) - 1] = '\0';

  jobType = Job::City;
  jobState = JobState::Pending;
  server.send(202, "application/json", "{\"state\":\"pending\"}");
}

void handleStatus() {
  if (!tokenOk()) return server.send(401, "application/json", "{}");
  JsonDocument out;
  switch (jobState) {
    case JobState::Pending: out["state"] = "pending"; break;
    case JobState::Ok:      out["state"] = "ok"; out["result"] = jobResult; break;
    case JobState::Fail:    out["state"] = "fail"; out["error"] = jobError; break;
    default:                out["state"] = "idle"; break;
  }
  String body;
  serializeJson(out, body);
  server.send(200, "application/json", body);
}

// Applies the dropdown selections to the draft, then commits. Each coin slot
// carries the chosen CoinGecko id and each stock slot the chosen symbol; the
// ticker and name come from the catalog entry, not the browser, so a slot can
// never end up labelled one thing and fetched as another. A slot whose field is
// absent or not in the catalog keeps its previous value (the city, validated
// separately, is already in the draft).
void handleSave() {
  if (!tokenOk()) return server.send(401, "application/json", "{}");

  for (uint8_t i = 0; i < (uint8_t)Coin::Count; i++) {
    char key[8];
    snprintf(key, sizeof(key), "coin%u", i);
    if (!server.hasArg(key)) continue;
    const CoinCatalogEntry *e = coinCatalogFind(server.arg(key).c_str());
    if (e == nullptr) continue;
    strncpy(draft.coinId[i], e->id, sizeof(draft.coinId[i]) - 1);
    draft.coinId[i][sizeof(draft.coinId[i]) - 1] = '\0';
    strncpy(draft.coinTicker[i], e->ticker, sizeof(draft.coinTicker[i]) - 1);
    draft.coinTicker[i][sizeof(draft.coinTicker[i]) - 1] = '\0';
    strncpy(draft.coinName[i], e->name, sizeof(draft.coinName[i]) - 1);
    draft.coinName[i][sizeof(draft.coinName[i]) - 1] = '\0';
  }
  for (uint8_t i = 0; i < (uint8_t)Ticker::Count; i++) {
    char key[8];
    snprintf(key, sizeof(key), "stock%u", i);
    if (!server.hasArg(key)) continue;
    const StockCatalogEntry *e = stockCatalogFind(server.arg(key).c_str());
    if (e == nullptr) continue;
    strncpy(draft.stockSym[i], e->symbol, sizeof(draft.stockSym[i]) - 1);
    draft.stockSym[i][sizeof(draft.stockSym[i]) - 1] = '\0';
  }

  configCommit(draft);
  server.send(200, "application/json", "{\"saved\":true}");
}

void handleReboot() {
  if (!tokenOk()) return server.send(401, "application/json", "{}");
  server.send(200, "application/json", "{\"reboot\":true}");
  rebootRequested = true;
}

}  // namespace

void configPortalBegin(const char *ip) {
  draft = configDraft();

  // A four-digit PIN from the hardware RNG, shown on the panel: only someone in
  // front of the device can read it, which is what gates the LAN (ADR-0005).
  snprintf(pin, sizeof(pin), "%04u", (unsigned)(esp_random() % 10000u));
  // A session token so the pages that follow /auth do not each resend the PIN.
  snprintf(token, sizeof(token), "%08x", (unsigned)esp_random());

  server.on("/", handleRoot);
  server.on("/auth", HTTP_POST, handleAuth);
  server.on("/state", handleState);
  server.on("/catalog", handleCatalog);
  server.on("/validate", HTTP_POST, handleValidate);
  server.on("/status", handleStatus);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.begin();

  active = true;
  rebootRequested = false;
  jobType = Job::None;
  jobState = JobState::Idle;
  Serial.printf("config portal up at http://%s/  pin=%s\n", ip, pin);
}

bool configPortalActive() { return active; }

void configPortalService() {
  server.handleClient();
  // Only between clients, so the outbound validation TLS is the one connection
  // open -- the browser was already answered "pending" and is polling.
  if (jobState == JobState::Pending) runPendingJob();
}

const char *configPortalPin() { return active ? pin : ""; }

bool configPortalShouldReboot() { return rebootRequested; }

}  // namespace net
