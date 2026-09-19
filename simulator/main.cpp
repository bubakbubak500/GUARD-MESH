// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <bcrypt.h>
#include "SimPlatform.h"
#include <vector>
#include <deque>
#include <filesystem>
extern "C" unsigned lodepng_encode32(unsigned char **, size_t *, const unsigned char *, unsigned,
                                     unsigned);

namespace {
constexpr int WIDTH = 320, HEIGHT = 240;
uint32_t framebuffer[WIDTH * HEIGHT]{};
HWND window = nullptr;
bool running = true, pressed = false, dirty = false;
uint16_t pointerX = 0, pointerY = 0;
std::deque<int> keys;
int ballX = 0, ballY = 0;
bool ballHeld = false;
bool injectRequested = false, snapshotRequested = false;
bool restartRequested = false;
void receiveDemo();
void captureSnapshot();
class SimSerialLink : public BaseSerialInterface {
public:
  void enable() override {}
  void disable() override {}
  bool isEnabled() const override { return false; }
  bool isConnected() const override { return false; }
  bool isWriteBusy() const override { return false; }
  size_t writeFrame(const uint8_t *, size_t) override { return 0; }
  size_t checkRecvFrame(uint8_t *) override { return 0; }
};
SimSerialLink serialLink;
SensorManager sensors;
UITask ui(&board, &serialLink);

RECT viewport(HWND hwnd) {
  RECT client;
  GetClientRect(hwnd, &client);
  int w = std::min((int)client.right, (int)client.bottom * WIDTH / HEIGHT), h = w * HEIGHT / WIDTH;
  int x = ((int)client.right - w) / 2, y = ((int)client.bottom - h) / 2;
  return RECT{x, y, x + w, y + h};
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_DESTROY:
    running = false;
    PostQuitMessage(0);
    return 0;
  case WM_LBUTTONDOWN:
    SetCapture(hwnd);
    pressed = true;
    break;
  case WM_LBUTTONUP:
    ReleaseCapture();
    pressed = false;
    break;
  case WM_KILLFOCUS:
    pressed = false;
    ballHeld = false;
    break;
  case WM_SIZE:
    dirty = true;
    break;
  case WM_CHAR:
    if (wp >= 32 || wp == 8 || wp == 13 || wp == 27)
      keys.push_back((int)wp);
    return 0;
  case WM_KEYDOWN:
    if (wp == VK_F1)
      MessageBoxW(hwnd,
                  L"Mouse: T-Deck touchscreen (drag to scroll)\nKeyboard: type into firmware "
                  L"fields; Enter sends; Escape goes back\nArrows: roll the trackball; F6: hold "
                  L"its centre button\nF5: receive a simulated message\nF12: save PNG and UI "
                  L"labels into .sim-cache/screenshots\n\nThis runs the shared firmware UI. Radio, "
                  L"GPS, storage hardware and network are simulated/offline.",
                  L"T-Deck simulator", MB_OK);
    if (wp == VK_F5)
      injectRequested = true;
    if (wp == VK_F12)
      snapshotRequested = true;
    if (wp == VK_LEFT)
      ballX -= 3;
    if (wp == VK_RIGHT)
      ballX += 3;
    if (wp == VK_UP)
      ballY -= 3;
    if (wp == VK_DOWN)
      ballY += 3;
    if (wp == VK_F6)
      ballHeld = true;
    return 0;
  case WM_KEYUP:
    if (wp == VK_F6)
      ballHeld = false;
    return 0;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
    RECT r = viewport(hwnd);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = WIDTH;
    info.bmiHeader.biHeight = -HEIGHT;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);
    if (display.isOn())
      StretchDIBits(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 0, 0, WIDTH, HEIGHT,
                    framebuffer, &info, DIB_RGB_COLORS, SRCCOPY);
    EndPaint(hwnd, &ps);
    return 0;
  }
  }
  if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) {
    RECT r = viewport(hwnd);
    pointerX = (uint16_t)std::clamp(
        (GET_X_LPARAM(lp) - r.left) * WIDTH / std::max(1L, r.right - r.left), 0L, 319L);
    pointerY = (uint16_t)std::clamp(
        (GET_Y_LPARAM(lp) - r.top) * HEIGHT / std::max(1L, r.bottom - r.top), 0L, 239L);
    ui.noteUserInput();
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
void pump(unsigned ms) {
  auto until = millis() + ms;
  do {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    ui.loop();
    SdNvsPrefs::tick(millis());
    if (the_mesh.pendingAck && millis() >= the_mesh.ackDue) {
      ui.onMessageAcked(the_mesh.pendingAck);
      the_mesh.pendingAck = 0;
    }
    if (injectRequested) {
      injectRequested = false;
      receiveDemo();
    }
    if (snapshotRequested) {
      snapshotRequested = false;
      try {
        captureSnapshot();
      } catch (const std::exception &e) {
        ui.showAlert(e.what(), 2500);
      }
    }
    static bool screenOn = true;
    if (display.isOn() != screenOn) {
      screenOn = display.isOn();
      dirty = true;
    }
    if (dirty) {
      InvalidateRect(window, nullptr, FALSE);
      dirty = false;
    }
    delay(5);
  } while (running && millis() < until);
}
void saveFrame(const char *path) {
  std::vector<uint8_t> rgba(WIDTH * HEIGHT * 4);
  size_t i = 0;
  for (uint32_t p : framebuffer) {
    rgba[i++] = (uint8_t)(p >> 16);
    rgba[i++] = (uint8_t)(p >> 8);
    rgba[i++] = (uint8_t)p;
    rgba[i++] = 255;
  }
  unsigned char *png = nullptr;
  size_t size = 0;
  if (lodepng_encode32(&png, &size, rgba.data(), WIDTH, HEIGHT) != 0)
    throw std::runtime_error("Cannot encode UI screenshot");
  FILE *file = fopen(path, "wb");
  if (!file) {
    free(png);
    throw std::runtime_error("Cannot save UI screenshot");
  }
  bool ok = fwrite(png, 1, size, file) == size;
  fclose(file);
  free(png);
  if (!ok)
    throw std::runtime_error("Incomplete UI screenshot");
}
void click(int x, int y) {
  RECT r = viewport(window);
  int px = r.left + (2 * x + 1) * (r.right - r.left) / (2 * WIDTH),
      py = r.top + (2 * y + 1) * (r.bottom - r.top) / (2 * HEIGHT);
  SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(px, py));
  pump(100);
  SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(px, py));
  pump(200);
}
void dumpLabels(lv_obj_t *obj, FILE *f) {
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN))
    return;
  if (lv_obj_check_type(obj, &lv_label_class) && lv_obj_is_visible(obj)) {
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    if (a.x2 >= 0 && a.y2 >= 0 && a.x1 < WIDTH && a.y1 < HEIGHT)
      fprintf(f, "[%d,%d,%d,%d] %s\n", a.x1, a.y1, a.x2, a.y2, lv_label_get_text(obj));
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i++)
    dumpLabels(lv_obj_get_child(obj, i), f);
}
void receiveDemo() {
  if (the_mesh.contacts.empty())
    return;
  const auto &c = the_mesh.contacts[0];
  ui.notify(UIEventType::contactMessage);
  ui.newMsgFromPubWithMeta(0, true, c.id.pub_key, c.name,
                           "Simulated incoming message. No radio transmission.",
                           ui.getMsgCount() + 1, 24, -72);
  ++the_mesh.received;
}
void captureSnapshot() {
  std::filesystem::create_directories("screenshots");
  std::string stem =
      "screenshots/tdeck-" + std::to_string(time(nullptr)) + "-" + std::to_string(millis());
  saveFrame((stem + ".png").c_str());
  FILE *f = fopen((stem + ".txt").c_str(), "w");
  if (f) {
    dumpLabels(lv_scr_act(), f);
    dumpLabels(lv_layer_top(), f);
    dumpLabels(lv_layer_sys(), f);
    fclose(f);
  }
  printf("Saved %s.png\n", stem.c_str());
}
lv_obj_t *findLabel(lv_obj_t *obj, const char *text) {
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN))
    return nullptr;
  if (lv_obj_check_type(obj, &lv_label_class) && lv_obj_is_visible(obj) &&
      strstr(lv_label_get_text(obj), text))
    return obj;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i++)
    if (auto found = findLabel(lv_obj_get_child(obj, i), text))
      return found;
  return nullptr;
}
void clickLabel(const char *text) {
  lv_obj_t *obj = findLabel(lv_layer_top(), text);
  if (!obj)
    obj = findLabel(lv_scr_act(), text);
  if (!obj)
    throw std::runtime_error(std::string("Visible UI label not found: ") + text);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  click((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
}
lv_obj_t *findTextarea(lv_obj_t *obj) {
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN))
    return nullptr;
  if (lv_obj_check_type(obj, &lv_textarea_class) && lv_obj_is_visible(obj))
    return obj;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i++)
    if (auto found = findTextarea(lv_obj_get_child(obj, i)))
      return found;
  return nullptr;
}
void seedScenario() {
  for (int i = 0; i < 32; i++)
    the_mesh.self_id.pub_key[i] = (uint8_t)(0x80 + i);
  for (int n = 0; n < 3; n++) {
    uint8_t pub[32];
    for (int i = 0; i < 32; i++)
      pub[i] = (uint8_t)(n * 40 + i + 1);
    the_mesh.uiAddManualContact(pub, n == 0 ? "SIM Alpha" : n == 1 ? "SIM Bravo" : "SIM Repeater");
  }
  the_mesh.contacts[2].type = ADV_TYPE_REPEATER;
  uint8_t secret[32]{};
  the_mesh.uiAddOrUpdateChannel(0, "#guardian-sim", secret);
}
} // namespace

