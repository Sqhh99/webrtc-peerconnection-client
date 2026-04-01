#ifndef CALL_SIGNALING_TRANSPORT_H_GUARD
#define CALL_SIGNALING_TRANSPORT_H_GUARD

#include <string>

class CallSignalingTransport {
 public:
  virtual ~CallSignalingTransport() = default;

  virtual bool IsConnected() const = 0;
  virtual void SendCallRequest(const std::string& to,
                               const std::string& call_id) = 0;
  virtual void SendCallResponse(const std::string& to,
                                const std::string& call_id,
                                bool accepted,
                                const std::string& reason = "") = 0;
  virtual void SendCallCancel(const std::string& to,
                              const std::string& call_id,
                              const std::string& reason = "") = 0;
  virtual void SendCallEnd(const std::string& to,
                           const std::string& call_id,
                           const std::string& reason = "") = 0;
};

#endif  // CALL_SIGNALING_TRANSPORT_H_GUARD
