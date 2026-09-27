// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace ui {
enum class ContactSort : uint8_t { Name = 0, LastHeard = 1, LastMessage = 2, Distance = 3 };
struct ContactEntry {
    int      mesh_idx;
    bool     is_repeater;
    bool     is_room;
    bool     has_dir;
    bool     has_gps;
    bool     is_fav;
    bool     is_blocked;   // on the ignore list -> red person icon
    uint32_t last_heard;
    int32_t  gps_lat;   // microdegrees (0 = unknown)
    int32_t  gps_lon;
    uint8_t  key6[6];   // pub_key prefix — stable identity for multi-select
    char     name[40];
    double   distance_km; // computed once per refresh, not in every comparison
};
void sortContacts(ContactEntry* entries, size_t count, ContactSort mode, bool descending,
                  uint32_t now, double latitude, double longitude);
bool contactMatches(uint8_t filter, const char* name, bool is_rep, bool has_location,
                    bool direct, bool is_fav, int fav_count, const char* needle);
}