int simDigitalRead(int pin) {
  return pin == 0 && ballHeld ? LOW : HIGH;
}
bool simReplaceFile(const char *source, const char *destination) {
  return MoveFileExA(source, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
void simRequestRestart() {
  restartRequested = true;
  running = false;
}

void simBlit(int x, int y, int w, int h, const uint16_t *p) {
  for (int yy = 0; yy < h; yy++)
    for (int xx = 0; xx < w; xx++) {
      uint16_t v = p[yy * w + xx];
      int dx = x + xx, dy = y + yy;
      if (dx >= 0 && dx < WIDTH && dy >= 0 && dy < HEIGHT)
        framebuffer[dy * WIDTH + dx] = (((v >> 11) * 255 / 31) << 16) |
                                       ((((v >> 5) & 63) * 255 / 63) << 8) | ((v & 31) * 255 / 31);
    }
  dirty = true;
}
extern "C" int64_t esp_timer_get_time() {
  return (int64_t)simMicros();
}
bool heltecV4CapTouchBegin() {
  return true;
}
int heltecV4CapTouchCheck() {
  return 0;
}
bool heltecV4CapTouchPopTap(uint16_t *, uint16_t *) {
  return false;
}
bool heltecV4CapTouchGetLive(uint16_t *x, uint16_t *y) {
  *x = pointerX;
  *y = pointerY;
  return pressed;
}
bool heltecV4CapTouchPopSwipe(int8_t *, int8_t *) {
  return false;
}
bool heltecV4CapTouchStartBackgroundPoll(uint32_t) {
  return true;
}
bool heltecV4CapTouchIsAsyncPolling() {
  return true;
}
bool heltecV4CapTouchIsSwiping() {
  return false;
}
void heltecV4CapTouchSetRotation(uint8_t) {}
void heltecV4CapTouchSetPointRotation(uint8_t) {}
void heltecV4CapTouchSetSlowPoll(bool) {}
const char *heltecV4CapTouchDebug() {
  return "Desktop pointer";
}
void heltecV4CapTouchGetRaw(uint16_t *x, uint16_t *y) {
  *x = pointerX;
  *y = pointerY;
}
void tdeckKeyboardBegin() {}
void tdeckKeyboardForceLegacy(bool) {}
void tdeckKeyboardPoll() {}
int tdeckKeyboardReadKey() {
  if (keys.empty())
    return 0;
  int k = keys.front();
  keys.pop_front();
  return k;
}
void tdeckKeyboardDiscardModifiers() {}
void tdeckKeyboardAllowModifiers() {}
void tdeckKeyboardSetBacklight(uint8_t) {}
void tdeckKeyboardFlushBacklight() {}
void tdeckTrackballBegin() {}
bool tdeckTrackballReadMotion(int *x, int *y) {
  *x = ballX;
  *y = ballY;
  ballX = ballY = 0;
  return *x || *y;
}
bool tdeckTrackballClickHeld() {
  return ballHeld;
}
void tdeckTrackballSetRotation(uint8_t) {}
namespace mesh {
Identity::Identity() {
  memset(pub_key, 0, sizeof pub_key);
}
} // namespace mesh
namespace mesh {
void Utils::toHex(char *dest, const uint8_t *src, size_t len) {
  const char *digits = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    dest[i * 2] = digits[src[i] >> 4];
    dest[i * 2 + 1] = digits[src[i] & 15];
  }
  dest[len * 2] = 0;
}
void Utils::sha256(uint8_t *out, size_t n, const uint8_t *data, int len) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  uint8_t digest[32]{};
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
    std::abort();
  if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0)
    std::abort();
  if (BCryptHashData(hash, const_cast<PUCHAR>(data), len, 0) < 0 ||
      BCryptFinishHash(hash, digest, 32, 0) < 0)
    std::abort();
  memcpy(out, digest, std::min(n, sizeof digest));
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(alg, 0);
}
} // namespace mesh
void reserveTileFetchStack() {}
bool meshcomodPrepareSdMigration() {
  return false;
}
bool meshcomodArmSdMigLatch() {
  return false;
}
bool meshcomodMigrateSpiffsToSd(bool) {
  return false;
}
void meshcomodClearSdMigLatch() {}
bool g_contacts_on_sd = false, g_sd_migration_blocked = false;
ColorVal UIColor::window_bkg = 0, UIColor::title_bkg = 0, UIColor::title_txt = 0xffff,
         UIColor::primary_txt = 0xffff, UIColor::secondary_txt = 0x7777,
         UIColor::warning_txt = 0xf800, UIColor::popup_bkg = 0, UIColor::popup_txt = 0xffff,
         UIColor::corp_blue = 0x001f;

