# Data sources — OpenSky and hexdb.io

Where the aircraft come from (OpenSky Network) and how they get names (hexdb.io). The code is in
`opensky_client` / `opensky_auth` / `opensky_quota` and `aircraft_lookup` / `route_lookup` /
`lookup_cache` / `lookup_store` / `lookup_budget` / `hexdb_http`, all on core 0 (`net_task`).
Reasons and old mistakes: [history.md](history.md).

## OpenSky Network REST API

Base URL `https://opensky-network.org/api`. One endpoint: `GET /states/all` with a bounding box
(`lamin`, `lomin`, `lamax`, `lomax`, decimal degrees) centred on the home location
(`radius_deg`, default 2.5°). Fetched with HTTP/1.0 (`useHTTP10(true)`) and parsed straight
from the socket stream — see history.md "HTTP/1.0 for /states/all". TLS is not verified
(`setInsecure()`).

Anonymous access works (lower limits); an account is an optional upgrade. Wrong credentials
never stop the data — see "OpenSky login failure" below.

### Authentication — OAuth2 client credentials (not Basic Auth)

Since March 2026 OpenSky accepts **only** the OAuth2 `client_credentials` flow; username /
password Basic Auth (older tutorials, micro-radar's README) no longer works.

1. The user enters `client_id` + `client_secret` on the settings page (created on
   opensky-network.org → Account). Stored in NVS, never in source, never echoed into the page.
2. Token:
   ```
   POST https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token
   Content-Type: application/x-www-form-urlencoded
   Body: grant_type=client_credentials&client_id=...&client_secret=...
   ```
   Response: `access_token`, `expires_in` (~1800 s).
3. `Authorization: Bearer <token>` on every `/states/all` call.
4. The token is cached and refreshed **proactively**, 60 s before it expires
   (`tokenNeedsRefresh()`), not after a `401`. A `401`/`403` on a Bearer request = refresh once
   and retry; still rejected → `LoginFailed` and the same poll again, anonymously. `AuthError`
   only when an anonymous request is rejected.

### Rate limits / credits

Check the current values on opensky-network.org if this misbehaves — they can change.

- Daily credits: anonymous **400**, account **4000** (`openSkyDailyLimit(auth)`; 400 in
  `LoginFailed`, since the board then runs anonymously).
- Cost per `/states/all` call by bounding-box area: ≤ 25 sq° = 1 credit, 25–100 = 2,
  100–400 = 3, > 400 = 4. The default radius (±2.5°) stays in the 1-credit tier; the Location
  tab shows the cost per request, the OpenSky tab the estimate per day
  (`openSkyDailyCredits(radius, poll, pausedMinutesPerDay())`) against the limit.
- Poll interval default 15 s, configurable. Never faster than the interval, not even after a
  manual action.
- `429` = credits exhausted: back off by `X-Rate-Limit-Retry-After-Seconds` if present, else a
  fixed 60 s; the backoff is capped at 3600 s (`max_backoff_s`).
- **Credits left are tracked** (`opensky_quota`, pure + tested): every 200 carries
  `X-Rate-Limit-Remaining`, a 429 the seconds until the pool renews. `opensky_client` keeps the
  latest state (`openSkyQuota()`) and logs it:
  `ok: 12 aircraft, credits remaining=3971 (account)` /
  `rate limited (429): credits exhausted, pool renews in 25012s (~6 h 57 min), next try in 3600s`.
  The OpenSky tab shows it (`quotaStatusText()`).
- 15 s polls = 5760 credits/day, more than an account's 4000. Ways out: a longer interval, or
  *Pause data fetching at night* (architecture.md "Brightness & night mode"; 23:00–06:30 at
  15 s = 1800 requests less). An over-budget setting raises a warning "!"
  (`OpenSkyBudgetOverLimit`, architecture.md "Device alerts").

### OpenSky login failure — anonymous fallback

`opensky_auth` (pure, RAM only — a reboot re-learns it with one token request). Spec:
`spec/done/06-opensky-login-fallback.md`.

- **State** `OpenSkyAuthState`: `mode` `NoCredentials` / `Account` / `LoginFailed`,
  `lastHttpCode`, `credentialsHash` (FNV-1a of id + secret, no copy of the secret),
  `failedSinceMs` (first rejection of the series), `nextTokenTryMs`, `tokenGranted`.
- **Token answer** → `classifyTokenResponse()`: 200 + `access_token` = `Ok`; 400/401/403 =
  `Rejected`; anything else (0, −1, 5xx, 200 without a token) = `Transient`.
  `onTokenResult()`: Ok → `Account`; Rejected → `LoginFailed`, next try now + 30 min
  (`kLoginRetryMs`), `failedSinceMs` kept on a repeated rejection; Transient in `Account` →
  unchanged (a network blip is not a wrong password); Transient in `LoginFailed` → another
  30 min. `NoCredentials` never changes on a token result. `onCredentials()`: new credentials
  saved on the page → `Account` at once, a fair try.
- **Around a poll** (`opensky_client::fetchAircraftStates()`, decisions pure):
  `shouldTryToken()` (in `LoginFailed` only after `nextTokenTryMs`, wrap-safe);
  `planStatesRequest(auth, tokenValid)` = Bearer only in `Account` with a valid token;
  `afterStatesResponse()`: 401/403 with Bearer → `RefreshTokenAndRetry` (skipped if the token
  was fetched this poll), then `RetryAnonymous` (→ `LoginFailed`); 401/403 anonymous →
  `AuthError`; anything else → `Accept`. At most two extra requests per poll.
- The cached token is dropped on `Rejected` and **before** a refresh: a refresh with no answer
  sends the retry anonymously and keeps `Account`, instead of re-sending a rejected token and
  landing in `LoginFailed` for 30 min over a network blip.
- **Log** (`authChangeLogLine()`, once per login result; `login ok` only for the first token
  for these credentials): `[opensky_client] login failed (http=401) — check client_id/secret;
  using anonymous access, next try in 30 min` / `[opensky_client] login ok — using account`;
  a rejected anonymous request: `/states/all rejected (http=401, anonymous)`. On the board, "no
  token requests for 30 min" = no `token fetch failed` line in that time.
- **Settings page, OpenSky tab:** the page applies `onCredentials(model.auth, hash(cfg), …)`,
  so credentials just saved read as `Account`. Pill: grey "Anonymous mode" / green "OpenSky
  account configured" / red `.pill.err` "Login failed — anonymous mode". In `LoginFailed` a
  `.stat.quota.warn#loginwarn` line (`loginFailedDetailText()`): HTTP code, ", since
  DD.MM.YYYY HH:MM" (only with a synced clock and a series younger than 40 days —
  `loginFailedSinceKnown()`, millis() wraps after 49.7 days), 400 instead of 4000, "next login
  attempt in N min" (rounded up, ≥ 1). `#daily` gets `data-lf="1"` so the live JS keeps 400
  until the client id or secret is edited.
