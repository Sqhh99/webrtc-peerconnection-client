#include <windows.h>

#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "rtc_base/physical_socket_server.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/thread.h"
#include "rtc_base/win32_socket_init.h"

#include "app_config.h"
#include "call_coordinator.h"
#include "defaults.h"
#include "sdl_app.h"

namespace {

void PrintUsage(const char* program_name) {
  std::cerr << "Usage: " << program_name
            << " --signal-host <host> --signal-port <port> "
               "[--username <name>] [--media-source camera|file] "
               "[--media-file <path>]\n";
}

std::optional<AppConfig> ParseAppConfig(int argc, char* argv[]) {
  AppConfig config;

  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    auto require_value = [&](const char* flag_name) -> const char* {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << flag_name << "\n";
        return nullptr;
      }
      return argv[++i];
    };

    if (argument == "--signal-host") {
      const char* value = require_value("--signal-host");
      if (!value) {
        return std::nullopt;
      }
      config.signal_host = value;
    } else if (argument == "--signal-port") {
      const char* value = require_value("--signal-port");
      if (!value) {
        return std::nullopt;
      }
      try {
        config.signal_port = std::stoi(value);
      } catch (const std::exception&) {
        std::cerr << "Invalid value for --signal-port: " << value << "\n";
        return std::nullopt;
      }
    } else if (argument == "--username") {
      const char* value = require_value("--username");
      if (!value) {
        return std::nullopt;
      }
      config.username = value;
    } else if (argument == "--media-source") {
      const char* value = require_value("--media-source");
      if (!value) {
        return std::nullopt;
      }
      const std::string source = value;
      if (source == "camera") {
        config.local_video_source.kind = LocalVideoSourceKind::Camera;
      } else if (source == "file") {
        config.local_video_source.kind = LocalVideoSourceKind::File;
      } else {
        std::cerr << "Invalid value for --media-source: " << value << "\n";
        return std::nullopt;
      }
    } else if (argument == "--media-file") {
      const char* value = require_value("--media-file");
      if (!value) {
        return std::nullopt;
      }
      config.local_video_source.file_path = value;
    } else if (argument == "--help" || argument == "-h") {
      PrintUsage(argv[0]);
      return std::nullopt;
    } else {
      std::cerr << "Unknown argument: " << argument << "\n";
      return std::nullopt;
    }
  }

  if (config.signal_host.empty() || config.signal_port <= 0) {
    return std::nullopt;
  }
  if (!config.local_video_source.file_path.empty()) {
    config.local_video_source.kind = LocalVideoSourceKind::File;
  }
  if (config.local_video_source.kind == LocalVideoSourceKind::File &&
      config.local_video_source.file_path.empty()) {
    std::cerr << "--media-file is required when --media-source file is used\n";
    return std::nullopt;
  }
  if (config.username.empty()) {
    config.username = GenerateRandomUsername();
  }
  config.signal_url = "ws://" + config.signal_host + ":" +
                      std::to_string(config.signal_port) + "/ws/webrtc";
  return config;
}

}  // namespace

int main(int argc, char* argv[]) {
  const auto config = ParseAppConfig(argc, argv);
  if (!config) {
    PrintUsage(argv[0]);
    return 1;
  }

  webrtc::WinsockInitializer winsock_init;
  webrtc::PhysicalSocketServer socket_server;
  webrtc::AutoSocketServerThread main_thread(&socket_server);
  webrtc::Environment env = webrtc::CreateEnvironment();

  if (!webrtc::InitializeSSL()) {
    std::cerr << "Failed to initialize WebRTC SSL\n";
    return 1;
  }

  auto coordinator = std::make_unique<CallCoordinator>(env);
  if (!coordinator->Initialize()) {
    std::cerr << "Failed to initialize CallCoordinator\n";
    webrtc::CleanupSSL();
    return 1;
  }
  if (!coordinator->SetLocalVideoSource(config->local_video_source)) {
    std::cerr << "Failed to configure local video source\n";
    coordinator->Shutdown();
    webrtc::CleanupSSL();
    return 1;
  }

  SdlApp app(coordinator.get(), *config);
  coordinator->SetUIObserver(&app);
  if (!app.Initialize()) {
    coordinator->Shutdown();
    webrtc::CleanupSSL();
    return 1;
  }

  coordinator->ConnectToSignalServer(config->signal_url, config->username);
  const int result = app.Run();

  coordinator->Shutdown();
  coordinator.reset();
  webrtc::CleanupSSL();
  return result;
}
