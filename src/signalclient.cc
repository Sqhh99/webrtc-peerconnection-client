#include "signalclient.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <utility>

#include <boost/asio/connect.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

SessionDescriptionPayload ParseSessionDescription(const json& payload,
                                                  const std::string& fallback_type) {
  SessionDescriptionPayload result;
  result.call_id = payload.value("callId", payload.value("call_id", ""));
  result.type = fallback_type;

  if (payload.contains("sdp")) {
    const json& sdp_value = payload.at("sdp");
    if (sdp_value.is_object()) {
      result.type = sdp_value.value("type", fallback_type);
      result.sdp = sdp_value.value("sdp", "");
      return result;
    }
    if (sdp_value.is_string()) {
      result.type = payload.value("type", fallback_type);
      result.sdp = sdp_value.get<std::string>();
      return result;
    }
  }

  result.type = payload.value("type", fallback_type);
  result.sdp = payload.value("sdp", "");
  return result;
}

IceCandidatePayload ParseIceCandidate(const json& payload) {
  IceCandidatePayload result;
  result.call_id = payload.value("callId", payload.value("call_id", ""));

  const json* candidate_payload = &payload;
  if (payload.contains("candidate") && payload.at("candidate").is_object()) {
    candidate_payload = &payload.at("candidate");
  }

  result.sdp_mid = candidate_payload->value("sdpMid", "");
  if (result.sdp_mid.empty()) {
    result.sdp_mid = candidate_payload->value("sdp_mid", "");
  }

  if (candidate_payload->contains("sdpMLineIndex")) {
    result.sdp_mline_index = candidate_payload->at("sdpMLineIndex").get<int>();
  } else if (candidate_payload->contains("sdpMlineIndex")) {
    result.sdp_mline_index = candidate_payload->at("sdpMlineIndex").get<int>();
  } else if (candidate_payload->contains("sdp_mline_index")) {
    result.sdp_mline_index = candidate_payload->at("sdp_mline_index").get<int>();
  }

  result.candidate = candidate_payload->value("candidate", "");
  return result;
}

std::vector<IceServerConfig> ParseIceServers(const json& payload) {
  std::vector<IceServerConfig> ice_servers;
  if (!payload.contains("iceServers") || !payload.at("iceServers").is_array()) {
    return ice_servers;
  }

  for (const auto& server_value : payload.at("iceServers")) {
    if (!server_value.is_object()) {
      continue;
    }

    IceServerConfig config;
    if (server_value.contains("urls")) {
      const auto& urls = server_value.at("urls");
      if (urls.is_array()) {
        for (const auto& url_value : urls) {
          if (url_value.is_string()) {
            config.urls.push_back(url_value.get<std::string>());
          }
        }
      } else if (urls.is_string()) {
        config.urls.push_back(urls.get<std::string>());
      }
    }

    config.username = server_value.value("username", "");
    config.credential = server_value.value("credential", "");
    ice_servers.push_back(std::move(config));
  }

  return ice_servers;
}

std::vector<ClientInfo> ParseClientList(const json& payload) {
  std::vector<ClientInfo> clients;
  if (!payload.contains("clients") || !payload.at("clients").is_array()) {
    return clients;
  }

  for (const auto& client_value : payload.at("clients")) {
    ClientInfo client;
    if (client_value.is_object()) {
      client.id = client_value.value("id", "");
    } else if (client_value.is_string()) {
      client.id = client_value.get<std::string>();
    }
    if (!client.id.empty()) {
      clients.push_back(std::move(client));
    }
  }
  return clients;
}

}  // namespace

struct SignalClient::ParsedUrl {
  std::string host;
  std::string port;
  std::string target;
};

class SignalClient::Impl {
 public:
  using WorkGuard = asio::executor_work_guard<asio::io_context::executor_type>;

  explicit Impl(SignalClient* owner) : owner(owner) {}

  SignalClient* owner;
  asio::io_context io_context;
  std::unique_ptr<WorkGuard> work_guard;
  std::unique_ptr<tcp::resolver> resolver;
  std::unique_ptr<websocket::stream<beast::tcp_stream>> websocket;
  std::unique_ptr<asio::steady_timer> reconnect_timer;
  beast::flat_buffer read_buffer;
  std::deque<std::string> write_queue;
  ParsedUrl parsed_url;
  std::thread io_thread;
};

