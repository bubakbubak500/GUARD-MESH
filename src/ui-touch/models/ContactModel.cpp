#include "ContactModel.h"
#include "LocationModel.h"
#include <algorithm>
#include <cstring>
namespace ui {
static int compareNames(const char* a, const char* b) {
  while (*a || *b) {
    unsigned char ca = static_cast<unsigned char>(*a++), cb = static_cast<unsigned char>(*b++);
    if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
    if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
    if (ca != cb) return ca < cb ? -1 : 1;
  }
  return 0;
}
void sortContacts(ContactEntry* entries, size_t count, ContactSort mode, bool descending,
                  uint32_t now, double latitude, double longitude) {
  if (!entries || count < 2) return;
  if (mode == ContactSort::Distance) {
    for (size_t i = 0; i < count; ++i)
      entries[i].distance_km = distanceKm(latitude, longitude, entries[i].gps_lat / 1.0e6, entries[i].gps_lon / 1.0e6);
  }
  std::sort(entries, entries + count, [=](const ContactEntry& a, const ContactEntry& b) {
    const ContactEntry* ea = &a;
    const ContactEntry* eb = &b;
    // Favorites always sort above non-favorites regardless of the category filter
    // OR the asc/desc flip. Operator's starred contacts should be the first thing
    // visible whether they're filtering by RPT, Peer, or anything.
    if (ea->is_fav != eb->is_fav) {
      return ea->is_fav;
    }
    // `prim` < 0 means a comes before b in the mode's NATURAL order; the asc/desc
    // toggle (descending) flips just this primary key at the very end.
    int prim = 0;
    if (mode == ContactSort::Distance) {
      // Nearest first. No-location rows are excluded upstream, so both have a fix.
      const double da = ea->distance_km;
      const double db = eb->distance_km;
      if (da < db) prim = -1; else if (da > db) prim = 1;
    } else {
      // Contacts whose last-heard renders as "?" sink to the end in every non-distance
      // mode — structurally, NOT affected by the asc/desc flip. "?" is NOT just
      // last_heard==0: formatAgeBadge also shows it for a future/garbage timestamp
      // (RTC unset -> now <= last_heard) or one over 400 days old. Mirror that exact
      // condition using the captured clock so the sort matches what the row displays.
      const uint32_t a_age = (now > ea->last_heard && ea->last_heard != 0) ? (now - ea->last_heard) : 0;
      const uint32_t b_age = (now > eb->last_heard && eb->last_heard != 0) ? (now - eb->last_heard) : 0;
      const bool a_unknown = (a_age == 0 || a_age > (uint32_t)400 * 24u * 3600u);
      const bool b_unknown = (b_age == 0 || b_age > (uint32_t)400 * 24u * 3600u);
      if (a_unknown != b_unknown) return !a_unknown;
      if (mode == ContactSort::LastHeard ||
          mode == ContactSort::LastMessage) {
        if (ea->last_heard != eb->last_heard)
          prim = (ea->last_heard > eb->last_heard) ? -1 : 1;   // newer first (natural)
      }
    }
    if (prim == 0) prim = compareNames(ea->name, eb->name);   // A-Z natural order / tiebreak
    if (prim == 0) prim = memcmp(ea->key6, eb->key6, 6);    // stable final tiebreak → deterministic 128-row cut (#73)
    return descending ? prim > 0 : prim < 0;

  });
}
bool contactMatches(uint8_t filter, const char* name, bool is_rep, bool has_location,
                    bool direct, bool is_fav, int fav_count, const char* needle) {
  if (!name) return false;

  switch(filter){
    case 1u: if(!is_rep) return false; break;                              // repeaters
    case 2u: if(is_rep)  return false; break;                              // peers
    case 3u: if(fav_count>0 && !is_fav) return false; break;               // favorites
    case 4u: if(!(has_location)) return false; break;      // has location
    case 5u: if(!direct) return false; break;                  // 0-hop / direct neighbors only
    default: break;                                                        // 0 = all
  }
  if(needle && needle[0]){
    char nl[40]; int nn=(int)strlen(name); if(nn>(int)sizeof(nl)-1) nn=(int)sizeof(nl)-1;
    for(int j=0;j<nn;++j){ char ch=name[j]; if(ch>='A'&&ch<='Z') ch=(char)(ch-'A'+'a'); nl[j]=ch; }
    nl[nn]='\0';
    if(!strstr(nl,needle)) return false;
  }
  return true;

}
}