- **Display:** with wrong credentials the poll ends `Ok` anonymously — aircraft, green dot,
  red "!" (`OpenSkyLoginFailed`). "BŁĄD LOGOWANIA OPENSKY" only when even anonymous access is
  rejected. The "!" message and the web alert card stay generic; details are on the OpenSky
  tab only.

## Aircraft identification — airline and type

The point of the project ("what plane is that?"). A state vector has only `icao24` and
`callsign`; the callsign (`LOT281`) is the flight, airline and type need a lookup.

- **hexdb.io** (free, keyless, community-run): `GET https://hexdb.io/api/v1/aircraft/{icao24}`
  ```json
  {"ICAOTypeCode":"A319","Manufacturer":"Airbus","ModeS":"4010EE",
   "OperatorFlagCode":"EZY","RegisteredOwners":"easyJet Airline","Registration":"G-EZBZ",
   "Type":"A319 111"}
  ```
  `RegisteredOwners` = airline; the type shown is the friendly name from `ICAOTypeCode`
  (`aircraft_names`: `B38M` → "737 MAX 8", ≤ 12 chars), else hexdb.io's `Type`.
- **Not found** (`{"status":"404","error":"Aircraft not found."}` — military, private, new
  registrations): a confirmed miss, cached too. The airline then comes from the callsign's ICAO
  prefix (`aircraft_names`: `SXS6JZ` → SunExpress; 3 letters + digit only, so registrations
  like `SPKAX` are left alone), otherwise `--`.
