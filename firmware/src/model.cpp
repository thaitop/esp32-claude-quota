#include "model.h"

// Host-compilable alongside model.h -- no Arduino headers here either.

const char *describe(FetchOutcome outcome) {
  switch (outcome) {
    case FetchOutcome::Never:       return "no data yet";
    case FetchOutcome::Ok:          return "ok";
    case FetchOutcome::Offline:     return "offline";
    case FetchOutcome::Unreachable: return "unreachable";
    case FetchOutcome::Rejected:    return "rejected";
    case FetchOutcome::Malformed:   return "malformed";
  }
  return "unknown";
}
