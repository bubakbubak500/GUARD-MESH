// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace guardian {
struct Draft {
  std::string to, subject, body, token;
  unsigned priority = 0;
  bool pending() const { return !token.empty(); }
};
bool loadDraft(Draft& draft);
bool saveDraft(const Draft& draft);
bool validateDraft(const Draft& draft);
std::string newToken();
}