- Never block a row on a lookup: show what's cached, `...` while pending, `--` when unknown.

## Route & airport lookup

Same provider, two more endpoints. hexdb.io has the ICAO and IATA variants of both; the code
uses **ICAO**: OpenSky callsigns (`DLH9LH`) are ICAO-style, and the route answer is ICAO codes.

- **Route:** `GET https://hexdb.io/api/v1/route/icao/{callsign}` →
  `{"flight":"EIN17A","route":"EIDW-EGLL","updatetime":1397991739}`; `route` split on `-` gives
  origin / destination ICAO codes. Not found (`{"status":"404","error":"Route not found."}`,
  common for general aviation) → `--` for SKĄD / DOKĄD.
- **Airport:** `GET https://hexdb.io/api/v1/airport/icao/{code}` →
  `{"airport":"Heathrow Airport","country_code":"GB","iata":"LHR","icao":"EGLL",...}`. Gives the
  IATA code the screens show (table, panels) and the country code of the detail views. No
  record → the ICAO code is shown instead (`rowAirportCode()` / `formatRowRoute()`), country
  `--`.
- **Place names** for the detail views (`place_names`): a built-in IATA → city table (PL + EN,
  ~120 airports likely over Poland), else hexdb.io's airport name minus "International
  Airport", ASCII-folded (`asciiFold()`, hexdb.io names contain any accent), else IATA / ICAO.
  hexdb.io has no city field, hence the table.

## Caching and the per-poll budget

hexdb.io is a hobby service with no SLA (informal limit ~1000 requests / 5 min) — be a good
citizen, cache everything, never re-fetch a known key.

- **Keys:** aircraft by `icao24` (an airframe's airline / type never changes), routes by
  callsign, airports by code. Each is looked up once, ever.
- **Three levels:** RAM (`lookup_cache`, LRU — 96 aircraft, 96 routes, 64 airports), then NVS
  (`lookup_store`, namespaces `lk_ac`, `lk_rt`, `lk_ap`; record = version + fields joined by
  0x1F; confirmed misses stored too), then hexdb.io. An entry that falls out of RAM comes back
  from NVS, not from hexdb.io. A RAM capacity must hold one poll's working set. When NVS is
  full, that kind's namespace is wiped (settings live in their own namespace).
- **LRU detail:** `LookupCache::find()` refreshes an entry, `contains()` doesn't — the per-poll
  budget asks about every aircraft, and asking isn't using. `put()` drops the least recently
  used when full.
- **Budget** (`lookup_budget`, `planLookups()`, nearest rows first): per poll all cache hits
  (RAM or NVS, free) plus `kDefaultLookupBudget` (2) new aircraft / route fetches and
  `kDefaultAirportLookupBudget` (3) new airport fetches. The rest shows `...` and gets its turn
  on a later poll; a busy sky resolves within a few polls.
- **Two snapshots per poll** (`poll_pipeline`): phase A right after `/states/all` from caches
  only, phase B with the budgets — see architecture.md "Concurrency — two cores".
- **Keep-alive** (`hexdb_http`): one TLS session per phase B; a request on a connection the
  server closed is retried once, fresh.
- RAM cache sizes are in the `[perf]` line and `/perf.json` (`cache ac/rt/ap`); they must level
  off at the capacities, not grow with uptime.
