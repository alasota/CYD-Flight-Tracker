// home — where the device stands and how far around it the sky is watched:
// airspace's settings object (spec 12 §7). Config (admin/core/config_store.h)
// holds one as `home`; the pipeline, the OpenSky bounding box, the radar
// rings and the sun times read only this.
//
// Header-only, no logic: the clamps live in config_store (the aggregate keeps
// its invariants). ⚠️ env:cyd is gnu++11 — with these default member
// initializers the struct isn't an aggregate there: assign members, never
// `Home{…}`.
#pragma once

struct Home {
  float lat = 0.0f;
  float lon = 0.0f;
  // Bounding-box half-width in degrees. Default keeps the resulting OpenSky
  // bbox in the 1-credit tier (<=25 sq deg) — see docs/data-sources.md "Rate limits".
  float radius_deg = 2.5f;
  // True once the user has saved the settings form at least once via
  // web_server. (0, 0) is a real place (Gulf of Guinea) — without this
  // flag there's no way to tell "user hasn't configured their home
  // location yet" apart from "user's home really is at 0,0", so the
  // firmware would happily poll/display aircraft around Null Island with
  // no indication anything needs setting up. See docs/review-notes-2026.md
  // 1.5.
  bool configured = false;
};
