// 主应用类 - 协调各个管理器，增强错误处理和稳定性
class WebRTCDemo {
    constructor() {
        this.clientId = null;
        this.isInitialized = false;
        this.retryCount = 0;
        this.maxRetries = 3;
        
        // 创建管理器实例
        try {
            this.signalingManager = new SignalingManager();
            this.mediaManager = new MediaManager();
            this.callManager = new CallManager(this.signalingManager, this.mediaManager);
            this.webrtcManager = new WebRTCManager(this.signalingManager, this.mediaManager);
            
            // 设置UI管理器
            ui.setManagers(this.callManager, this.mediaManager);
            
            this.setupConnections();
            this.setupErrorHandling();
            this.initializeApp();
        } catch (error) {
            logger.log(`应用初始化失败: ${error.message}`, 'error');
            this.handleCriticalError(error);
        }
    }
    
    async initializeApp() {
        try {
            logger.log('正在初始化WebRTC演示应用...', 'info');
            
            // 检查浏览器兼容性
            await this.checkBrowserCompatibility();
            
            // 检测设备能力
            await this.detectDeviceCapabilities();
            
            // 自动连接到信令服务器
            await this.autoConnect();
            
            this.isInitialized = true;
            logger.log('应用初始化完成', 'success');
            
        } catch (error) {
            logger.log(`应用初始化过程中出错: ${error.message}`, 'error');
            this.handleInitializationError(error);
        }
    }
    
    // 检查浏览器兼容性
    async checkBrowserCompatibility() {
        const issues = [];
        
        // 检查WebRTC支持
        if (!window.RTCPeerConnection) {
            issues.push('不支持WebRTC (RTCPeerConnection)');
        }
        
        // 检查媒体设备API
        if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
            issues.push('不支持媒体设备访问 (getUserMedia)');
        }
        
        // 检查WebSocket支持
        if (!window.WebSocket) {
            issues.push('不支持WebSocket');
        }
        
        // 检查安全上下文
        if (!window.isSecureContext && !['localhost', '127.0.0.1'].includes(location.hostname)) {
            issues.push('非安全上下文（需要HTTPS）');
        }
        
