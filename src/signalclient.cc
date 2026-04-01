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

#include "signal_message_dispatcher.h"
#include "signaling_codec.h"

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

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
  QueueJsonMessage(BuildCallRequestMessage(GetClientId(), to, call_id));
}

void SignalClient::SendCallResponse(const std::string& to,
                                    const std::string& call_id,
                                    bool accepted,
                                    const std::string& reason) {
  QueueJsonMessage(
      BuildCallResponseMessage(GetClientId(), to, call_id, accepted, reason));
}

void SignalClient::SendCallCancel(const std::string& to,
                                  const std::string& call_id,
                                  const std::string& reason) {
  QueueJsonMessage(
      BuildCallCancelMessage(GetClientId(), to, call_id, reason));
}

void SignalClient::SendCallEnd(const std::string& to,
                               const std::string& call_id,
                               const std::string& reason) {
  QueueJsonMessage(BuildCallEndMessage(GetClientId(), to, call_id, reason));
}

void SignalClient::SendOffer(const std::string& to,
                             const SessionDescriptionPayload& sdp) {
  QueueJsonMessage(BuildOfferMessage(GetClientId(), to, sdp));
}

void SignalClient::SendAnswer(const std::string& to,
                              const SessionDescriptionPayload& sdp) {
  QueueJsonMessage(BuildAnswerMessage(GetClientId(), to, sdp));
}

void SignalClient::SendIceCandidate(const std::string& to,
                                    const IceCandidatePayload& candidate) {
  QueueJsonMessage(BuildIceCandidateMessage(GetClientId(), to, candidate));
}

void SignalClient::RequestClientList() {
  QueueJsonMessage(BuildListClientsMessage(GetClientId()));
}

bool SignalClient::InvokeOnIoThread(std::function<void()> task) {
  if (!task) {
    return false;
  }

  if (!impl_ || !impl_->io_thread.joinable() || impl_->io_context.stopped()) {
    return false;
  }

  if (impl_->io_thread.get_id() == std::this_thread::get_id()) {
    task();
    return true;
  }

  asio::post(impl_->io_context, [task = std::move(task)]() mutable { task(); });
  return true;
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

                                         QueueJsonMessage(
                                             BuildRegisterMessage(client_id));
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
  SignalClientObserver* observer = nullptr;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    observer = observer_;
  }
  if (!observer) {
    return;
  }

  const SignalMessageDispatchOutcome outcome =
      DispatchSignalingMessage(message, observer);
  if (!outcome.success) {
    ReportConnectionError(outcome.error);
    return;
  }

  if (outcome.has_ice_servers) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      ice_servers_ = outcome.ice_servers;
    }
  }
  if (outcome.request_client_list) {
    RequestClientList();
  }
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
