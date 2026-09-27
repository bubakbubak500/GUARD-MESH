#pragma once

class HTTPClient {
public:
  bool ended = false;
  bool reuse = true;
  int connectTimeout = 0;
  int timeout = 0;
  void setReuse(bool value) { reuse = value; }
  void setConnectTimeout(int value) { connectTimeout = value; }
  void setTimeout(int value) { timeout = value; }
  void end() { ended = true; }
};
