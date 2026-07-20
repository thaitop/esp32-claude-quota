#include "poller.h"

#include <Arduino.h>

#include "../config.h"

namespace net {
namespace {

struct Entry {
  FetchFn fetch = nullptr;
  uint32_t intervalMs = 0;
  uint32_t dueAtMs = 0;
  bool registered = false;
};

Entry entries[(size_t)Feed::Count];

uint32_t refreshes = 0;

// How long to wait after a failure. Backs off so a bridge that is down does
// not mean a request every five seconds for the rest of the day, but stays
// responsive enough that recovery is noticed quickly.
uint32_t retryDelay(uint16_t consecutiveFailures) {
  const uint32_t capped = consecutiveFailures > 5 ? 5 : consecutiveFailures;
  return POLL_RETRY_MS * (1u << capped);  // 5s, 10s, 20s ... 160s
}

}  // namespace

void registerFeed(Feed feed, FetchFn fetch, uint32_t intervalMs) {
  Entry &entry = entries[(size_t)feed];
  entry.fetch = fetch;
  entry.intervalMs = intervalMs;
  entry.dueAtMs = 0;  // due immediately at boot
  entry.registered = true;
}

void refreshAll() {
  for (Entry &entry : entries) entry.dueAtMs = 0;
  refreshes++;
}

uint32_t refreshGeneration() { return refreshes; }

bool service(AppModel &model, uint32_t nowMs) {
  // Pick the most overdue feed rather than the first one due, so a fast feed
  // cannot starve a slow one that came due while it was busy.
  int chosen = -1;
  uint32_t worstOverdue = 0;

  for (size_t i = 0; i < (size_t)Feed::Count; i++) {
    const Entry &entry = entries[i];
    if (!entry.registered) continue;
    if ((int32_t)(nowMs - entry.dueAtMs) < 0) continue;

    const uint32_t overdue = nowMs - entry.dueAtMs;
    if (chosen < 0 || overdue > worstOverdue) {
      chosen = (int)i;
      worstOverdue = overdue;
    }
  }

  if (chosen < 0) return false;

  Entry &entry = entries[chosen];
  FeedStatus &status = model.status((Feed)chosen);

  status.lastAttemptMs = nowMs;
  const FetchOutcome outcome = entry.fetch(model);
  status.outcome = outcome;

  if (outcome == FetchOutcome::Ok) {
    status.lastSuccessMs = nowMs;
    status.consecutiveFailures = 0;
    entry.dueAtMs = nowMs + entry.intervalMs;
  } else {
    if (status.consecutiveFailures < 0xFFFF) status.consecutiveFailures++;
    entry.dueAtMs = nowMs + retryDelay(status.consecutiveFailures);
  }

  return true;
}

}  // namespace net
