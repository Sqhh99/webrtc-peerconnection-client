#ifndef APP_CONFIG_H_GUARD
#define APP_CONFIG_H_GUARD

#include <string>

struct AppConfig {
  std::string signal_host;
  int signal_port = 0;
  std::string username;
  std::string signal_url;
};

#endif  // APP_CONFIG_H_GUARD
