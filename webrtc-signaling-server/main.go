package main

import (
	"encoding/json"
	"flag"
	"log"
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

// WebRTCSignalingServer WebRTC信令服务器
type WebRTCSignalingServer struct {
	clients map[string]*WebRTCClient // map[uid]*WebRTCClient
	mutex   sync.RWMutex
}

// WebRTCClient WebRTC客户端连接
type WebRTCClient struct {
	uid    string
	conn   *websocket.Conn
	server *WebRTCSignalingServer
	send   chan []byte
}

// WebRTCMessage WebRTC信令消息结构
type WebRTCMessage struct {
	Type    string          `json:"type"`
	From    string          `json:"from,omitempty"`
	To      string          `json:"to,omitempty"`
	Payload json.RawMessage `json:"payload,omitempty"`
}

// IceServer ICE服务器配置
type IceServer struct {
	URLs       []string `json:"urls"`
	Username   string   `json:"username,omitempty"`
	Credential string   `json:"credential,omitempty"`
}

var (
	addr     = flag.String("addr", ":8081", "http service address")
	upgrader = websocket.Upgrader{
		CheckOrigin: func(r *http.Request) bool {
			return true // 允许所有来源
		},
	}
	server *WebRTCSignalingServer
)

func main() {
	flag.Parse()
	log.SetFlags(log.LstdFlags | log.Lshortfile)

	server = NewWebRTCSignalingServer()

	http.HandleFunc("/ws/webrtc", handleWebRTCConnection)
	http.HandleFunc("/health", handleHealth)

	log.Printf("WebRTC信令服务器启动在 %s", *addr)
	log.Printf("WebSocket地址: ws://localhost%s/ws/webrtc?uid=YOUR_CLIENT_ID", *addr)

	if err := http.ListenAndServe(*addr, nil); err != nil {
		log.Fatal("启动服务器失败:", err)
	}
}

// handleHealth 健康检查接口
func handleHealth(w http.ResponseWriter, r *http.Request) {
	w.WriteHeader(http.StatusOK)
	w.Write([]byte("OK"))
}

// handleWebRTCConnection 处理WebRTC WebSocket连接
func handleWebRTCConnection(w http.ResponseWriter, r *http.Request) {
	// 从URL参数获取客户端ID
	uid := r.URL.Query().Get("uid")
	if uid == "" {
		log.Printf("缺少uid参数")
		http.Error(w, "Missing uid parameter", http.StatusBadRequest)
		return
	}

	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("WebSocket升级失败: %v", err)
		return
	}

	log.Printf("新的WebRTC连接: uid=%s, remote=%s", uid, r.RemoteAddr)
	server.HandleConnection(conn, uid)
}

// NewWebRTCSignalingServer 创建新的WebRTC信令服务器
func NewWebRTCSignalingServer() *WebRTCSignalingServer {
	return &WebRTCSignalingServer{
		clients: make(map[string]*WebRTCClient),
	}
}

// HandleConnection 处理新的WebRTC信令连接
func (s *WebRTCSignalingServer) HandleConnection(conn *websocket.Conn, uid string) {
	client := &WebRTCClient{
		uid:    uid,
		conn:   conn,
		server: s,
		send:   make(chan []byte, 256),
	}

	// 处理重复登录
	s.mutex.Lock()
	if existingClient, ok := s.clients[uid]; ok {
		// 关闭旧连接
		log.Printf("检测到重复登录，关闭旧连接: %s", uid)
		delete(s.clients, uid)
		s.mutex.Unlock()

		// 在锁外关闭旧连接，避免死锁
		go func() {
			existingClient.conn.Close()
			defer func() {
				if r := recover(); r != nil {
					log.Printf("关闭旧连接 send channel 时发生 panic: %v", r)
				}
			}()
			close(existingClient.send)
		}()

		s.mutex.Lock()
	}
	s.clients[uid] = client
	s.mutex.Unlock()

	log.Printf("WebRTC客户端连接成功: %s (当前在线: %d)", uid, s.getClientCount())

	// 启动读写协程
	go client.writePump()
	go client.readPump()

	// 延迟发送注册消息，确保 writePump 已经启动
	go func() {
		time.Sleep(50 * time.Millisecond)
		s.sendRegisteredMessage(client)
	}()
}

// getClientCount 获取当前在线客户端数量
func (s *WebRTCSignalingServer) getClientCount() int {
	s.mutex.RLock()
	defer s.mutex.RUnlock()
	return len(s.clients)
}

