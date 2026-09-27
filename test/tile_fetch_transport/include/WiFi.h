#pragma once
#define WL_CONNECTED 3
class WiFiClient {
public:
  bool stopped = false;
  void stop();
};
class FakeWiFi {
public:
  int connection = WL_CONNECTED;
  int status() const { return connection; }
};
extern FakeWiFi WiFi;
