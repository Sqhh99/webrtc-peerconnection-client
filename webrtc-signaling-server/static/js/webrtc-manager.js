// WebRTC连接管理类
class WebRTCManager {
    constructor(signalingManager, mediaManager) {
        this.signalingManager = signalingManager;
        this.mediaManager = mediaManager;
        this.peerConnection = null;
        this.currentPeer = null;
        
        // 统计数据缓存，用于计算码率
        this.prevVideoBytes = 0;
        this.prevAudioBytes = 0;
        this.prevVideoTime = 0;
        this.prevAudioTime = 0;
        
        // 增强的ICE配置，支持移动端穿透NAT
        this.configuration = {
            iceServers: [
                // Google STUN服务器
                { urls: 'stun:stun.l.google.com:19302' },
                { urls: 'stun:stun1.l.google.com:19302' },
                { urls: 'stun:stun2.l.google.com:19302' },
                { urls: 'stun:stun3.l.google.com:19302' },
                { urls: 'stun:stun4.l.google.com:19302' },
                // 其他公共STUN服务器
                { urls: 'stun:stun.stunprotocol.org:3478' },
                { urls: 'stun:stun.voiparound.com' },
                { urls: 'stun:stun.voipbuster.com' },
                // 免费的TURN服务器（用于移动端NAT穿透）
                {
                    urls: 'turn:numb.viagenie.ca',
                    credential: 'muazkh',
                    username: 'webrtc@live.com'
                },
                {
                    urls: 'turn:relay.metered.ca:80',
                    username: 'demo',
                    credential: 'demo'
                },
                {
                    urls: 'turn:relay.metered.ca:443',
                    username: 'demo',
                    credential: 'demo'
                }
            ],
            iceCandidatePoolSize: 10,
            // 移动端优化配置
            bundlePolicy: 'max-bundle',
            rtcpMuxPolicy: 'require',
            iceTransportPolicy: 'all' // 允许所有类型的候选（包括relay）
        };
        
        this.connectionState = 'new';
        this.gatheringState = 'new';
        this.signalingState = 'stable';
        
        this.setupEventHandlers();
    }
    
    setupEventHandlers() {
        // 不在这里注册信令消息处理器，因为CallManager已经处理了
    }
    
    // 创建新的PeerConnection
    createPeerConnection(peerId) {
        if (this.peerConnection) {
            this.closePeerConnection();
        }
        
        this.currentPeer = peerId;
        this.peerConnection = new RTCPeerConnection(this.configuration);
        
        // 设置事件监听器
        this.peerConnection.onicecandidate = (event) => {
            if (event.candidate) {
                const candidateType = this.getCandidateType(event.candidate.candidate);
                logger.log(`发送ICE候选 [${candidateType}]: ${event.candidate.candidate}`, 'debug');
                
                // 立即发送候选，对移动端很重要
                this.signalingManager.sendMessage({
                    type: 'ice-candidate',
                    from: this.signalingManager.clientId,
                    to: peerId,
                    payload: { 
                        candidate: event.candidate,
                        candidateType: candidateType
                    }
                });
            } else {
                logger.log('ICE候选收集完成', 'info');
                // 检查连接状态
                this.checkConnectionHealth();
            }
        };
        
        this.peerConnection.ontrack = (event) => {
            logger.log('接收到远程流', 'success');
            if (event.streams && event.streams[0]) {
                this.mediaManager.setRemoteStream(event.streams[0]);
            }
        };
        
        this.peerConnection.onconnectionstatechange = () => {
            this.connectionState = this.peerConnection.connectionState;
            logger.log(`WebRTC连接状态: ${this.connectionState}`, 'info');
            
            if (this.connectionState === 'connected') {
                this.onConnectionEstablished?.();
                this.startConnectionMonitoring();
            } else if (this.connectionState === 'disconnected') {
                logger.log('连接断开，尝试恢复...', 'warning');
                this.attemptReconnection();
            } else if (this.connectionState === 'failed') {
                logger.log('连接失败，尝试ICE重启...', 'error');
                this.restartIce();
            }
        };
        
        this.peerConnection.oniceconnectionstatechange = () => {
            logger.log(`ICE连接状态: ${this.peerConnection.iceConnectionState}`, 'info');
            this.connectionState = this.peerConnection.iceConnectionState;
            this.onConnectionChange?.(this.connectionState, this.currentPeer);
            
            // 当ICE连接建立时，通知连接建立完成
            if (this.peerConnection.iceConnectionState === 'connected' || this.peerConnection.iceConnectionState === 'completed') {
                logger.log('ICE连接建立成功，触发连接建立事件', 'success');
                this.onConnectionEstablished?.();
            }
        };
        
        this.peerConnection.onicegatheringstatechange = () => {
            this.gatheringState = this.peerConnection.iceGatheringState;
            logger.log(`ICE收集状态: ${this.gatheringState}`, 'info');
        };
        
        this.peerConnection.onsignalingstatechange = () => {
            this.signalingState = this.peerConnection.signalingState;
            logger.log(`信令状态: ${this.signalingState}`, 'info');
        };
        
        // 添加本地流
        if (this.mediaManager.hasStream()) {
            const stream = this.mediaManager.getCurrentStream();
            stream.getTracks().forEach(track => {
                logger.log(`添加轨道: ${track.kind}`, 'debug');
                this.peerConnection.addTrack(track, stream);
            });
        }
        
        logger.log(`已创建到 ${peerId} 的WebRTC连接`, 'info');
        return this.peerConnection;
    }
    
