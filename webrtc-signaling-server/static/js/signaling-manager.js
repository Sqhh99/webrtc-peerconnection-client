// 信令服务管理类
class SignalingManager {
    constructor() {
        this.socket = null;
        this.isConnected = false;
        this.clientId = null;
        this.reconnectAttempts = 0;
        this.isManualDisconnect = false;
        this.messageHandlers = new Map();
        
        this.setupEventHandlers();
    }
    
    setupEventHandlers() {
        // 注册默认消息处理器
        this.registerHandler('registered', this.handleRegistered.bind(this));
        this.registerHandler('client-list', this.handleClientList.bind(this));
        this.registerHandler('user-offline', this.handleUserOffline.bind(this));
    }
    
    registerHandler(messageType, handler) {
        this.messageHandlers.set(messageType, handler);
    }
    
    async connect(clientId = null) {
        if (this.isConnected) return true;
        
        // 生成或使用传入的客户端ID
        this.clientId = clientId || 'client_' + Math.random().toString(36).substr(2, 9);
        
        try {
            // 根据当前页面协议选择WebSocket协议
            const wsProtocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsUrl = `${wsProtocol}//${window.location.host}/ws/webrtc?uid=${this.clientId}`;
            
            logger.log(`正在连接到信令服务器: ${wsUrl}`, 'info');
            this.socket = new WebSocket(wsUrl);
            
            this.socket.onopen = () => {
                this.isConnected = true;
                this.reconnectAttempts = 0;
                logger.log('连接到信令服务器成功', 'success');
                
                // 注册客户端（现在通过URL参数，但仍然发送register消息以保持兼容）
                this.sendMessage({
                    type: 'register',
                    from: this.clientId
                });
                
                this.onConnectionChange?.(true, this.clientId);
            };
            
            this.socket.onmessage = (event) => {
                const message = JSON.parse(event.data);
                this.handleMessage(message);
            };
            
            this.socket.onclose = () => {
                this.isConnected = false;
                logger.log('与信令服务器连接断开', 'error');
                this.onConnectionChange?.(false);
                
                if (!this.isManualDisconnect && this.clientId) {
                    this.attemptReconnect();
                }
            };
            
            this.socket.onerror = (error) => {
                logger.log(`WebSocket错误: ${error}`, 'error');
            };
            
            return true;
        } catch (error) {
            logger.log(`连接失败: ${error.message}`, 'error');
            return false;
        }
    }
    
    disconnect() {
        this.isManualDisconnect = true;
        if (this.socket) {
            this.socket.close();
        }
        this.isConnected = false;
        this.isManualDisconnect = false;
    }
    
    sendMessage(message) {
        if (this.socket && this.socket.readyState === WebSocket.OPEN) {
            this.socket.send(JSON.stringify(message));
            return true;
        }
        return false;
    }
    
    handleMessage(message) {
        logger.log(`收到消息: ${message.type}`, 'info');
        
        const handler = this.messageHandlers.get(message.type);
        if (handler) {
            handler(message);
        } else {
            logger.log(`未处理的消息类型: ${message.type}`, 'warning');
        }
    }
    
    handleRegistered(message) {
        logger.log('客户端注册成功', 'success');
        this.onRegistered?.(message);
    }
    
    handleClientList(message) {
        this.onClientListUpdate?.(message.payload);
    }
    
    handleUserOffline(message) {
        const offlineClientId = message.payload.clientId;
        logger.log(`用户 ${offlineClientId} 已下线`, 'warning');
        this.onUserOffline?.(offlineClientId);
    }
    
    attemptReconnect() {
        if (this.reconnectAttempts >= 5) {
            logger.log('重连尝试次数过多，停止重连', 'error');
            return;
        }
        
        this.reconnectAttempts++;
        const delay = Math.min(1000 * Math.pow(2, this.reconnectAttempts - 1), 10000);
        
        logger.log(`${delay/1000}秒后尝试第${this.reconnectAttempts}次重连...`, 'warning');
        
        setTimeout(() => {
            if (!this.isConnected && this.clientId) {
                this.connect(this.clientId);
            }
        }, delay);
    }
}