// sendRegisteredMessage 发送注册确认消息
func (s *WebRTCSignalingServer) sendRegisteredMessage(client *WebRTCClient) {
	log.Printf("发送注册消息给客户端: %s", client.uid)

	iceServers := s.getIceServers()
	payload := map[string]interface{}{
		"iceServers": iceServers,
	}

	payloadData, _ := json.Marshal(payload)
	response := WebRTCMessage{
		Type:    "registered",
		From:    client.uid,
		Payload: payloadData,
	}

	data, err := json.Marshal(response)
	if err != nil {
		log.Printf("序列化注册消息失败: %v", err)
		return
	}

	select {
	case client.send <- data:
		log.Printf("✓ 注册消息发送成功: %s", client.uid)
	default:
		log.Printf("✗ 客户端发送缓冲区已满: %s", client.uid)
		return
	}

	// 立即发送客户端列表给新连接的客户端
	s.sendClientList(client)

	// 广播客户端列表给所有其他客户端
	s.broadcastClientList()
}

// getIceServers 获取ICE服务器配置
func (s *WebRTCSignalingServer) getIceServers() []IceServer {
	return []IceServer{
		// Google 公共 STUN 服务器
		{
			URLs: []string{"stun:stun.l.google.com:19302"},
		},
		// Google 备用 STUN 服务器
		{
			URLs: []string{"stun:stun1.l.google.com:19302"},
		},
		// 自定义 TURN 服务器 (UDP)
		{
			URLs:       []string{"turn:113.46.159.182:3478"},
			Username:   "myuser",
			Credential: "mypassword",
		},
		// 自定义 TURN 服务器 (TCP)
		{
			URLs:       []string{"turn:113.46.159.182:3478?transport=tcp"},
			Username:   "myuser",
			Credential: "mypassword",
		},
		// 公共 TURN 服务器作为备份
		{
			URLs:       []string{"turn:openrelay.metered.ca:80"},
			Username:   "openrelayproject",
			Credential: "openrelayproject",
		},
		{
			URLs:       []string{"turn:openrelay.metered.ca:443"},
			Username:   "openrelayproject",
			Credential: "openrelayproject",
		},
		{
			URLs:       []string{"turn:openrelay.metered.ca:443?transport=tcp"},
			Username:   "openrelayproject",
			Credential: "openrelayproject",
		},
	}
}

// removeClient 移除客户端
func (s *WebRTCSignalingServer) removeClient(client *WebRTCClient) {
	s.mutex.Lock()
	// 只有当 map 中的客户端就是当前这个客户端时才删除
	if existingClient, ok := s.clients[client.uid]; ok && existingClient == client {
		delete(s.clients, client.uid)
		log.Printf("WebRTC客户端断开: %s (剩余在线: %d)", client.uid, len(s.clients))

		// 通知其他客户端该用户已下线
		go s.notifyUserOffline(client.uid)

		// 广播客户端列表
		go s.broadcastClientList()
	} else {
		log.Printf("WebRTC客户端断开（但已有新连接）: %s", client.uid)
	}
	s.mutex.Unlock()

	// 安全地关闭 send channel
	defer func() {
		if r := recover(); r != nil {
			log.Printf("关闭 send channel 时发生 panic: %v", r)
		}
	}()
	close(client.send)
}

// notifyUserOffline 通知其他客户端用户已下线
func (s *WebRTCSignalingServer) notifyUserOffline(uid string) {
	payload := map[string]interface{}{
		"clientId": uid,
	}
	payloadData, _ := json.Marshal(payload)

	offlineMsg := WebRTCMessage{
		Type:    "user-offline",
		From:    "server",
		Payload: payloadData,
	}

	data, err := json.Marshal(offlineMsg)
	if err != nil {
		log.Printf("序列化下线消息失败: %v", err)
		return
	}

	s.mutex.RLock()
	defer s.mutex.RUnlock()

	for clientUID, client := range s.clients {
		if clientUID != uid {
			select {
			case client.send <- data:
			default:
				log.Printf("发送下线通知失败: %s", clientUID)
			}
		}
	}
}

// relayMessage 转发消息
func (s *WebRTCSignalingServer) relayMessage(senderUID string, msg *WebRTCMessage) {
	s.mutex.RLock()
	targetClient, ok := s.clients[msg.To]
	s.mutex.RUnlock()

	if !ok {
		log.Printf("✗ 目标客户端不在线: %s", msg.To)
		return
	}

	data, err := json.Marshal(msg)
	if err != nil {
		log.Printf("序列化转发消息失败: %v", err)
		return
	}

	select {
	case targetClient.send <- data:
		log.Printf("✓ 消息转发: %s -> %s (type: %s)", senderUID, msg.To, msg.Type)
	default:
		log.Printf("✗ 目标客户端发送缓冲区已满: %s", msg.To)
	}
}