SignalClient::SignalClient()
    : observer_(nullptr),
      is_connected_(false),
      manual_disconnect_(false),
      reconnect_attempts_(0) {}

SignalClient::~SignalClient() {
  Disconnect();
  StopIoThread();
}

void SignalClient::Connect(const std::string& server_url,
                           const std::string& client_id) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    server_url_ = server_url;
    client_id_ = client_id;
    manual_disconnect_ = false;
    reconnect_attempts_ = 0;
  }

  EnsureIoThreadStarted();
  asio::dispatch(impl_->io_context, [this]() { ConnectOnIoThread(); });
}

void SignalClient::Disconnect() {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    manual_disconnect_ = true;
    reconnect_attempts_ = 0;
  }

  if (!impl_) {
    ReportDisconnected(true);
    return;
  }

  asio::dispatch(impl_->io_context, [this]() {
    CloseTransport();
    ReportDisconnected(true);
  });
}

bool SignalClient::IsConnected() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return is_connected_;
}

std::string SignalClient::GetClientId() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return client_id_;
}

std::vector<IceServerConfig> SignalClient::GetIceServers() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return ice_servers_;
}

void SignalClient::RegisterObserver(SignalClientObserver* observer) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  observer_ = observer;
}

void SignalClient::SendCallRequest(const std::string& to,
                                   const std::string& call_id) {
  json message = {
      {"type", "call-request"},
      {"from", GetClientId()},
      {"to", to},
      {"payload", {{"callId", call_id},
                   {"timestamp",
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count()}}}};
  QueueJsonMessage(message.dump());
}

void SignalClient::SendCallResponse(const std::string& to,
                                    const std::string& call_id,
                                    bool accepted,
                                    const std::string& reason) {
  json payload = {{"callId", call_id}, {"accepted", accepted}};
  if (!reason.empty()) {
    payload["reason"] = reason;
  }

  json message = {{"type", "call-response"},
                  {"from", GetClientId()},
                  {"to", to},
                  {"payload", std::move(payload)}};
  QueueJsonMessage(message.dump());
}

void SignalClient::SendCallCancel(const std::string& to,
                                  const std::string& call_id,
                                  const std::string& reason) {
  json payload = {{"callId", call_id}};
  if (!reason.empty()) {
    payload["reason"] = reason;
  }

  json message = {{"type", "call-cancel"},
                  {"from", GetClientId()},
                  {"to", to},
                  {"payload", std::move(payload)}};
  QueueJsonMessage(message.dump());
}

void SignalClient::SendCallEnd(const std::string& to,
                               const std::string& call_id,
                               const std::string& reason) {
  json payload = {{"callId", call_id}};
  if (!reason.empty()) {
    payload["reason"] = reason;
  }

  json message = {{"type", "call-end"},
                  {"from", GetClientId()},
                  {"to", to},
                  {"payload", std::move(payload)}};
  QueueJsonMessage(message.dump());
}

void SignalClient::SendOffer(const std::string& to,
                             const SessionDescriptionPayload& sdp) {
  json message = {
      {"type", "offer"},
      {"from", GetClientId()},
      {"to", to},
      {"payload",
       {{"callId", sdp.call_id},
        {"sdp", {{"type", sdp.type}, {"sdp", sdp.sdp}}}}}};
  QueueJsonMessage(message.dump());
}

void SignalClient::SendAnswer(const std::string& to,
                              const SessionDescriptionPayload& sdp) {
  json message = {
      {"type", "answer"},
      {"from", GetClientId()},
      {"to", to},
      {"payload",
       {{"callId", sdp.call_id},
        {"sdp", {{"type", sdp.type}, {"sdp", sdp.sdp}}}}}};
  QueueJsonMessage(message.dump());
}

void SignalClient::SendIceCandidate(const std::string& to,
                                    const IceCandidatePayload& candidate) {
  json message = {{"type", "ice-candidate"},
                  {"from", GetClientId()},
                  {"to", to},
                  {"payload",
                   {{"callId", candidate.call_id},
                    {"candidate",
                     {{"sdpMid", candidate.sdp_mid},
                      {"sdpMLineIndex", candidate.sdp_mline_index},
                      {"candidate", candidate.candidate}}}}}};
  QueueJsonMessage(message.dump());
}