    // 发起呼叫 - 创建Offer
    async createOffer(peerId) {
        try {
            if (!this.peerConnection) {
                this.createPeerConnection(peerId);
            }
            
            logger.log(`开始创建offer给 ${peerId}`, 'info');
            
            // 移动端优化的offer配置
            const offer = await this.peerConnection.createOffer({
                offerToReceiveVideo: true,
                offerToReceiveAudio: true,
                voiceActivityDetection: true,
                iceRestart: false // 初始offer不重启ICE
            });
            
            await this.peerConnection.setLocalDescription(offer);
            
            logger.log('本地描述已设置，发送offer', 'info');
            this.signalingManager.sendMessage({
                type: 'offer',
                from: this.signalingManager.clientId,
                to: peerId,
                payload: { 
                    sdp: offer,
                    timestamp: Date.now()
                }
            });
            
            return true;
        } catch (error) {
            logger.log(`创建offer失败: ${error.message}`, 'error');
            this.onError?.(error);
            return false;
        }
    }
    
    // 处理接收到的Offer
    async handleOffer(message) {
        try {
            if (!this.peerConnection) {
                this.createPeerConnection(message.from);
            }
            
            logger.log(`收到来自 ${message.from} 的offer`, 'info');
            
            const remoteDesc = new RTCSessionDescription(message.payload.sdp);
            await this.peerConnection.setRemoteDescription(remoteDesc);
            
            logger.log('远程描述已设置，创建answer', 'info');
            const answer = await this.peerConnection.createAnswer();
            await this.peerConnection.setLocalDescription(answer);
            
            this.signalingManager.sendMessage({
                type: 'answer',
                from: this.signalingManager.clientId,
                to: message.from,
                payload: { 
                    sdp: answer,
                    timestamp: Date.now()
                }
            });
            
            logger.log('answer已发送', 'success');
        } catch (error) {
            logger.log(`处理offer失败: ${error.message}`, 'error');
            this.onError?.(error);
        }
    }
    
    // 处理接收到的Answer
    async handleAnswer(message) {
        try {
            if (!this.peerConnection) {
                logger.log('收到answer但没有peer connection', 'error');
                return;
            }
            
            logger.log(`收到来自 ${message.from} 的answer`, 'info');
            
            const remoteDesc = new RTCSessionDescription(message.payload.sdp);
            await this.peerConnection.setRemoteDescription(remoteDesc);
            
            logger.log('WebRTC连接建立成功', 'success');
        } catch (error) {
            logger.log(`处理answer失败: ${error.message}`, 'error');
            this.onError?.(error);
        }
    }
    
    // 处理ICE候选
    async handleIceCandidate(message) {
        try {
            if (!this.peerConnection) {
                logger.log('收到ICE候选但没有peer connection', 'warning');
                return;
            }
            
            const candidate = new RTCIceCandidate(message.payload.candidate);
            await this.peerConnection.addIceCandidate(candidate);
            logger.log('ICE候选已添加', 'debug');
        } catch (error) {
            logger.log(`添加ICE候选失败: ${error.message}`, 'error');
        }
    }
    
    // 关闭连接
    closePeerConnection() {
        if (this.peerConnection) {
            logger.log('正在关闭WebRTC连接...', 'info');
            
            // 停止监控
            this.stopConnectionMonitoring();
            
            // 关闭连接
            this.peerConnection.close();
            
            // 重置状态
            this.peerConnection = null;
            this.currentPeer = null;
            this.connectionState = 'closed';
            this.gatheringState = 'new';
            this.signalingState = 'stable';
            
            // 重置统计缓存
            this.prevVideoBytes = 0;
            this.prevAudioBytes = 0;
            this.prevVideoTime = 0;
            this.prevAudioTime = 0;
            
            // 清理远程媒体流
            this.mediaManager.clearRemoteStream();
            
            logger.log('WebRTC连接已完全关闭', 'info');
        }
    }
    
