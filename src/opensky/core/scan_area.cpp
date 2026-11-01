#include "opensky/core/scan_area.h"

float bboxAreaSqDeg(float radius_deg) {
  float side = 2.0f * radius_deg;
  return side * side;
}

int openSkyCreditCost(float radius_deg) {
  float area = bboxAreaSqDeg(radius_deg);
  if (area <= 25.0f) return 1;
  if (area <= 100.0f) return 2;
  if (area <= 400.0f) return 3;
  return 4;
}
