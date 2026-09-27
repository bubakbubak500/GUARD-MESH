#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fs { class FS; }
class File {
public:
  File() = default;
  File(fs::FS *owner, const char *path, bool writing);
  explicit operator bool() const { return _open; }
  int read(uint8_t *buffer, size_t count);
  size_t write(const uint8_t *buffer, size_t count);
  void close();
private:
  fs::FS *_owner = nullptr;
  std::string _path;
  bool _writing = false;
  bool _open = false;
  size_t _position = 0;
};

namespace fs {
class FS {
public:
  std::map<std::string, std::vector<uint8_t> > files;
  std::vector<std::string> removed;
  std::vector<std::string> directories;
  bool failReadOpen = false;
  bool failWriteOpen = false;
  size_t readLimit = size_t(-1);
  size_t writeLimit = size_t(-1);
  int openCount = 0;
  File open(const char *path, const char *mode);
  bool remove(const char *path);
  bool mkdir(const char *path);
};
}