void SignalClient::RequestClientList() {
  json message = {{"type", "list-clients"}, {"from", GetClientId()}};
  QueueJsonMessage(message.dump());
}

void SignalClient::EnsureIoThreadStarted() {
  if (!impl_) {
    impl_ = std::make_unique<Impl>(this);
  }
  if (!impl_->work_guard) {
    impl_->io_context.restart();
    impl_->work_guard =
        std::make_unique<Impl::WorkGuard>(asio::make_work_guard(impl_->io_context));
  }
  if (!impl_->io_thread.joinable()) {
    impl_->io_thread = std::thread([this]() { impl_->io_context.run(); });
  }
}

void SignalClient::StopIoThread() {
  if (!impl_) {
    return;
  }

  if (impl_->work_guard) {
    impl_->work_guard.reset();
  }
  impl_->io_context.stop();
  if (impl_->io_thread.joinable()) {
    impl_->io_thread.join();
  }
  impl_.reset();
}

void SignalClient::ConnectOnIoThread() {
  std::string server_url;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    server_url = server_url_;
  }

  try {
    CloseTransport();
    RecreateTransport();
    impl_->parsed_url = ParseUrl(server_url);
  } catch (const std::exception& ex) {
    ReportConnectionError(ex.what());
    return;
  }

  impl_->resolver->async_resolve(
      impl_->parsed_url.host, impl_->parsed_url.port,
      [this](beast::error_code ec, tcp::resolver::results_type results) {
            if (ec) {
              ReportConnectionError("Resolve failed: " + ec.message());
              AttemptReconnect();
              return;
            }

            beast::get_lowest_layer(*impl_->websocket).expires_after(
                std::chrono::seconds(30));
            beast::get_lowest_layer(*impl_->websocket).async_connect(
                results,
                [this](beast::error_code connect_ec,
                       const tcp::resolver::results_type::endpoint_type&) {
                               if (connect_ec) {
                                 ReportConnectionError("Connect failed: " +
                                                       connect_ec.message());
                                 AttemptReconnect();
                                 return;
                               }

                               beast::get_lowest_layer(*impl_->websocket)
                                   .expires_never();
                               impl_->websocket->set_option(
                                   websocket::stream_base::timeout::suggested(
                                       beast::role_type::client));
                               impl_->websocket->set_option(
                                   websocket::stream_base::decorator(
                                       [](websocket::request_type& request) {
                                         request.set(
                                             beast::http::field::user_agent,
                                             std::string("peerconnection-client"));
                                       }));

                               const std::string client_id = GetClientId();
                               std::string target = impl_->parsed_url.target;
                               target +=
                                   (target.find('?') == std::string::npos ? "?uid="
                                                                          : "&uid=");
                               target += client_id;

                               impl_->websocket->async_handshake(
                                   impl_->parsed_url.host + ":" +
                                       impl_->parsed_url.port,
                                   target,
                                   [this](beast::error_code handshake_ec) {
                                         if (handshake_ec) {
                                           ReportConnectionError(
                                               "Handshake failed: " +
                                               handshake_ec.message());
                                           AttemptReconnect();
                                           return;
                                         }

                                         {
                                           std::lock_guard<std::mutex> lock(
                                               state_mutex_);
                                           is_connected_ = true;
                                           reconnect_attempts_ = 0;
                                         }

                                         SignalClientObserver* observer = nullptr;
                                         std::string client_id;
                                         {
                                           std::lock_guard<std::mutex> lock(
                                               state_mutex_);
                                           observer = observer_;
                                           client_id = client_id_;
                                         }
                                         if (observer) {
                                           observer->OnConnected(client_id);
                                         }

                                         json register_message = {
                                             {"type", "register"},
                                             {"from", client_id}};
                                         QueueJsonMessage(register_message.dump());
                                         StartRead();
                                       });
                             });
          });
}