        if (issues.length > 0) {
            const message = `浏览器兼容性问题:\n${issues.map(issue => `• ${issue}`).join('\n')}`;
            logger.log(message, 'warning');
            ui.showError(message, '兼容性警告');
        } else {
            logger.log('浏览器兼容性检查通过', 'success');
        }
    }
    
    // 检测设备能力
    async detectDeviceCapabilities() {
        try {
            logger.log('正在检测设备能力...', 'info');
            
            // 检测可用的媒体设备
            if (navigator.mediaDevices && navigator.mediaDevices.enumerateDevices) {
                const devices = await navigator.mediaDevices.enumerateDevices();
                const videoDevices = devices.filter(device => device.kind === 'videoinput');
                const audioDevices = devices.filter(device => device.kind === 'audioinput');
                
                logger.log(`检测到 ${videoDevices.length} 个视频设备, ${audioDevices.length} 个音频设备`, 'info');
                
                if (videoDevices.length === 0 && audioDevices.length === 0) {
                    logger.log('未检测到媒体设备，可能需要授权', 'warning');
                }
            }
            
            // 检测屏幕共享支持
            const supportsScreenShare = navigator.mediaDevices && navigator.mediaDevices.getDisplayMedia;
            logger.log(`屏幕共享支持: ${supportsScreenShare ? '是' : '否'}`, 'info');
            
            // 检测移动设备特性
            const isMobile = this.mediaManager.detectMobileDevice();
            logger.log(`设备类型: ${isMobile ? '移动设备' : '桌面设备'}`, 'info');
            
            // 更新连接质量为检测完成
            ui.updateConnectionQuality('unknown');
            
        } catch (error) {
            logger.log(`设备能力检测失败: ${error.message}`, 'warning');
        }
    }
    
    setupConnections() {
        try {
            // 信令管理器事件
            this.signalingManager.onConnectionChange = (isConnected, clientId) => {
                this.handleConnectionChange(isConnected, clientId);
            };
            
            this.signalingManager.registerHandler('client-list', (message) => {
                this.handleClientList(message);
            });
            
            this.signalingManager.registerHandler('user-offline', (message) => {
                this.handleUserOffline(message);
            });
            
            // 呼叫管理器事件
            this.callManager.onCallStateChange = () => {
                this.updateUI();
            };
            
            this.callManager.onStartWebRTCConnection = async (peerId) => {
                await this.handleStartWebRTCConnection(peerId);
            };
            
            this.callManager.onOfferReceived = (message) => {
                this.webrtcManager.handleOffer(message);
            };
            
            this.callManager.onAnswerReceived = (message) => {
                this.webrtcManager.handleAnswer(message);
            };
            
            this.callManager.onIceCandidateReceived = (message) => {
                this.webrtcManager.handleIceCandidate(message);
            };
            
            this.callManager.onCallEnd = (peerId) => {
                logger.log(`清理与 ${peerId} 的WebRTC连接`, 'info');
                this.stopStatsMonitoring();
                this.webrtcManager.closePeerConnection();
                
                // 确保UI状态更新
                this.updateUI();
            };
            
            // 媒体管理器事件
            this.mediaManager.onStreamChange = () => {
                this.updateUI();
            };
            
            // WebRTC管理器事件
            this.webrtcManager.onConnectionEstablished = () => {
                logger.log('WebRTC连接建立，更新通话状态为connected', 'success');
                // 更新呼叫管理器状态为连接已建立
                if (this.callManager) {
                    this.callManager.callState = 'connected';
                    this.callManager.onCallStateChange?.();
                }
                this.handleWebRTCConnectionEstablished();
            };
            
            this.webrtcManager.onConnectionLost = () => {
                this.handleWebRTCConnectionLost();
            };
            
            this.webrtcManager.onError = (error) => {
                this.handleWebRTCError(error);
            };
            
            logger.log('事件监听器设置完成', 'debug');
            
        } catch (error) {
            logger.log(`设置连接监听器失败: ${error.message}`, 'error');
            throw error;
        }
    }
    
    // 设置全局错误处理
    setupErrorHandling() {
        // 全局错误捕获
        window.addEventListener('error', (event) => {
            logger.log(`全局错误: ${event.error?.message || event.message}`, 'error');
            this.handleGlobalError(event.error || new Error(event.message));
        });
        
        // Promise拒绝捕获
        window.addEventListener('unhandledrejection', (event) => {
            logger.log(`未处理的Promise拒绝: ${event.reason?.message || event.reason}`, 'error');
            this.handleGlobalError(event.reason);
            event.preventDefault(); // 防止控制台错误
        });
        
        // 页面可见性变化处理
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) {
                logger.log('页面进入后台', 'debug');
            } else {
                logger.log('页面回到前台', 'debug');
                this.handlePageVisible();
            }
        });
        
        // 网络状态变化处理
        if ('onLine' in navigator) {
            window.addEventListener('online', () => {
                logger.log('网络连接已恢复', 'success');
                this.handleNetworkOnline();
            });
            
            window.addEventListener('offline', () => {
                logger.log('网络连接中断', 'warning');
                this.handleNetworkOffline();
            });
        }
    }
    
    // 处理连接状态变化
    handleConnectionChange(isConnected, clientId) {
        this.clientId = clientId;
        ui.updateConnectionStatus(isConnected, clientId);
        
        if (isConnected) {
            logger.log(`WebSocket连接成功，客户端ID: ${clientId}`, 'success');
            this.retryCount = 0; // 重置重试计数
            ui.updateConnectionQuality('good');
        } else {
            logger.log('WebSocket连接断开', 'warning');
            ui.updateConnectionQuality('poor');
            this.handleConnectionLoss();
        }
    }
    
    // 处理客户端列表更新
    handleClientList(message) {
        try {
            const clients = message.payload.clients.filter(client => client.id !== this.clientId);
            ui.updateClientList(clients);
            logger.log(`在线用户列表已更新，共 ${clients.length} 个用户`, 'info');
        } catch (error) {
            logger.log(`处理用户列表失败: ${error.message}`, 'error');
        }
    }
    
    // 处理用户离线
    handleUserOffline(message) {
        const offlineClientId = message.payload.clientId;
        logger.log(`用户 ${offlineClientId} 离线`, 'info');
        
        // 如果正在与该用户通话，结束通话
        if (this.callManager.currentCall === offlineClientId) {
            logger.log('通话对方已离线，结束通话', 'warning');
            this.callManager.endCall();
        }
    }
    
    // 处理开始WebRTC连接
    async handleStartWebRTCConnection(peerId) {
        try {
            logger.log(`准备与 ${peerId} 建立WebRTC连接`, 'info');
            
            // 确保有媒体流
            if (!this.mediaManager.hasStream()) {
                logger.log('没有媒体流，尝试获取摄像头...', 'info');
                const hasCamera = await this.mediaManager.startCamera();
                
                if (!hasCamera) {
                    logger.log('摄像头获取失败，尝试纯音频模式...', 'warning');
                    const hasAudio = await this.mediaManager.startAudioOnly();
                    
                    if (!hasAudio) {
                        logger.log('无法获取任何媒体流，取消呼叫', 'error');
                        this.callManager.cancelCall();
                        ui.showError('无法访问摄像头和麦克风，请检查设备权限');
                        return;
                    }
                }
            }
            
            // 开始WebRTC连接
            await this.webrtcManager.createOffer(peerId);
            
        } catch (error) {
            logger.log(`建立WebRTC连接失败: ${error.message}`, 'error');
            this.callManager.cancelCall();
            ui.showError('建立连接失败，请重试');
        }
    }
    
    
    // 处理WebRTC连接建立
    handleWebRTCConnectionEstablished() {
        logger.log('WebRTC连接已建立', 'success');
        ui.updateConnectionQuality('excellent');
        
        // 开始统计监控
        this.startStatsMonitoring();
    }
    
    // 处理WebRTC连接丢失
    handleWebRTCConnectionLost() {
        logger.log('WebRTC连接丢失', 'warning');
        ui.updateConnectionQuality('poor');
        
        // 停止统计监控
        this.stopStatsMonitoring();
    }
    
    // 处理WebRTC错误
    handleWebRTCError(error) {
        logger.log(`WebRTC错误: ${error.message}`, 'error');
        ui.showError(`连接错误: ${error.message}`);
        ui.updateConnectionQuality('poor');
    }
    
    // 处理连接丢失
    handleConnectionLoss() {
        if (this.retryCount < this.maxRetries) {
            const delay = Math.min(1000 * Math.pow(2, this.retryCount), 10000);
            this.retryCount++;
            
            logger.log(`连接断开，${delay/1000}秒后进行第${this.retryCount}次重连尝试...`, 'warning');
            
            setTimeout(() => {
                this.connect();
            }, delay);
        } else {
            logger.log('达到最大重连次数，停止重连', 'error');
            ui.showError('连接服务器失败，请检查网络连接或刷新页面重试');
        }
    }
    
    // 处理页面可见性变化
    handlePageVisible() {
        // 页面回到前台时，检查连接状态
        if (!this.signalingManager.isConnected) {
            logger.log('页面回到前台，检查连接状态', 'debug');
            this.connect();
        }
    }
    
    // 处理网络在线
    handleNetworkOnline() {
        ui.updateConnectionQuality('good');
        if (!this.signalingManager.isConnected) {
            this.connect();
        }
    }
    
    // 处理网络离线
    handleNetworkOffline() {
        ui.updateConnectionQuality('poor');
        // 清理现有连接
        this.webrtcManager.cleanup();
    }
    
    // 处理全局错误
    handleGlobalError(error) {
        // 避免错误循环
        if (this._handlingGlobalError) return;
        this._handlingGlobalError = true;
        
        try {
            // 记录错误详情
            logger.log(`全局错误处理: ${error?.stack || error?.message || error}`, 'error');
            
            // 根据错误类型进行不同处理
            if (error?.name === 'SecurityError') {
                ui.showError('权限错误：无法访问媒体设备，请检查浏览器权限设置');
            } else if (error?.name === 'NotAllowedError') {
                ui.showError('访问被拒绝：请允许访问摄像头和麦克风');
            } else if (error?.name === 'NetworkError') {
                ui.showError('网络错误：请检查网络连接');
            } else {
                // 一般性错误，不显示给用户以免困惑
                console.error('应用错误:', error);
            }
            
        } finally {
            this._handlingGlobalError = false;
        }
    }
    
    // 处理关键错误
    handleCriticalError(error) {
        logger.log(`关键错误: ${error.message}`, 'error');
        ui.showError('应用初始化失败，请刷新页面重试', '关键错误');
        
        // 禁用所有交互
        document.body.style.pointerEvents = 'none';
        document.body.style.opacity = '0.5';
    }
    
    // 处理初始化错误
    handleInitializationError(error) {
        logger.log(`初始化错误: ${error.message}`, 'error');
        
        // 尝试降级模式
        if (this.retryCount < 3) {
            this.retryCount++;
            logger.log(`初始化失败，尝试降级模式 (${this.retryCount}/3)`, 'warning');
            
            setTimeout(() => {
                this.initializeApp();
            }, 2000);
        } else {
            this.handleCriticalError(error);
        }
    }
    
    // 计算连接质量
    calculateConnectionQuality(stats) {
        if (!stats) return 'unknown';
        
        let score = 0;
        let factors = 0;
        
        // 丢包率评分 (40% 权重)
        if (stats.packetLoss !== undefined) {
            factors++;
            if (stats.packetLoss < 1) score += 40;
            else if (stats.packetLoss < 3) score += 30;
            else if (stats.packetLoss < 5) score += 20;
            else if (stats.packetLoss < 10) score += 10;
        }
        
        // 延迟评分 (30% 权重)
        if (stats.latency !== undefined) {
            factors++;
            if (stats.latency < 50) score += 30;
            else if (stats.latency < 100) score += 25;
            else if (stats.latency < 200) score += 20;
            else if (stats.latency < 300) score += 15;
            else if (stats.latency < 500) score += 10;
        }
        
        // 码率评分 (30% 权重)
        if (stats.video.bitrate !== undefined) {
            factors++;
            if (stats.video.bitrate > 1000000) score += 30; // >1Mbps
            else if (stats.video.bitrate > 500000) score += 25; // >500kbps
            else if (stats.video.bitrate > 200000) score += 20; // >200kbps
            else if (stats.video.bitrate > 100000) score += 15; // >100kbps
            else if (stats.video.bitrate > 50000) score += 10; // >50kbps
        }
        
        if (factors === 0) return 'unknown';
        
        const normalizedScore = score / factors;
        
        if (normalizedScore >= 25) return 'excellent';
        if (normalizedScore >= 20) return 'good';
        if (normalizedScore >= 15) return 'fair';
        if (normalizedScore >= 10) return 'poor';
        return 'very-poor';
    }
    
    // 开始统计监控
    startStatsMonitoring() {
        if (this.statsInterval) {
            clearInterval(this.statsInterval);
        }
        
        this.statsInterval = setInterval(async () => {
            try {
                const stats = await this.webrtcManager.getStats();
                if (stats) {
                    ui.updateCallStats(stats);
                    
                    // 更新连接质量
                    const quality = this.calculateConnectionQuality(stats);
                    ui.updateConnectionQuality(quality);
                }
            } catch (error) {
                logger.log(`获取通话统计失败: ${error.message}`, 'debug');
            }
        }, 2000);
    }
    
    // 停止统计监控
    stopStatsMonitoring() {
        if (this.statsInterval) {
            clearInterval(this.statsInterval);
            this.statsInterval = null;
            logger.log('通话统计监控已停止', 'debug');
            
            // 隐藏统计面板
            const callStats = document.getElementById('callStats');
            if (callStats) {
                callStats.style.display = 'none';
            }
        }
    }
    
    // 自动连接
    async autoConnect() {
        try {
            await this.connect();
        } catch (error) {
            logger.log(`自动连接失败: ${error.message}`, 'warning');
        }
    }
    
    // 连接到信令服务器
    async connect() {
        try {
            logger.log('正在连接到信令服务器...', 'info');
            ui.updateConnectionQuality('fair');
            
            const success = await this.signalingManager.connect();
            if (!success) {
                throw new Error('信令服务器连接失败');
            }
            
        } catch (error) {
            logger.log(`连接错误: ${error.message}`, 'error');
            ui.updateConnectionQuality('poor');
            throw error;
        }
    }
    
    // 呼叫用户
    async callUser(targetClientId) {
        try {
            if (!this.isInitialized) {
                throw new Error('应用尚未初始化完成');
            }
            
            if (!this.signalingManager.isConnected) {
                throw new Error('信令服务器未连接');
            }
            
            if (targetClientId === this.clientId) {
                throw new Error('不能呼叫自己');
            }
            
            logger.log(`准备呼叫用户: ${targetClientId}`, 'info');
            
            const success = await this.callManager.initiateCall(targetClientId);
            if (success) {
                this.updateUI();
                logger.log(`呼叫 ${targetClientId} 成功发起`, 'success');
            } else {
                throw new Error('发起呼叫失败');
            }
            
        } catch (error) {
            logger.log(`呼叫失败: ${error.message}`, 'error');
            ui.showError(error.message);
        }
    }
    
    // 更新UI状态
    updateUI() {
        try {
            const callState = this.callManager.getCallState();
            const mediaState = this.mediaManager.getMediaState();
            
            ui.updateButtons(callState, mediaState);
            
            // 更新连接状态显示
            if (callState.isInCall) {
                ui.updateConnectionQuality('excellent');
            } else if (this.signalingManager.isConnected) {
                ui.updateConnectionQuality('good');
            }
            
        } catch (error) {
            logger.log(`更新UI失败: ${error.message}`, 'error');
        }
    }
    
    // 获取应用状态
    getStatus() {
        try {
            return {
                initialized: this.isInitialized,
                clientId: this.clientId,
                signaling: this.signalingManager.getConnectionStatus(),
                call: this.callManager.getCallState(),
                media: this.mediaManager.getMediaState(),
                webrtc: this.webrtcManager.getConnectionInfo(),
                retryCount: this.retryCount
            };
        } catch (error) {
            logger.log(`获取状态失败: ${error.message}`, 'error');
            return { error: error.message };
        }
    }
    
    // 获取调试信息
    async getDebugInfo() {
        try {
            const status = this.getStatus();
            const stats = await this.webrtcManager.getStats();
            
            return {
                ...status,
                stats,
                userAgent: navigator.userAgent,
                timestamp: new Date().toISOString(),
                performance: {
                    memory: performance.memory ? {
                        used: Math.round(performance.memory.usedJSHeapSize / 1024 / 1024),
                        total: Math.round(performance.memory.totalJSHeapSize / 1024 / 1024),
                        limit: Math.round(performance.memory.jsHeapSizeLimit / 1024 / 1024)
                    } : null
                }
            };
        } catch (error) {
            logger.log(`获取调试信息失败: ${error.message}`, 'error');
            return { error: error.message };
        }
    }
    
    // 清理资源
    cleanup() {
        try {
            logger.log('开始清理应用资源', 'info');
            
            this.stopStatsMonitoring();
            this.webrtcManager?.cleanup();
            this.mediaManager?.stopAll();
            this.signalingManager?.disconnect();
            
            // 清理事件监听器
            window.removeEventListener('error', this.handleGlobalError);
            window.removeEventListener('unhandledrejection', this.handleGlobalError);
            
            logger.log('应用资源清理完成', 'info');
        } catch (error) {
            logger.log(`清理资源失败: ${error.message}`, 'error');
        }
    }
}