int appMain(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  bool smoke = argc > 1 && strcmp(argv[1], "--smoke") == 0;
  WNDCLASSW cls{};
  cls.lpfnWndProc = windowProc;
  cls.hInstance = GetModuleHandle(nullptr);
  cls.lpszClassName = L"GuardMeshTDeckSimulator";
  cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassW(&cls);
  RECT r{0, 0, WIDTH * 3, HEIGHT * 3};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  window = CreateWindowW(cls.lpszClassName,
                         L"GUARD-MESH | T-Deck simulator | F1 Help | F5 Receive | F12 Screenshot",
                         WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                         r.bottom - r.top, nullptr, nullptr, cls.hInstance, nullptr);
  if (!window)
    return 1;
  SdNvsPrefs::load(smoke ? "" : "state/preferences.txt");
  touchPrefsBegin();
  touchPrefsSetSetupDone(true);
  touchPrefsSetScreenTimeoutSecs(0);
  auto &prefs = the_mesh.prefs;
  strcpy(prefs.node_name, "Guardian SIM");
  prefs.freq = 869.618f;
  prefs.bw = 62.5f;
  prefs.sf = 8;
  prefs.cr = 5;
  prefs.tx_power_dbm = 14;
  prefs.airtime_factor = 9;
  SdNvsPrefs saved;
  saved.begin("sim");
  if (saved.getBytesLength("node") == sizeof prefs)
    saved.getBytes("node", &prefs, sizeof prefs);
  prefs.node_name[sizeof prefs.node_name - 1] = 0;
  puts("Starting the shared T-Deck firmware UI (simulated peripherals)...");
  seedScenario();
  ui.begin(&display, &sensors, &prefs);
  receiveDemo();
  ui.notify(UIEventType::channelMessage);
  ui.newMsgFromPubWithMeta(0, true, nullptr, "#guardian-sim",
                           "SIM Bravo: Welcome to the T-Deck firmware simulator.",
                           ui.getMsgCount() + 1, 20, -80);
  ShowWindow(window, smoke ? SW_HIDE : SW_SHOW);
  pump(2600);
  if (smoke) {
    saveFrame("home.png");
    click(32, 225);
    saveFrame("chats.png");
    clickLabel("SIM Alpha");
    pump(400);
    saveFrame("conversation.png");
    lv_obj_t *ta = findTextarea(lv_layer_top());
    if (!ta)
      ta = findTextarea(lv_scr_act());
    if (!ta)
      throw std::runtime_error("Chat composer not visible");
    lv_area_t a;
    lv_obj_get_coords(ta, &a);
    click((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
    for (char c : std::string("Simulator keyboard test"))
      SendMessageW(window, WM_CHAR, c, 0);
    pump(300);
    SendMessageW(window, WM_CHAR, 13, 0);
    pump(1100);
    if (the_mesh.sent != 1)
      throw std::runtime_error("Typing and Enter did not send one simulated message");
    bool delivered = false;
    for (int i = 0; i < ui.msgCap(); i++) {
      UITask::UIMessage m{};
      if (ui.getMessageByIndex(i, m) && m.outgoing && !strcmp(m.text, "Simulator keyboard test") &&
          m.deliv_state == UITask::DELIV_DELIVERED)
        delivered = true;
    }
    if (!delivered)
      throw std::runtime_error("Simulated ACK did not mark the firmware message delivered");
    saveFrame("sent.png");
    click(16, 22);
    pump(300);
    click(96, 225);
    saveFrame("contacts.png");
    click(224, 225);
    pump(300);
    saveFrame("map.png");
    click(288, 225);
    saveFrame("settings.png");
    clickLabel("Profile");
    saveFrame("profile.png");
    auto profileTa = findTextarea(lv_layer_top());
    if (!profileTa)
      profileTa = findTextarea(lv_scr_act());
    if (!profileTa)
      throw std::runtime_error("Profile name field not visible");
    lv_obj_get_coords(profileTa, &a);
    click(a.x2 - 5, (a.y1 + a.y2) / 2);
    for (size_t i = 0; i < strlen(prefs.node_name); i++)
      keys.push_back(8);
    for (char c : std::string("SIM edited"))
      keys.push_back(c);
    pump(300);
    saveFrame("profile-edited.png");
    click(70, 145);
    pump(200);
    click(12, 22);
    pump(1200);
    if (strcmp(prefs.node_name, "SIM edited"))
      throw std::runtime_error(std::string("Profile edit did not update firmware preferences: ") +
                               prefs.node_name);
    SetWindowPos(window, nullptr, 0, 0, 853, 681, SWP_NOMOVE | SWP_NOZORDER);
    pump(100);
    click(160, 225);
    clickLabel("Apps");
    pump(300);
    saveFrame("apps.png");
    FILE *f = fopen("labels.txt", "w");
    if (f) {
      dumpLabels(lv_scr_act(), f);
      fclose(f);
    }
    the_mesh.savePrefs();
    touchPrefsSetAccentColor(0x336699);
    SdNvsPrefs::setFile("state/persistence-test.txt");
    if (!SdNvsPrefs::flush())
      throw std::runtime_error("Simulator settings were not saved");
    SdNvsPrefs::load("state/persistence-test.txt");
    touchPrefsReload();
    NodePrefs restored{};
    saved.getBytes("node", &restored, sizeof restored);
    if (strcmp(restored.node_name, "SIM edited") || touchPrefsGetAccentColor() != 0x336699)
      throw std::runtime_error("Simulator settings did not survive reload");
    puts("Smoke run complete: navigation, typing, send/ACK, profile, scaled input and persistence "
         "passed.");
    return 0;
  }
  while (running)
    pump(20);
  the_mesh.savePrefs();
  SdNvsPrefs::flush();
  if (restartRequested) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(exe) + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi))
      return 1;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  return 0;
}

int main(int argc, char **argv) {
  try {
    return appMain(argc, argv);
  } catch (const std::exception &e) {
    fprintf(stderr, "Simulator failed: %s\n", e.what());
    return 1;
  }
}