void SignalClient::RecreateTransport() {
  impl_->resolver = std::make_unique<tcp::resolver>(impl_->io_context);
  impl_->websocket =
      std::make_unique<websocket::stream<beast::tcp_stream>>(impl_->io_context);
  impl_->reconnect_timer = std::make_unique<asio::steady_timer>(impl_->io_context);
  impl_->read_buffer.consume(impl_->read_buffer.size());
  impl_->write_queue.clear();
}

void SignalClient::StartRead() {
  if (!impl_ || !impl_->websocket) {
    return;
  }

  impl_->websocket->async_read(
      impl_->read_buffer,
      [this](beast::error_code ec, std::size_t) {
            if (ec) {
              ReportConnectionError("Read failed: " + ec.message());
              ReportDisconnected(true);
              AttemptReconnect();
              return;
            }

            const std::string message =
                beast::buffers_to_string(impl_->read_buffer.data());
            impl_->read_buffer.consume(impl_->read_buffer.size());
            HandleIncomingMessage(message);
            StartRead();
          });
}

void SignalClient::QueueJsonMessage(const std::string& message) {
  if (!impl_) {
    return;
  }

  asio::post(impl_->io_context, [this, message]() {
    if (!impl_ || !impl_->websocket) {
      return;
    }
    impl_->write_queue.push_back(message);
    if (impl_->write_queue.size() == 1) {
      WriteNextMessage();
    }
  });
}

void SignalClient::WriteNextMessage() {
  if (!impl_ || !impl_->websocket || impl_->write_queue.empty()) {
    return;
  }

  impl_->websocket->async_write(
      asio::buffer(impl_->write_queue.front()),
      [this](beast::error_code ec, std::size_t) {
            if (ec) {
              ReportConnectionError("Write failed: " + ec.message());
              ReportDisconnected(true);
              AttemptReconnect();
              return;
            }

            if (!impl_->write_queue.empty()) {
              impl_->write_queue.pop_front();
            }
            if (!impl_->write_queue.empty()) {
              WriteNextMessage();
            }
          });
}

void SignalClient::HandleIncomingMessage(const std::string& message) {
  json payload;
  try {
    payload = json::parse(message);
  } catch (const std::exception& ex) {
    ReportConnectionError(std::string("Invalid JSON from signaling server: ") +
                          ex.what());
    return;
  }

  if (!payload.is_object()) {
    return;
  }

  const std::string type = payload.value("type", "");
  const std::string from = payload.value("from", "");
  const json message_payload =
      payload.contains("payload") && payload.at("payload").is_object()
          ? payload.at("payload")
          : json::object();

  SignalClientObserver* observer = nullptr;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    observer = observer_;
  }
  if (!observer) {
    return;
  }

  switch (GetMessageType(type)) {
    case SignalMessageType::Registered: {
      const auto ice_servers = ParseIceServers(message_payload);
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        ice_servers_ = ice_servers;
      }
      observer->OnIceServersReceived(ice_servers);
      RequestClientList();
      break;
    }
    case SignalMessageType::ClientList:
      observer->OnClientListUpdate(ParseClientList(message_payload));
      break;
    case SignalMessageType::UserOffline:
      observer->OnUserOffline(message_payload.value("clientId", ""));
      break;
    case SignalMessageType::CallRequest:
      observer->OnCallRequest(
          from, message_payload.value("callId", message_payload.value("call_id", "")));
      break;
    case SignalMessageType::CallResponse:
      observer->OnCallResponse(
          from,
          message_payload.value("callId", message_payload.value("call_id", "")),
          message_payload.value("accepted", false),
          message_payload.value("reason", ""));
      break;
    case SignalMessageType::CallCancel:
      observer->OnCallCancel(
          from,
          message_payload.value("callId", message_payload.value("call_id", "")),
          message_payload.value("reason", ""));
      break;
    case SignalMessageType::CallEnd:
      observer->OnCallEnd(
          from,
          message_payload.value("callId", message_payload.value("call_id", "")),
          message_payload.value("reason", ""));
      break;
    case SignalMessageType::Offer:
      observer->OnOffer(from, ParseSessionDescription(message_payload, "offer"));
      break;
    case SignalMessageType::Answer:
      observer->OnAnswer(from,
                         ParseSessionDescription(message_payload, "answer"));
      break;
    case SignalMessageType::IceCandidate:
      observer->OnIceCandidate(from, ParseIceCandidate(message_payload));
      break;
    default:
      break;
  }
}

