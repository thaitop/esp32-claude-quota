# Utilization is reported, never derived

The bridge already counts billable tokens out of Claude Code's transcripts, so
it looks like it could synthesize a utilization percentage whenever the
Utilization Cache is missing or stale. It deliberately does not: Anthropic's
limit accounting is not a plain token sum, and a derived figure would be a guess
wearing the costume of a fact — worse than showing nothing, because the user
would act on it. When utilization cannot be read from the cache, the display
shows Unknown (`--`) and the Token Total is reported separately under its own
name.

## Consequences

The screen goes blank-ish whenever the Claude Usage app stops maintaining
`~/.claude/.statusline-usage-cache`, and there is no fallback that makes it look
alive. That is intended: the failure is visible rather than silently wrong.