    // 完全重置WebRTC管理器
    resetWebRTCManager() {
        this.closePeerConnection();
        
        // 重置所有回调
        this.onConnectionChange = null;
        this.onError = null;
        
        logger.log('WebRTC管理器已完全重置', 'debug');
    }
    
    // 获取连接状态
    getConnectionInfo() {
        if (!this.peerConnection) {
            return {
                connected: false,
                connectionState: 'closed',
                signalingState: 'closed',
                gatheringState: 'closed'
            };
        }
        
        return {
            connected: this.connectionState === 'connected',
            connectionState: this.connectionState,
            signalingState: this.signalingState,
            gatheringState: this.gatheringState,
            currentPeer: this.currentPeer
        };
    }
    
    // 获取统计信息
    async getStats() {
        if (!this.peerConnection) {
            return null;
        }
        
        try {
            const stats = await this.peerConnection.getStats();
            const result = {
                audio: { sent: 0, received: 0, bitrate: 0, packetsLost: 0, packetsReceived: 0 },
                video: { sent: 0, received: 0, bitrate: 0, packetsLost: 0, packetsReceived: 0, frameRate: 0 },
                connection: {},
                packetLoss: 0,
                latency: 0,
                jitter: 0,
                videoCodec: 'H264',
                audioCodec: 'OPUS',
                videoResolution: '1280x720',
                iceConnectionState: this.peerConnection.iceConnectionState,
                signalingState: this.peerConnection.signalingState,
                connectionState: this.peerConnection.connectionState,
                dtlsState: 'new',
                transport: {}
            };
            
            let videoBytesReceived = 0, audioBytesReceived = 0;
            let videoTimestamp = 0, audioTimestamp = 0;
            let prevVideoBytesReceived = this.prevVideoBytes || 0;
            let prevAudioBytesReceived = this.prevAudioBytes || 0;
            let prevVideoTimestamp = this.prevVideoTime || 0;
            let prevAudioTimestamp = this.prevAudioTime || 0;
            
            stats.forEach(report => {
                // 出站RTP统计
                if (report.type === 'outbound-rtp') {
                    if (report.mediaType === 'audio') {
                        result.audio.sent = report.bytesSent || 0;
                    } else if (report.mediaType === 'video') {
                        result.video.sent = report.bytesSent || 0;
                        if (report.frameWidth && report.frameHeight) {
                            result.videoResolution = `${report.frameWidth}x${report.frameHeight}`;
                        }
                    }
                } 
                // 入站RTP统计
                else if (report.type === 'inbound-rtp') {
                    if (report.mediaType === 'audio') {
                        audioBytesReceived = report.bytesReceived || 0;
                        audioTimestamp = report.timestamp || Date.now();
                        result.audio.received = audioBytesReceived;
                        result.audio.packetsLost = report.packetsLost || 0;
                        result.audio.packetsReceived = report.packetsReceived || 0;
                        
                        // 计算音频码率
                        if (prevAudioTimestamp > 0) {
                            const timeDiff = (audioTimestamp - prevAudioTimestamp) / 1000;
                            const bytesDiff = audioBytesReceived - prevAudioBytesReceived;
                            result.audio.bitrate = timeDiff > 0 ? (bytesDiff * 8 / timeDiff) : 0;
                        }
                        
                        result.jitter = report.jitter || 0;
                    } else if (report.mediaType === 'video') {
                        videoBytesReceived = report.bytesReceived || 0;
                        videoTimestamp = report.timestamp || Date.now();
                        result.video.received = videoBytesReceived;
                        result.video.packetsLost = report.packetsLost || 0;
                        result.video.packetsReceived = report.packetsReceived || 0;
                        result.video.frameRate = report.framesPerSecond || 0;
                        
                        // 计算视频码率
                        if (prevVideoTimestamp > 0) {
                            const timeDiff = (videoTimestamp - prevVideoTimestamp) / 1000;
                            const bytesDiff = videoBytesReceived - prevVideoBytesReceived;
                            result.video.bitrate = timeDiff > 0 ? (bytesDiff * 8 / timeDiff) : 0;
                        }
                        
                        // 视频分辨率
                        if (report.frameWidth && report.frameHeight) {
                            result.videoResolution = `${report.frameWidth}x${report.frameHeight}`;
                        }
                    }
                }
                // 编解码器信息
                else if (report.type === 'codec') {
                    if (report.mimeType) {
                        if (report.mimeType.includes('video')) {
                            result.videoCodec = report.mimeType.split('/')[1].toUpperCase();
                        } else if (report.mimeType.includes('audio')) {
                            result.audioCodec = report.mimeType.split('/')[1].toUpperCase();
                        }
                    }
                }
                // ICE候选对统计
                else if (report.type === 'candidate-pair' && report.state === 'succeeded') {
                    result.latency = (report.currentRoundTripTime || 0) * 1000;
                    result.transport.localCandidateType = report.localCandidateType;
                    result.transport.remoteCandidateType = report.remoteCandidateType;
                    result.transport.protocol = report.protocol;
                    result.connection = {
                        localAddress: report.localCandidateId,
                        remoteAddress: report.remoteCandidateId,
                        roundTripTime: report.currentRoundTripTime
                    };
                }
                // DTLS传输统计
                else if (report.type === 'transport') {
                    result.dtlsState = report.dtlsState || 'new';
                    result.transport.bytesSent = report.bytesSent || 0;
                    result.transport.bytesReceived = report.bytesReceived || 0;
                }
            });
            
            // 保存当前数据供下次计算使用
            this.prevVideoBytes = videoBytesReceived;
            this.prevAudioBytes = audioBytesReceived;
            this.prevVideoTime = videoTimestamp;
            this.prevAudioTime = audioTimestamp;
            
            // 计算总丢包率
            const totalLost = result.video.packetsLost + result.audio.packetsLost;
            const totalReceived = result.video.packetsReceived + result.audio.packetsReceived;
            if (totalReceived > 0) {
                result.packetLoss = (totalLost / (totalLost + totalReceived)) * 100;
            }
            
            return result;
        } catch (error) {
            logger.log(`获取统计信息失败: ${error.message}`, 'error');
            return null;
        }
    }
    