// broadcastClientList 广播客户端列表
func (s *WebRTCSignalingServer) broadcastClientList() {
	s.mutex.RLock()
	var clients []map[string]string
	for uid := range s.clients {
		clients = append(clients, map[string]string{"id": uid})
	}
	s.mutex.RUnlock()

	log.Printf("广播客户端列表 (在线: %d)", len(clients))

	payload := map[string]interface{}{
		"clients": clients,
	}
	payloadData, _ := json.Marshal(payload)

	message := WebRTCMessage{
		Type:    "client-list",
		Payload: payloadData,
	}

	data, err := json.Marshal(message)
	if err != nil {
		log.Printf("序列化客户端列表失败: %v", err)
		return
	}

	s.mutex.RLock()
	defer s.mutex.RUnlock()

	for _, client := range s.clients {
		select {
		case client.send <- data:
		default:
			log.Printf("广播客户端列表失败: %s", client.uid)
		}
	}
}

// sendClientList 发送客户端列表给指定客户端
func (s *WebRTCSignalingServer) sendClientList(client *WebRTCClient) {
	s.mutex.RLock()
	var clients []map[string]string
	for uid := range s.clients {
		clients = append(clients, map[string]string{"id": uid})
	}
	s.mutex.RUnlock()

	log.Printf("发送客户端列表给 %s (在线: %d)", client.uid, len(clients))

	payload := map[string]interface{}{
		"clients": clients,
	}
	payloadData, _ := json.Marshal(payload)

	message := WebRTCMessage{
		Type:    "client-list",
		Payload: payloadData,
	}

	data, err := json.Marshal(message)
	if err != nil {
		log.Printf("序列化客户端列表失败: %v", err)
		return
	}

	select {
	case client.send <- data:
		log.Printf("✓ 客户端列表发送成功: %s", client.uid)
	default:
		log.Printf("✗ 发送客户端列表失败: %s", client.uid)
	}
}

// ============================================================================
// WebRTCClient 方法
// ============================================================================

// readPump 读取客户端消息
func (c *WebRTCClient) readPump() {
	defer func() {
		c.server.removeClient(c)
		c.conn.Close()
	}()

	c.conn.SetReadDeadline(time.Now().Add(60 * time.Second))
	c.conn.SetPongHandler(func(string) error {
		c.conn.SetReadDeadline(time.Now().Add(60 * time.Second))
		return nil
	})

	for {
		_, message, err := c.conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				log.Printf("WebRTC客户端连接错误: %v", err)
			}
			break
		}

		var msg WebRTCMessage
		if err := json.Unmarshal(message, &msg); err != nil {
			log.Printf("解析WebRTC消息失败: %v", err)
			continue
		}

		// 设置发送者
		msg.From = c.uid

		log.Printf("收到消息: type=%s, from=%s, to=%s", msg.Type, msg.From, msg.To)

		switch msg.Type {
		case "register":
			// 注册消息已经在连接时处理，这里忽略
			log.Printf("忽略注册消息，客户端已注册: %s", c.uid)
		case "list-clients":
			c.server.sendClientList(c)
		case "offer", "answer", "ice-candidate", "conflict-resolution",
			"call-request", "call-response", "call-cancel", "call-end":
			// 转发信令消息
			if msg.To == "" {
				log.Printf("消息缺少目标用户: type=%s", msg.Type)
				continue
			}
			c.server.relayMessage(c.uid, &msg)
		default:
			log.Printf("未知的消息类型: %s", msg.Type)
		}
	}
}

// writePump 向客户端写入消息
func (c *WebRTCClient) writePump() {
	ticker := time.NewTicker(54 * time.Second)
	defer func() {
		ticker.Stop()
		c.conn.Close()
	}()

	for {
		select {
		case message, ok := <-c.send:
			c.conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
			if !ok {
				c.conn.WriteMessage(websocket.CloseMessage, []byte{})
				return
			}

			if err := c.conn.WriteMessage(websocket.TextMessage, message); err != nil {
				log.Printf("写入消息失败: %v", err)
				return
			}

		case <-ticker.C:
			c.conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
			if err := c.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				return
			}
		}
	}
}
