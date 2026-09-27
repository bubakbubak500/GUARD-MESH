# Guardian štít

`guardian-shield.svg` je nezměněný symbol z `LOGO_SVG` v
`guardian/qt/startup_animation.py` našeho repozitáře Guardian
(`C:/Users/ok7ps/Documents/Guardian`). Původní komentář ho označuje jako symbol
z uživatelského `Guardian_sticker_white.svg`, bez nápisu a pozadí.

`python scripts/build/gen-guardian-logo.py` (PySide6) vytváří
`src/guardian_logo.c` a `.h`: RGB565 pro první hardwarový snímek a RGB565 s alfou
pro LVGL. Výstupy jsou součástí repozitáře, běžný firmware build PySide6 nepotřebuje.
