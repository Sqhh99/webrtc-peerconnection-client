package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"strings"
	"time"

	"github.com/gorilla/mux"
	"github.com/joho/godotenv"
	"github.com/livekit/protocol/auth"
	"github.com/livekit/protocol/livekit"
	lksdk "github.com/livekit/server-sdk-go/v2"
	"github.com/rs/cors"
)

var (
	// LiveKit 配置
	livekitURL   string // HTTP/HTTPS URL for API calls
	livekitWsURL string // WebSocket URL for client connections
	apiKey       string
	apiSecret    string
	serverPort   string
	serverHost   string
	roomClient   *lksdk.RoomServiceClient
)

// Room 房间信息
type Room struct {
	Name         string    `json:"name"`
	DisplayName  string    `json:"displayName"`
	Participants int       `json:"participants"`
	CreatedAt    time.Time `json:"createdAt"`
}

// TokenRequest 请求Token的结构
type TokenRequest struct {
	RoomName        string `json:"roomName"`
	ParticipantName string `json:"participantName"`
}

// TokenResponse Token响应结构
type TokenResponse struct {
	Token    string `json:"token"`
	URL      string `json:"url"`
	RoomName string `json:"roomName"`
}

// ErrorResponse 错误响应
type ErrorResponse struct {
	Error string `json:"error"`
}

func init() {
	// 加载环境变量
	if err := godotenv.Load(); err != nil {
		log.Println("未找到 .env 文件，使用默认配置")
	}

	livekitURL = getEnv("LIVEKIT_URL", "http://localhost:7880")
	livekitWsURL = getEnv("LIVEKIT_WS_URL", "ws://localhost:7880")
	apiKey = getEnv("LIVEKIT_API_KEY", "devkey")
	apiSecret = getEnv("LIVEKIT_API_SECRET", "secret")
	serverPort = getEnv("SERVER_PORT", "8081")
	serverHost = getEnv("SERVER_HOST", "localhost")
}

func main() {
	log.SetFlags(log.LstdFlags | log.Lshortfile)

	// 初始化 LiveKit Room Client
	roomClient = lksdk.NewRoomServiceClient(livekitURL, apiKey, apiSecret)

	// 创建路由
	router := mux.NewRouter()

	// API 端点 - 必须先注册API路由
	api := router.PathPrefix("/api").Subrouter()
	api.HandleFunc("/token", handleGetToken).Methods("POST")
	api.HandleFunc("/rooms", handleListRooms).Methods("GET")
	api.HandleFunc("/rooms", handleCreateRoom).Methods("POST")
	api.HandleFunc("/rooms/{roomName}", handleDeleteRoom).Methods("DELETE")
	api.HandleFunc("/rooms/{roomName}/participants", handleListParticipants).Methods("GET")
	api.HandleFunc("/health", handleHealth).Methods("GET")

	// 静态文件服务
	router.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		http.ServeFile(w, r, "./static/index.html")
	}).Methods("GET")

	router.PathPrefix("/").Handler(http.FileServer(http.Dir("./static")))

	// CORS 配置
	c := cors.New(cors.Options{
		AllowedOrigins:   []string{"*"},
		AllowedMethods:   []string{"GET", "POST", "PUT", "DELETE", "OPTIONS"},
		AllowedHeaders:   []string{"*"},
		AllowCredentials: true,
	})

	handler := c.Handler(router)

	addr := fmt.Sprintf(":%s", serverPort)
	log.Println()
	log.Println("====================================")
	log.Printf("🚀 LiveKit 视频会议服务器启动成功")
	log.Println("====================================")
	log.Printf("📍 服务地址: http://%s%s", serverHost, addr)
	log.Printf("🎥 LiveKit API: %s", livekitURL)
	log.Printf("🔌 LiveKit WebSocket: %s", livekitWsURL)
	log.Printf("🔑 API Key: %s", apiKey)
	log.Printf("📱 访问 Web 应用: http://%s%s", serverHost, addr)
	log.Println("====================================")
	log.Println()

	if err := http.ListenAndServe(addr, handler); err != nil {
		log.Fatalf("❌ 服务器启动失败: %v", err)
	}
}

// ============================================================================
// 辅助函数
// ============================================================================

func getEnv(key, defaultValue string) string {
	value := os.Getenv(key)
	if value == "" {
		return defaultValue
	}
	return value
}

func respondJSON(w http.ResponseWriter, status int, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(data)
}

func respondError(w http.ResponseWriter, status int, message string) {
	respondJSON(w, status, ErrorResponse{Error: message})
}

// ============================================================================
// HTTP 处理器
// ============================================================================

func serveHome(w http.ResponseWriter, r *http.Request) {
	http.ServeFile(w, r, "./static/index.html")
}