SignalMessageType SignalClient::GetMessageType(const std::string& type_str) const {
  if (type_str == "register") return SignalMessageType::Register;
  if (type_str == "registered") return SignalMessageType::Registered;
  if (type_str == "client-list") return SignalMessageType::ClientList;
  if (type_str == "user-offline") return SignalMessageType::UserOffline;
  if (type_str == "call-request") return SignalMessageType::CallRequest;
  if (type_str == "call-response") return SignalMessageType::CallResponse;
  if (type_str == "call-cancel") return SignalMessageType::CallCancel;
  if (type_str == "call-end") return SignalMessageType::CallEnd;
  if (type_str == "offer") return SignalMessageType::Offer;
  if (type_str == "answer") return SignalMessageType::Answer;
  if (type_str == "ice-candidate") return SignalMessageType::IceCandidate;
  return SignalMessageType::Unknown;
}

void SignalClient::AttemptReconnect() {
  bool manual_disconnect = false;
  int delay_ms = 0;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    manual_disconnect = manual_disconnect_;
    if (manual_disconnect_ || reconnect_attempts_ >= kMaxReconnectAttempts) {
      return;
    }
    ++reconnect_attempts_;
    delay_ms = std::min(1000 * (1 << (reconnect_attempts_ - 1)), 10000);
  }

  if (!impl_ || !impl_->reconnect_timer) {
    return;
  }
  if (manual_disconnect) {
    return;
  }

  impl_->reconnect_timer->expires_after(std::chrono::milliseconds(delay_ms));
  impl_->reconnect_timer->async_wait([this](beast::error_code ec) {
        if (ec) {
          return;
        }
        ConnectOnIoThread();
      });
}

void SignalClient::CloseTransport() {
  if (!impl_) {
    return;
  }

  if (impl_->reconnect_timer) {
    impl_->reconnect_timer->cancel();
  }
  if (impl_->resolver) {
    impl_->resolver->cancel();
  }
  if (impl_->websocket) {
    beast::error_code close_ec;
    impl_->websocket->next_layer().socket().cancel(close_ec);
    impl_->websocket->next_layer().socket().shutdown(
        tcp::socket::shutdown_both, close_ec);
    impl_->websocket->next_layer().socket().close(close_ec);
  }
  impl_->read_buffer.consume(impl_->read_buffer.size());
  impl_->write_queue.clear();
  impl_->websocket.reset();
  impl_->resolver.reset();
  impl_->reconnect_timer.reset();
}

void SignalClient::ReportDisconnected(bool notify_observer) {
  SignalClientObserver* observer = nullptr;
  bool was_connected = false;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    was_connected = is_connected_;
    is_connected_ = false;
    observer = observer_;
  }

  if (notify_observer && was_connected && observer) {
    observer->OnDisconnected();
  }
}

void SignalClient::ReportConnectionError(const std::string& error) {
  SignalClientObserver* observer = nullptr;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    observer = observer_;
  }
  if (observer) {
    observer->OnConnectionError(error);
  }
}

SignalClient::ParsedUrl SignalClient::ParseUrl(const std::string& url) const {
  std::string remaining = url;
  const std::string ws_prefix = "ws://";
  if (remaining.rfind(ws_prefix, 0) == 0) {
    remaining = remaining.substr(ws_prefix.size());
  } else {
    throw std::runtime_error("Only ws:// URLs are currently supported");
  }

  const size_t slash_pos = remaining.find('/');
  const std::string authority =
      slash_pos == std::string::npos ? remaining : remaining.substr(0, slash_pos);
  const std::string target =
      slash_pos == std::string::npos ? "/" : remaining.substr(slash_pos);

  const size_t colon_pos = authority.rfind(':');
  if (colon_pos == std::string::npos) {
    throw std::runtime_error("Signaling server URL must include host and port");
  }

  ParsedUrl parsed_url;
  parsed_url.host = authority.substr(0, colon_pos);
  parsed_url.port = authority.substr(colon_pos + 1);
  parsed_url.target = target;

  if (parsed_url.host.empty() || parsed_url.port.empty()) {
    throw std::runtime_error("Invalid signaling server URL");
  }
  return parsed_url;
}
