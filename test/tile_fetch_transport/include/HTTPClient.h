#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#define HTTP_CODE_OK 200
class WiFiClient;
class FakeStream {
public:
  std::vector<uint8_t> body;
  size_t position = 0;
  size_t readLimit = size_t(-1);
  int readBytes(uint8_t *out, size_t count);
};
class HTTPClient {
public:
  bool begun = false;
  bool ended = false;
  int responseCode = 200;
  int declaredSize = -2; // -2 means body size
  int connectTimeout = 0;
  int timeout = 0;
  std::string url;
  std::string userAgent;
  FakeStream stream;
  bool begin(WiFiClient &, const char *);
  void setConnectTimeout(int value) { connectTimeout = value; }
  void setTimeout(int value) { timeout = value; }
  void addHeader(const char *, const char *);
  int GET();
  int getSize() const { return declaredSize == -2 ? int(stream.body.size()) : declaredSize; }
  FakeStream *getStreamPtr() { return &stream; }
  bool connected() const { return begun && !ended; }
  void end();
};