// 增强的日志管理器
class Logger {
    constructor() {
        this.levels = {
            'debug': 0,
            'info': 1,
            'success': 2,
            'warning': 3,
            'error': 4
        };
        this.currentLevel = 'debug';
        this.maxLogEntries = 1000;
        this.logHistory = [];
    }
    
    log(message, level = 'info') {
        try {
            const timestamp = new Date();
            const logEntry = {
                timestamp,
                level,
                message,
                stackTrace: level === 'error' ? this.getStackTrace() : null
            };
            
            // 添加到历史记录
            this.logHistory.push(logEntry);
            if (this.logHistory.length > this.maxLogEntries) {
                this.logHistory.shift();
            }
            
            // 控制台输出
            if (this.levels[level] >= this.levels[this.currentLevel]) {
                const consoleMethod = this.getConsoleMethod(level);
                consoleMethod(`[${level.toUpperCase()}] ${message}`);
                
                // 错误级别输出堆栈
                if (level === 'error' && logEntry.stackTrace) {
                    console.error('Stack trace:', logEntry.stackTrace);
                }
            }
            
            // UI显示
            if (typeof ui !== 'undefined' && ui.addLog) {
                ui.addLog(message, level);
            }
            
        } catch (error) {
            // 避免日志系统自身错误导致的循环
            console.error('Logger error:', error);
        }
    }
    
