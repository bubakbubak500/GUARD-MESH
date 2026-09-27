#include "FormFields.h"
#include <cstdlib>
namespace ui { namespace widgets {
bool parseFloatField(lv_obj_t* ta, float& out) {
  if (!ta) return false;
  const char* txt = lv_textarea_get_text(ta);
  if (!txt || !txt[0]) return false;
  // Tolerate a European decimal comma ("50,8466") and any trailing whitespace —
  // strict strtof stops at the comma and rejected such coordinates as invalid.
  char buf[40]; size_t j = 0;
  for (const char* p = txt; *p && j < sizeof(buf) - 1; ++p)
    buf[j++] = (*p == ',') ? '.' : *p;
  buf[j] = '\0';
  char* endptr = nullptr;
  float v = strtof(buf, &endptr);
  if (!endptr || endptr == buf) return false;
  while (*endptr == ' ' || *endptr == '\t' || *endptr == '\r' || *endptr == '\n') ++endptr;
  if (*endptr != '\0') return false;
  out = v;
  return true;
}

bool parseIntField(lv_obj_t* ta, int& out) {
  if (!ta) return false;
  const char* txt = lv_textarea_get_text(ta);
  if (!txt || !txt[0]) return false;
  char* endptr = nullptr;
  long v = strtol(txt, &endptr, 10);
  if (!endptr || endptr == txt || *endptr != '\0') return false;
  out = static_cast<int>(v);
  return true;
}
} }