func handleHealth(w http.ResponseWriter, r *http.Request) {
	respondJSON(w, http.StatusOK, map[string]string{
		"status": "ok",
		"time":   time.Now().Format(time.RFC3339),
	})
}

// handleGetToken 生成 LiveKit 访问令牌
func handleGetToken(w http.ResponseWriter, r *http.Request) {
	var req TokenRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		respondError(w, http.StatusBadRequest, "无效的请求格式")
		return
	}

	if req.RoomName == "" {
		req.RoomName = "default-room"
	}
	if req.ParticipantName == "" {
		req.ParticipantName = fmt.Sprintf("user-%d", time.Now().Unix())
	}

	// 清理房间名和参与者名
	req.RoomName = strings.TrimSpace(req.RoomName)
	req.ParticipantName = strings.TrimSpace(req.ParticipantName)

	// 创建 Access Token
	at := auth.NewAccessToken(apiKey, apiSecret)
	canPublish := true
	canSubscribe := true
	grant := &auth.VideoGrant{
		RoomJoin:     true,
		Room:         req.RoomName,
		CanPublish:   &canPublish,
		CanSubscribe: &canSubscribe,
	}
	at.AddGrant(grant).
		SetIdentity(req.ParticipantName).
		SetValidFor(24 * time.Hour)

	token, err := at.ToJWT()
	if err != nil {
		log.Printf("生成 token 失败: %v", err)
		respondError(w, http.StatusInternalServerError, "生成令牌失败")
		return
	}

	log.Printf("✅ 为用户 '%s' 生成房间 '%s' 的访问令牌", req.ParticipantName, req.RoomName)

	respondJSON(w, http.StatusOK, TokenResponse{
		Token:    token,
		URL:      livekitWsURL,
		RoomName: req.RoomName,
	})
}

// handleListRooms 列出所有活跃的房间
func handleListRooms(w http.ResponseWriter, r *http.Request) {
	rooms, err := roomClient.ListRooms(r.Context(), &livekit.ListRoomsRequest{})
	if err != nil {
		log.Printf("获取房间列表失败: %v", err)
		respondError(w, http.StatusInternalServerError, "获取房间列表失败")
		return
	}

	var roomList []Room
	for _, room := range rooms.Rooms {
		roomList = append(roomList, Room{
			Name:         room.Name,
			DisplayName:  room.Name,
			Participants: int(room.NumParticipants),
			CreatedAt:    time.Unix(room.CreationTime, 0),
		})
	}

	respondJSON(w, http.StatusOK, roomList)
}

// handleCreateRoom 创建新房间
func handleCreateRoom(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Name string `json:"name"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		respondError(w, http.StatusBadRequest, "无效的请求格式")
		return
	}

	if req.Name == "" {
		req.Name = fmt.Sprintf("room-%d", time.Now().Unix())
	}

	room, err := roomClient.CreateRoom(r.Context(), &livekit.CreateRoomRequest{
		Name:            req.Name,
		EmptyTimeout:    300, // 5分钟无人自动关闭
		MaxParticipants: 50,
	})
	if err != nil {
		log.Printf("创建房间失败: %v", err)
		respondError(w, http.StatusInternalServerError, "创建房间失败")
		return
	}

	log.Printf("✅ 创建房间: %s", room.Name)

	respondJSON(w, http.StatusCreated, Room{
		Name:         room.Name,
		DisplayName:  room.Name,
		Participants: int(room.NumParticipants),
		CreatedAt:    time.Unix(room.CreationTime, 0),
	})
}

// handleDeleteRoom 删除房间
func handleDeleteRoom(w http.ResponseWriter, r *http.Request) {
	vars := mux.Vars(r)
	roomName := vars["roomName"]

	_, err := roomClient.DeleteRoom(r.Context(), &livekit.DeleteRoomRequest{
		Room: roomName,
	})
	if err != nil {
		log.Printf("删除房间失败: %v", err)
		respondError(w, http.StatusInternalServerError, "删除房间失败")
		return
	}

	log.Printf("✅ 删除房间: %s", roomName)

	respondJSON(w, http.StatusOK, map[string]string{
		"message": "房间已删除",
	})
}

// handleListParticipants 列出房间中的参与者
func handleListParticipants(w http.ResponseWriter, r *http.Request) {
	vars := mux.Vars(r)
	roomName := vars["roomName"]

	participants, err := roomClient.ListParticipants(r.Context(), &livekit.ListParticipantsRequest{
		Room: roomName,
	})
	if err != nil {
		log.Printf("获取参与者列表失败: %v", err)
		respondError(w, http.StatusInternalServerError, "获取参与者列表失败")
		return
	}

	respondJSON(w, http.StatusOK, participants)
}
