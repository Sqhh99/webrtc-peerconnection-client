#ifndef APP_CONFIG_H_GUARD
#define APP_CONFIG_H_GUARD

#include <string>

#include "local_media_source.h"

struct AppConfig {
  std::string signal_host;
  int signal_port = 0;
  std::string username;
  std::string signal_url;
  LocalVideoSourceConfig local_video_source;
};

#endif  // APP_CONFIG_H_GUARD
