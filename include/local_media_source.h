#ifndef LOCAL_MEDIA_SOURCE_H_GUARD
#define LOCAL_MEDIA_SOURCE_H_GUARD

#include <string>

enum class LocalVideoSourceKind {
  Camera,
  File,
  Screen,
  Window,
};

inline const char* LocalVideoSourceKindToString(LocalVideoSourceKind kind) {
  switch (kind) {
    case LocalVideoSourceKind::Camera:
      return "Camera";
    case LocalVideoSourceKind::File:
      return "File";
    case LocalVideoSourceKind::Screen:
      return "Screen";
    case LocalVideoSourceKind::Window:
      return "Window";
    default:
      return "Unknown";
  }
}

inline bool IsImplementedLocalVideoSourceKind(LocalVideoSourceKind kind) {
  return kind == LocalVideoSourceKind::Camera ||
         kind == LocalVideoSourceKind::File;
}

struct LocalVideoSourceConfig {
  LocalVideoSourceKind kind = LocalVideoSourceKind::Camera;
  std::string file_path;
};

struct LocalVideoSourceState {
  LocalVideoSourceKind kind = LocalVideoSourceKind::Camera;
  std::string file_path;
  std::string display_name = "Camera";
  bool active = false;
};

#endif  // LOCAL_MEDIA_SOURCE_H_GUARD