    // 获取候选类型
    getCandidateType(candidateStr) {
        if (candidateStr.includes('typ host')) return 'host';
        if (candidateStr.includes('typ srflx')) return 'srflx';
        if (candidateStr.includes('typ relay')) return 'relay';
        return 'unknown';
    }
    
    // 检查连接健康状态
    async checkConnectionHealth() {
        if (!this.peerConnection) return;
        
        const stats = await this.peerConnection.getStats();
        let hasRelay = false;
        let hasSrflx = false;
        
        stats.forEach(report => {
            if (report.type === 'local-candidate') {
                if (report.candidateType === 'relay') hasRelay = true;
                if (report.candidateType === 'srflx') hasSrflx = true;
            }
        });
        
        if (!hasRelay && !hasSrflx) {
            logger.log('警告：没有relay或srflx候选，移动端可能连接困难', 'warning');
        }
    }
    
    // 尝试重连
    async attemptReconnection() {
        if (!this.peerConnection || !this.currentPeer) return;
        
        // 等待一段时间看是否自动恢复
        setTimeout(async () => {
            if (this.connectionState === 'disconnected') {
                logger.log('连接未恢复，执行ICE重启', 'warning');
                await this.restartIce();
            }
        }, 3000);
    }
    
    // ICE重启
    async restartIce() {
        if (!this.peerConnection || !this.currentPeer) return;
        
        try {
            logger.log('开始ICE重启...', 'info');
            const offer = await this.peerConnection.createOffer({ iceRestart: true });
            await this.peerConnection.setLocalDescription(offer);
            
            this.signalingManager.sendMessage({
                type: 'offer',
                from: this.signalingManager.clientId,
                to: this.currentPeer,
                payload: {
                    sdp: offer,
                    iceRestart: true,
                    timestamp: Date.now()
                }
            });
            
            logger.log('ICE重启offer已发送', 'info');
        } catch (error) {
            logger.log(`ICE重启失败: ${error.message}`, 'error');
        }
    }
    
    // 开始连接监控
    startConnectionMonitoring() {
        if (this.monitoringInterval) {
            clearInterval(this.monitoringInterval);
        }
        
        this.monitoringInterval = setInterval(async () => {
            if (this.peerConnection && this.connectionState === 'connected') {
                const stats = await this.getStats();
                if (stats) {
                    // 检查是否有数据流动
                    if (stats.video.received === 0 && stats.audio.received === 0) {
                        logger.log('警告：没有接收到媒体数据', 'warning');
                    }
                }
            }
        }, 5000);
    }
    
    // 停止连接监控
    stopConnectionMonitoring() {
        if (this.monitoringInterval) {
            clearInterval(this.monitoringInterval);
            this.monitoringInterval = null;
            logger.log('连接监控已停止', 'debug');
        }
    }
    
    // 清理资源
    cleanup() {
        this.stopConnectionMonitoring();
        this.closePeerConnection();
    }
}