    getConsoleMethod(level) {
        switch (level) {
            case 'error': return console.error;
            case 'warning': return console.warn;
            case 'info': 
            case 'success': return console.info;
            case 'debug': return console.debug;
            default: return console.log;
        }
    }
    
    getStackTrace() {
        try {
            const stack = new Error().stack;
            return stack ? stack.split('\n').slice(3, 8).join('\n') : null;
        } catch {
            return null;
        }
    }
    
    setLevel(level) {
        if (this.levels.hasOwnProperty(level)) {
            this.currentLevel = level;
            this.log(`日志级别已设置为: ${level}`, 'info');
        }
    }
    
    exportLogs(filterLevel = null) {
        try {
            let logs = this.logHistory;
            
            if (filterLevel && this.levels[filterLevel] !== undefined) {
                logs = logs.filter(entry => this.levels[entry.level] >= this.levels[filterLevel]);
            }
            
            return logs.map(entry => 
                `[${entry.timestamp.toISOString()}] [${entry.level.toUpperCase()}] ${entry.message}`
            ).join('\n');
        } catch (error) {
            console.error('Export logs error:', error);
            return '';
        }
    }
    
    getStats() {
        try {
            const stats = {};
            this.logHistory.forEach(entry => {
                stats[entry.level] = (stats[entry.level] || 0) + 1;
            });
            return stats;
        } catch (error) {
            console.error('Get stats error:', error);
            return {};
        }
    }
}

