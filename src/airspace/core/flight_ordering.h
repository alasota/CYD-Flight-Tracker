// flight_ordering — the order every screen shows the tracked flights in
// (airborne first, nearest first) and the split into Screen 1's featured
// flight and the table below it (docs/screens.md "Screen 1 — Flights").
//
// Pure — tested under `pio test -e native`.
#pragma once

#include <vector>

#include "airspace/core/tracked_flight.h"  // AircraftRow

// Sorts `rows` ascending by distance_km — closest aircraft first, per
// docs/screens.md "Screen 1" (closest aircraft on top). Call
// annotateDistances() first. Rows with has_distance == false sort last
// (nothing to rank them by); otherwise stable (equal distances keep their
// relative order).
void sortRowsByDistance(std::vector<AircraftRow> &rows);

// The order every screen shows (improvement #9): aircraft in the air first,
// nearest first; then aircraft on the ground (taxiing / parked at the nearby
// airport), nearest first; then rows without a position. So the featured
// slot on Screen 1 and the Screen 3 summary show something you can actually
// see in the sky, and the per-poll lookup budget is spent on it first.
// Stable within each group.
void sortRowsForDisplay(std::vector<AircraftRow> &rows);

// Result of splitting a (sorted) AircraftRow list into "the one closest
// aircraft, for featured_panel" and "everything else, for the table
// below it".
struct FeaturedSplit {
  bool hasFeatured = false;
  AircraftRow featured;           // valid only when hasFeatured
  std::vector<AircraftRow> rest;  // everything else, order preserved
};

// Splits `rows` (already sorted ascending by distance —
// sortRowsByDistance()) into its first element (the closest aircraft) and
// the remainder. Empty input -> hasFeatured=false, empty rest. A
// single-row input -> that row featured, rest empty. Pure.
FeaturedSplit splitFeaturedAndRest(const std::vector<AircraftRow> &rows);
