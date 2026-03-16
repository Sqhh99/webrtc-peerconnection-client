package main

import "net/http"

const rootPageHTML = `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>WebRTC Signaling Server</title>
  <style>
    body {
      margin: 0;
      font-family: "Segoe UI", sans-serif;
      background: #111315;
      color: #e8eaed;
    }
    main {
      max-width: 720px;
      margin: 64px auto;
      padding: 32px;
      background: #1a1d21;
      border: 1px solid #2a2f36;
      border-radius: 14px;
    }
    h1 {
      margin: 0 0 12px;
      font-size: 28px;
    }
    p {
      margin: 0 0 16px;
      color: #b6bcc6;
      line-height: 1.6;
    }
    code {
      padding: 2px 6px;
      border-radius: 6px;
      background: #0f1114;
      color: #7fd39b;
    }
    ul {
      margin: 20px 0 0;
      padding-left: 20px;
    }
    li {
      margin-bottom: 10px;
      color: #d7dce3;
    }
  </style>
</head>
<body>
  <main>
    <h1>WebRTC Signaling Server</h1>
    <p>This service is now dedicated to desktop clients and no longer hosts a browser-based calling UI.</p>
    <p>Please connect using the SDL desktop client.</p>
    <ul>
      <li>WebSocket: <code>/ws/webrtc?uid=&lt;client-id&gt;</code></li>
      <li>Health Check: <code>/health</code></li>
    </ul>
  </main>
</body>
</html>
`

func handleRoot(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}

	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	_, _ = w.Write([]byte(rootPageHTML))
}