// 全局实例
const logger = new Logger();
let webrtcDemo;

// 页面加载完成后初始化
document.addEventListener('DOMContentLoaded', () => {
    logger.log('DOM加载完成，开始初始化WebRTC演示应用', 'info');
    
    // 添加加载指示器
    ui.showLoading('正在初始化应用...');
    
    // 延迟初始化以确保所有资源加载完成
    setTimeout(async () => {
        try {
            webrtcDemo = new WebRTCDemo();
            
            // 绑定视频元素
            const localVideo = document.getElementById('localVideo');
            const remoteVideo = document.getElementById('remoteVideo');
            
            if (localVideo && remoteVideo) {
                webrtcDemo.mediaManager.init(localVideo, remoteVideo);
                logger.log('视频元素绑定成功', 'success');
            } else {
                logger.log('视频元素未找到', 'error');
            }
            
            // 暴露到全局作用域
            window.webrtcDemo = webrtcDemo;
            window.logger = logger;
            
            // 开发模式下暴露调试方法
            if (location.hostname === 'localhost' || location.hostname === '127.0.0.1') {
                window.getDebugInfo = () => webrtcDemo.getDebugInfo();
                window.getStatus = () => webrtcDemo.getStatus();
                window.exportLogs = () => logger.exportLogs();
                logger.log('调试方法已暴露到全局作用域', 'debug');
            }
            
            ui.hideLoading();
            logger.log('应用完全加载完成', 'success');
            
        } catch (error) {
            ui.hideLoading();
            logger.log(`应用初始化失败: ${error.message}`, 'error');
            ui.showError('应用加载失败，请刷新页面重试');
        }
    }, 100);
});

// 页面卸载时清理资源
window.addEventListener('beforeunload', () => {
    if (webrtcDemo) {
        webrtcDemo.cleanup();
    }
});

// 性能监控
if ('performance' in window) {
    window.addEventListener('load', () => {
        setTimeout(() => {
            const perfData = performance.getEntriesByType('navigation')[0];
            if (perfData) {
                logger.log(`页面加载性能 - DOM: ${Math.round(perfData.domContentLoadedEventEnd - perfData.domContentLoadedEventStart)}ms, 完整: ${Math.round(perfData.loadEventEnd - perfData.loadEventStart)}ms`, 'debug');
            }
        }, 0);
    });
}