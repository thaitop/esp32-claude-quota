#include "https.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "../config.h"

namespace net {

FetchOutcome fetchJson(const char *host, const char *path, JsonDocument &doc,
                       const JsonDocument &filter) {
  if (WiFi.status() != WL_CONNECTED) return FetchOutcome::Offline;

  // Both scoped so that every exit from this function -- including the early
  // returns below -- runs their destructors and hands the heap back.
  WiFiClientSecure client;
  HTTPClient http;

  client.setInsecure();
  client.setTimeout(HTTP_TIMEOUT_MS / 1000);
  client.setHandshakeTimeout(HTTP_TIMEOUT_MS / 1000);

  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);

  if (!http.begin(client, host, 443, path, true)) return FetchOutcome::Unreachable;
  // CoinGecko's free tier answers an anonymous client with a 403 often enough
  // to matter; a plain identifying agent is enough to stop it.
  http.addHeader("User-Agent", "esp32-claude-quota/1.0");

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    http.end();
    client.stop();
    return status < 0 ? FetchOutcome::Unreachable : FetchOutcome::Rejected;
  }

  // getString() rather than getStream(). Both APIs answer with
  // `Transfer-Encoding: chunked`, and HTTPClient only unwraps the chunk framing
  // on the buffered paths -- parsing the raw stream feeds the hexadecimal chunk
  // lengths to the JSON parser, which reports a malformed body for a response
  // that is perfectly well formed. The bridge sends a Content-Length and can
  // still be streamed; these two cannot.
  //
  // The copy costs a few hundred bytes against the 45KB the handshake already
  // has in flight, and it is freed with the String at the end of this scope.
  const String body = http.getString();
  http.end();
  client.stop();

  const DeserializationError error =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  return error ? FetchOutcome::Malformed : FetchOutcome::Ok;
}

}  // namespace net
