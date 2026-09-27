// SPDX-License-Identifier: GPL-3.0-or-later
#include "AccentCharacters.h"
namespace ui {
namespace accents {
static const char *const kAccA[] = {"à", "á", "â", "ă", "ä",
                                    "ã", "å", "ą", "æ"}; // æ: Danish/Norwegian (#237)
static const char *const kAccA_u[] = {"À", "Á", "Â", "Ă", "Ä", "Ã", "Å", "Ą", "Æ"};
static const char *const kAccE[] = {"è", "é", "ê", "ë", "ě", "ę"};
static const char *const kAccE_u[] = {"È", "É", "Ê", "Ë", "Ě", "Ę"};
static const char *const kAccI[] = {"ì", "í", "î", "ï"};
static const char *const kAccI_u[] = {"Ì", "Í", "Î", "Ï"};
static const char *const kAccO[] = {"ò", "ó", "ô", "ö", "õ", "ø"};
static const char *const kAccO_u[] = {"Ò", "Ó", "Ô", "Ö", "Õ", "Ø"};
static const char *const kAccU[] = {"ù", "ú", "û", "ü", "ů"};
static const char *const kAccU_u[] = {"Ù", "Ú", "Û", "Ü", "Ů"};
static const char *const kAccN[] = {"ñ", "ń"};
static const char *const kAccN_u[] = {"Ñ", "Ń"};
static const char *const kAccC[] = {"ç", "č", "ć"};
static const char *const kAccC_u[] = {"Ç", "Č", "Ć"};
static const char *const kAccS[] = {"ß", "ś", "š", "ş"};
static const char *const kAccY[] = {"ý", "ÿ"};
// Czech carons / ring — base letters that otherwise carry no Latin-1 accent.
static const char *const kAccT[] = {"ť", "ţ"};
static const char *const kAccT_u[] = {"Ť", "Ţ"};
static const char *const kAccZ[] = {"ž", "ż", "ź"};
static const char *const kAccZ_u[] = {"Ž", "Ż", "Ź"};
static const char *const kAccR[] = {"ř"};
static const char *const kAccR_u[] = {"Ř"};
static const char *const kAccL[] = {"ł"};
static const char *const kAccL_u[] = {"Ł"};
static const char *const kAccS_u[] = {"Ś", "Š", "Ş"};
const AccentSet kAccentSets[25] = {
    {'a', kAccA, 9},   {'A', kAccA_u, 9}, {'e', kAccE, 6},   {'E', kAccE_u, 6}, {'i', kAccI, 4},
    {'I', kAccI_u, 4}, {'o', kAccO, 6},   {'O', kAccO_u, 6}, {'u', kAccU, 5},   {'U', kAccU_u, 5},
    {'n', kAccN, 2},   {'N', kAccN_u, 2}, {'c', kAccC, 3},   {'C', kAccC_u, 3}, {'s', kAccS, 4},
    {'S', kAccS_u, 3}, {'y', kAccY, 2},   {'t', kAccT, 2},   {'T', kAccT_u, 2}, {'z', kAccZ, 3},
    {'Z', kAccZ_u, 3}, {'l', kAccL, 1},   {'L', kAccL_u, 1}, {'r', kAccR, 1},   {'R', kAccR_u, 1},
};
const AccentSet *lookup(const char *key) {
  if (!key || !key[0] || key[1])
    return nullptr; // single ASCII-char keys only
  for (const auto &s : kAccentSets)
    if (s.key == key[0])
      return &s;
  return nullptr;
}

} // namespace accents
} // namespace ui
