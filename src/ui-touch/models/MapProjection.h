// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
namespace ui { namespace maps {
void latLonToWorldPx(double lat, double lon, uint8_t zoom, double* x, double* y);
void worldPxToLatLon(double x, double y, uint8_t zoom, double* lat, double* lon);
int gridRadius(int dimension);
} }
