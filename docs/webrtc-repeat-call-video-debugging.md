# Repeated Call One-Way Video Debugging

## Problem

The client intermittently failed after hanging up and calling the same peer again.

The main visible symptom was one-way video:

- one side showed local preview and could receive the remote stream
- the other side stayed on "Waiting for remote video..."
- stats often showed one side with outbound video bitrate only, and the other side with inbound video only

The issue reproduced more frequently on localhost and less frequently through a remote signaling server.

## User-visible symptoms

- second or later call with the same peer could become one-way
- stale video frames could remain visible after hangup
- logs initially did not go to the console, which made timing issues harder to inspect

## Investigation process

The debugging was done in stages instead of assuming a single root cause.

### 1. Remote video publication and rendering path

First, the receive and rendering path was instrumented and hardened:

- enabled WebRTC logs to stderr in `src/main.cc`
- added detailed remote media state logging in `src/webrtcengine.cc`
- used `OnTrack` as the primary remote media callback and kept `OnAddTrack` as fallback
- ensured remote video tracks were published again after `SetRemoteDescription(...)` and ICE connected
- fixed renderer cleanup so stale frames were cleared after hangup

This ruled out the first common suspicion: the app was not primarily failing because SDL did not render a valid remote track.

In failing sessions, logs still showed:

- remote receivers existed
- `OnTrack` fired
- remote video tracks were published

That meant the problem was earlier in signaling and transport setup.

### 2. Signaling session isolation

The next suspicion was stale signaling messages from the previous call.

To isolate call sessions:

- a generated `call_id` was introduced in `CallManager`
- `call_id` was added to call request/response/cancel/end messages
- `call_id` was added to offer/answer/ICE payloads
- stale offers, answers, and ICE candidates were ignored in `CallCoordinator`

This removed cross-call contamination from old signaling messages.

### 3. Early ICE timing race

After session isolation, logs showed a more specific transport race:

- early ICE candidates could arrive before the transport for that MID existed
- on localhost this happened more often because signaling and ICE arrived faster

The characteristic log line was:

- `Not adding candidate because the JsepTransport doesn't exist`

At that point the code only queued ICE before `remote_description()` was set.
If `AddIceCandidate(...)` failed after remote description existed, the candidate was dropped.

The fix was:

- queue ICE candidates when `AddIceCandidate(...)` fails
- retry queued ICE candidates later instead of deleting them immediately
- process pending ICE again after `SetLocalDescription(...)` succeeds
- delete queued candidates on close to avoid leaks

This removed the obvious early-ICE drop path.

### 4. Duplicate close path

The next logs showed `ClosePeerConnection()` being invoked twice during the same teardown flow.

The cause was:

- `CallManager::EndCall()` and `HandleCallEnd()` invoked both `OnNeedClosePeerConnection()` and `OnCallEnded()`
- `CallCoordinator::OnCallEnded()` also closed the peer connection

This caused duplicate close operations and made teardown timing noisier.

The fix was:

- keep teardown in `OnNeedClosePeerConnection()`
- stop closing the peer connection again inside `CallCoordinator::OnCallEnded()`

### 5. Unified Plan sender mis-detection

After the earlier fixes, the remaining symptom matched a negotiation-direction bug:

- one side sent video
- the other side only received
- the failing side still displayed "Connected"

The key insight was that `AddTracks()` used this check:

- `if (!peer_connection_->GetSenders().empty())`

Under Unified Plan this is not reliable.
Remote SDP can create transceivers and sender objects before local tracks are actually attached.
That means:

- `GetSenders()` can be non-empty
- but `sender->track()` can still be null

So the code could incorrectly conclude "tracks already added" and create an answer without actually attaching local audio/video tracks.

This matched the observed one-way sessions.

The fix was:

- detect local media attachment by checking whether a sender actually has a track of the expected kind
- add missing local video/audio tracks individually
- before creating an answer, call `AddTracks()` again to ensure local tracks are really attached

## Final fixes applied

### Signaling and session safety

- added `call_id` to call control and WebRTC signaling messages
- ignored stale offers, answers, and ICE from previous calls

### Media track publication

- switched to `OnTrack`-first handling
- added remote track diagnostics
- republished remote video track after key signaling and ICE milestones

### ICE handling

- retried ICE candidates when transport was not ready
- retried pending ICE after local description was set
- preserved still-failing candidates for later retry

### Teardown and UI cleanup

- cleared stale video renderer frames on stop
- removed duplicate peer connection close during hangup/end-call flow

### Answer creation safety

- changed local track detection to use actual sender tracks instead of sender count
- ensured local tracks are attached before creating an answer

## Why this was hard to diagnose

The bug looked like a rendering issue at first, but it was actually a combination of state and timing problems:

- stale cross-call signaling was possible
- ICE could fail only in fast paths
- sender existence did not mean local track attachment
- the UI still showed the connection as established in one-way sessions

Because the failure was timing-sensitive, localhost reproduced it more often than a remote deployment.

## Files changed during the fix

- `src/webrtcengine.cc`
- `include/webrtcengine.h`
- `src/call_coordinator.cc`
- `include/call_coordinator.h`
- `src/callmanager.cc`
- `include/callmanager.h`
- `src/signalclient.cc`
- `include/signalclient.h`
- `include/signal_types.h`
- `src/videorenderer.cc`
- `include/videorenderer.h`
- `src/sdl_app.cc`
- `include/sdl_app.h`
- `src/main.cc`

## Validation status

The final user verification after the last fix indicated that the repeated-call one-way video issue no longer reproduced.

No clean local compile verification was completed from WSL because the existing `build/` directory was generated with a Windows Visual Studio CMake cache, which failed generator validation under the current shell environment.
