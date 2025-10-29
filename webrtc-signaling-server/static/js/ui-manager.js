// 现代化UI界面管理类
class UIManager {
    constructor() {
        this.elements = {
            // 连接状态
            connectionStatus: document.getElementById('connectionStatus'),
            
            // 视频元素
            localVideo: document.getElementById('localVideo'),
            remoteVideo: document.getElementById('remoteVideo'),
            localVideoPlaceholder: document.getElementById('localVideoPlaceholder'),
            remoteVideoPlaceholder: document.getElementById('remoteVideoPlaceholder'),
            
            // 媒体控制按钮
            startCameraBtn: document.getElementById('startCameraBtn'),
            stopCameraBtn: document.getElementById('stopCameraBtn'),
            startAudioBtn: document.getElementById('startAudioBtn'),
            stopAudioBtn: document.getElementById('stopAudioBtn'),
            
            // 通话控制按钮
            endCallBtn: document.getElementById('endCallBtn'),
            cancelCallBtn: document.getElementById('cancelCallBtn'),
            reconnectBtn: document.getElementById('reconnectBtn'),
            
            // 视频控制
            toggleVideoBtn: document.getElementById('toggleVideoBtn'),
            toggleAudioBtn: document.getElementById('toggleAudioBtn'),
            screenShareBtn: document.getElementById('screenShareBtn'),
            fullscreenBtn: document.getElementById('fullscreenBtn'),
            
            // 用户列表
            clientsList: document.getElementById('clientsList'),
            userCount: document.getElementById('userCount'),
            
            // 设备信息
            deviceType: document.getElementById('deviceType'),
            browserInfo: document.getElementById('browserInfo'),
            connectionQuality: document.getElementById('connectionQuality'),
            
            // 来电通知
            incomingCallNotification: document.getElementById('incomingCallNotification'),
            callerName: document.getElementById('callerName'),
            callerAvatar: document.getElementById('callerAvatar'),
            acceptCallBtn: document.getElementById('acceptCallBtn'),
            declineCallBtn: document.getElementById('declineCallBtn'),
            
            // 日志系统
            logOutput: document.getElementById('logOutput'),
            logFilter: document.getElementById('logFilter'),
            clearLogBtn: document.getElementById('clearLogBtn'),
            exportLogBtn: document.getElementById('exportLogBtn'),
            autoScrollToggle: document.getElementById('autoScrollToggle'),
            
            // 通话统计
            callStats: document.getElementById('callStats'),
            videoBitrate: document.getElementById('videoBitrate'),
            audioBitrate: document.getElementById('audioBitrate'),
            packetLoss: document.getElementById('packetLoss'),
            latency: document.getElementById('latency'),
            
            // 模态框
            errorModal: document.getElementById('errorModal'),
            errorMessage: document.getElementById('errorMessage'),
            loadingIndicator: document.getElementById('loadingIndicator'),
            loadingMessage: document.getElementById('loadingMessage')
        };
        
        this.callManager = null;
        this.mediaManager = null;
        this.autoScroll = true;
        this.isFullscreen = false;
        
        this.setupEventListeners();
        this.initializeDeviceDetection();
    }
    
    setManagers(callManager, mediaManager) {
        this.callManager = callManager;
        this.mediaManager = mediaManager;
    }
    
    setupEventListeners() {
        // 媒体控制按钮
        this.elements.startCameraBtn?.addEventListener('click', () => this.handleStartCamera());
        this.elements.stopCameraBtn?.addEventListener('click', () => this.handleStopCamera());
        this.elements.startAudioBtn?.addEventListener('click', () => this.handleStartAudio());
        this.elements.stopAudioBtn?.addEventListener('click', () => this.handleStopAudio());
        
        // 通话控制按钮
        this.elements.endCallBtn?.addEventListener('click', () => this.handleEndCall());
        this.elements.cancelCallBtn?.addEventListener('click', () => this.handleCancelCall());
        this.elements.reconnectBtn?.addEventListener('click', () => this.handleReconnect());
        
        // 视频控制按钮
        this.elements.toggleVideoBtn?.addEventListener('click', () => this.toggleVideo());
        this.elements.toggleAudioBtn?.addEventListener('click', () => this.toggleAudio());
        this.elements.screenShareBtn?.addEventListener('click', () => this.shareScreen());
        this.elements.fullscreenBtn?.addEventListener('click', () => this.toggleFullscreen());
        
        // 来电通知
        this.elements.acceptCallBtn?.addEventListener('click', () => this.handleAcceptCall());
        this.elements.declineCallBtn?.addEventListener('click', () => this.handleDeclineCall());
        
        // 日志控制
        this.elements.logFilter?.addEventListener('change', () => this.filterLogs());
        this.elements.clearLogBtn?.addEventListener('click', () => this.clearLogs());
        this.elements.exportLogBtn?.addEventListener('click', () => this.exportLogs());
        this.elements.autoScrollToggle?.addEventListener('change', (e) => {
            this.autoScroll = e.target.checked;
        });
        
        // 键盘快捷键
        document.addEventListener('keydown', (e) => this.handleKeyboard(e));
        
        // 全屏状态变化监听
        document.addEventListener('fullscreenchange', () => {
            this.isFullscreen = !!document.fullscreenElement;
            this.updateFullscreenButton();
        });
    }
    
    // 初始化设备检测
    initializeDeviceDetection() {
        const isMobile = this.detectMobileDevice();
        const browserInfo = this.getBrowserInfo();
        
        if (this.elements.deviceType) {
            this.elements.deviceType.textContent = isMobile ? '移动设备' : '桌面设备';
        }
        
        if (this.elements.browserInfo) {
            this.elements.browserInfo.textContent = browserInfo;
        }
    }
    
    // 检测移动设备
    detectMobileDevice() {
        return /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent);
    }
    
    // 获取浏览器信息
    getBrowserInfo() {
        const ua = navigator.userAgent;
        if (ua.indexOf('Chrome') > -1) return 'Chrome';
        if (ua.indexOf('Firefox') > -1) return 'Firefox';
        if (ua.indexOf('Safari') > -1) return 'Safari';
        if (ua.indexOf('Edge') > -1) return 'Edge';
        return '未知浏览器';
    }
    
    // 更新连接状态
    updateConnectionStatus(isConnected, clientId = null) {
        if (!this.elements.connectionStatus) return;
        
        const statusElement = this.elements.connectionStatus;
        const connectionLight = statusElement.querySelector('.connection-light');
        const statusText = statusElement.querySelector('span:not(.connection-light)'); // 选择不是connection-light的span
        
        if (isConnected) {
            statusElement.className = 'status-indicator connected';
            if (statusText) {
                statusText.textContent = `已连接 (ID: ${clientId})`;
            }
            if (connectionLight) {
                connectionLight.className = 'connection-light connected';
            }
        } else {
            statusElement.className = 'status-indicator disconnected';
            if (statusText) {
                statusText.textContent = '连接断开';
            }
            if (connectionLight) {
                connectionLight.className = 'connection-light disconnected';
            }
        }
    }
    
    // 更新在线用户列表
    updateClientList(clients) {
        if (!this.elements.clientsList) return;
        
        // 更新用户数量
        if (this.elements.userCount) {
            this.elements.userCount.textContent = clients.length;
        }
        
        // 清空现有列表
        this.elements.clientsList.innerHTML = '';
        
        if (clients.length === 0) {
            const placeholder = document.createElement('div');
            placeholder.className = 'client-placeholder';
            placeholder.innerHTML = '<p style="text-align: center; color: #6b7280; padding: 1rem;">暂无其他用户在线</p>';
            this.elements.clientsList.appendChild(placeholder);
            return;
        }
        
        // 添加用户项
        clients.forEach(client => {
            const clientItem = document.createElement('div');
            clientItem.className = 'client-item';
            
            // 生成头像字母
            const avatarLetter = client.id.charAt(client.id.length - 1).toUpperCase();
            
            clientItem.innerHTML = `
                <div class="client-info">
                    <div class="client-avatar">${avatarLetter}</div>
                    <div class="client-name">${client.id}</div>
                </div>
                <button class="btn btn-primary call-client-btn" onclick="this.handleCallUser('${client.id}')">
                    📞 呼叫
                </button>
            `;
            
            // 添加呼叫功能
            const callBtn = clientItem.querySelector('.call-client-btn');
            callBtn.addEventListener('click', () => {
                if (window.webrtcDemo) {
                    window.webrtcDemo.callUser(client.id);
                }
            });
            
            this.elements.clientsList.appendChild(clientItem);
        });
    }
    
    // 更新按钮状态
    updateButtons(callState, mediaState) {
        const { isInCall, isCalling, isReceiving, callState: currentState } = callState;
        const { hasVideo, hasAudio, hasStream } = mediaState;
        
        // 媒体控制按钮
        this.toggleButton(this.elements.startCameraBtn, !hasVideo);
        this.toggleButton(this.elements.stopCameraBtn, hasVideo);
        this.toggleButton(this.elements.startAudioBtn, !hasAudio);
        this.toggleButton(this.elements.stopAudioBtn, hasAudio);
        
        // 通话控制按钮
        // 接听方和发起方都应该能挂断电话，连接建立过程中也要显示挂断按钮
        this.toggleButton(this.elements.endCallBtn, isInCall || isReceiving || currentState === 'connecting');
        this.toggleButton(this.elements.cancelCallBtn, isCalling);
        
        // 视频控制按钮（通话中显示）
        this.toggleButton(this.elements.toggleVideoBtn, isInCall);
        this.toggleButton(this.elements.toggleAudioBtn, isInCall);
        this.toggleButton(this.elements.screenShareBtn, isInCall && !this.detectMobileDevice());
        
        // 更新视频占位符
        this.updateVideoPlaceholders(hasVideo, isInCall);
        
        // 更新通话统计显示
        this.toggleButton(this.elements.callStats, isInCall, 'block');
        
        // 如果通话结束，重置远程视频占位符文字
        if (currentState === 'idle' && this.elements.remoteVideoPlaceholder) {
            const placeholderText = this.elements.remoteVideoPlaceholder.querySelector('p');
            if (placeholderText) {
                placeholderText.textContent = '等待对方加入通话';
            }
        }
    }
    
    // 切换按钮显示状态
    toggleButton(element, show, displayType = 'inline-flex') {
        if (element) {
            element.style.display = show ? displayType : 'none';
        }
    }
    
    // 更新视频占位符
    updateVideoPlaceholders(hasLocalVideo, isInCall) {
        // 检查本地视频是否真的有流且有视频轨道
        const localVideo = this.elements.localVideo;
        const actualHasLocalVideo = localVideo && 
                                   localVideo.srcObject && 
                                   localVideo.srcObject.getVideoTracks().length > 0 && 
                                   localVideo.srcObject.getVideoTracks().some(track => track.enabled);
        
        if (this.elements.localVideoPlaceholder) {
            this.elements.localVideoPlaceholder.style.display = actualHasLocalVideo ? 'none' : 'flex';
        }
        
        // 检查远程视频
        const remoteVideo = this.elements.remoteVideo;
        const hasRemoteVideo = remoteVideo && 
                              remoteVideo.srcObject && 
                              remoteVideo.srcObject.getVideoTracks().length > 0 &&
                              remoteVideo.srcObject.getVideoTracks().some(track => track.enabled);
        
        if (this.elements.remoteVideoPlaceholder) {
            this.elements.remoteVideoPlaceholder.style.display = hasRemoteVideo ? 'none' : 'flex';
            
            // 更新占位符文字
            if (!hasRemoteVideo) {
                const placeholderText = this.elements.remoteVideoPlaceholder.querySelector('p');
                if (placeholderText) {
                    placeholderText.textContent = isInCall ? '等待对方视频...' : '等待对方加入通话';
                }
            }
        }
    }
    
    // 显示来电通知
    showIncomingCall(callerName, callManager) {
        if (!this.elements.incomingCallNotification) return;
        
        // 更新来电信息
        if (this.elements.callerName) {
            this.elements.callerName.textContent = callerName;
        }
        
        if (this.elements.callerAvatar) {
            this.elements.callerAvatar.textContent = callerName.charAt(callerName.length - 1).toUpperCase();
        }
        
        // 显示通知
        this.elements.incomingCallNotification.style.display = 'block';
        
        // 存储回调引用
        this._currentCaller = callerName;
        this._currentCallManager = callManager;
        
        // 播放提示音（如果支持）
        this.playNotificationSound();
        
        // 30秒后自动隐藏
        this._incomingCallTimeout = setTimeout(() => {
            this.hideIncomingCall();
        }, 30000);
    }
    
    // 隐藏来电通知
    hideIncomingCall() {
        if (this.elements.incomingCallNotification) {
            this.elements.incomingCallNotification.style.display = 'none';
        }
        
        // 清理来电相关状态
        this._currentCaller = null;
        this._currentCallManager = null;
        
        if (this._incomingCallTimeout) {
            clearTimeout(this._incomingCallTimeout);
            this._incomingCallTimeout = null;
        }
        
        this.stopNotificationSound();
        logger.log('来电通知已隐藏', 'debug');
    }
    
    // 显示错误信息
    showError(message, title = '发生错误') {
        if (this.elements.errorModal && this.elements.errorMessage) {
            this.elements.errorMessage.textContent = message;
            
            // 使用Bootstrap模态框
            const modal = new bootstrap.Modal(this.elements.errorModal);
            modal.show();
        } else {
            alert(`${title}: ${message}`);
        }
    }
    
    // 显示提示信息
    showAlert(message) {
        // 简化版本，直接显示错误
        this.showError(message, '提示');
    }
    
    // 显示加载状态
    showLoading(message = '请稍候...') {
        if (this.elements.loadingIndicator && this.elements.loadingMessage) {
            this.elements.loadingMessage.textContent = message;
            this.elements.loadingIndicator.style.display = 'flex';
        }
    }
    
    // 隐藏加载状态
    hideLoading() {
        if (this.elements.loadingIndicator) {
            this.elements.loadingIndicator.style.display = 'none';
        }
    }
    
    // 更新通话统计
    updateCallStats(stats) {
        if (!stats || !this.elements.callStats) return;
        
        if (this.elements.videoBitrate) {
            this.elements.videoBitrate.textContent = Math.round(stats.video.bitrate / 1000);
        }
        
        if (this.elements.audioBitrate) {
            this.elements.audioBitrate.textContent = Math.round(stats.audio.bitrate / 1000);
        }
        
        if (this.elements.packetLoss) {
            this.elements.packetLoss.textContent = `${stats.packetLoss.toFixed(1)}%`;
        }
        
        if (this.elements.latency) {
            this.elements.latency.textContent = `${stats.latency.toFixed(0)}ms`;
        }
        
        // 更新技术详情
        this.updateTechnicalDetails(stats);
    }
    
    // 更新技术详情
    updateTechnicalDetails(stats = {}) {
        // ICE连接状态
        const iceStateElement = document.getElementById('iceConnectionState');
        if (iceStateElement && stats.iceConnectionState) {
            iceStateElement.textContent = stats.iceConnectionState;
            iceStateElement.className = `info-value ${this.getStateClass(stats.iceConnectionState)}`;
        }
        
        // 信令状态
        const signalingStateElement = document.getElementById('signalingState');
        if (signalingStateElement && stats.signalingState) {
            signalingStateElement.textContent = stats.signalingState;
            signalingStateElement.className = `info-value ${this.getStateClass(stats.signalingState)}`;
        }
        
        // DTLS状态
        const dtlsStateElement = document.getElementById('dtlsState');
        if (dtlsStateElement && stats.dtlsState) {
            dtlsStateElement.textContent = stats.dtlsState;
            dtlsStateElement.className = `info-value ${this.getStateClass(stats.dtlsState)}`;
        }
        
        // 编解码器信息
        const videoCodecElement = document.getElementById('videoCodec');
        if (videoCodecElement && stats.videoCodec) {
            videoCodecElement.textContent = stats.videoCodec;
        }
        
        const audioCodecElement = document.getElementById('audioCodec');
        if (audioCodecElement && stats.audioCodec) {
            audioCodecElement.textContent = stats.audioCodec;
        }
        
        // 视频分辨率
        const resolutionElement = document.getElementById('videoResolution');
        if (resolutionElement && stats.videoResolution) {
            resolutionElement.textContent = stats.videoResolution;
        }
    }
    
    // 获取状态对应的CSS类
    getStateClass(state) {
        const stateMap = {
            // ICE连接状态
            'connected': 'success',
            'completed': 'success',
            'checking': 'warning',
            'disconnected': 'danger',
            'failed': 'danger',
            'closed': 'danger',
            'new': 'secondary',
            
            // 信令状态
            'stable': 'success',
            'have-local-offer': 'warning',
            'have-remote-offer': 'warning',
            'have-local-pranswer': 'warning',
            'have-remote-pranswer': 'warning',
            
            // DTLS状态
            'connected': 'success',
            'connecting': 'warning',
            'closed': 'danger',
            'failed': 'danger',
            'new': 'secondary'
        };
        
        return stateMap[state] || 'secondary';
    }
    
    // 更新连接质量指示器
    updateConnectionQuality(quality) {
        if (!this.elements.connectionQuality) return;
        
        const qualityMap = {
            'excellent': { text: '优秀', level: 5, className: 'success' },
            'good': { text: '良好', level: 4, className: 'success' },
            'fair': { text: '一般', level: 3, className: 'warning' },
            'poor': { text: '差', level: 2, className: 'danger' },
            'very-poor': { text: '很差', level: 1, className: 'danger' },
            'unknown': { text: '未知', level: 0, className: 'secondary' }
        };
        
        const q = qualityMap[quality] || qualityMap.unknown;
        this.elements.connectionQuality.textContent = q.text;
        this.elements.connectionQuality.className = `info-value ${q.className}`;
        
        // 更新质量指示条
        const qualityBars = document.getElementById('qualityBars');
        if (qualityBars) {
            const bars = qualityBars.querySelectorAll('.quality-bar');
            bars.forEach((bar, index) => {
                bar.className = 'quality-bar';
                if (index < q.level) {
                    bar.classList.add('active');
                    if (q.className === 'warning') bar.classList.add('warning');
                    if (q.className === 'danger') bar.classList.add('danger');
                }
            });
        }
    }
    
    // 日志管理
    addLog(message, level = 'info') {
        if (!this.elements.logOutput) return;
        
        const logEntry = document.createElement('div');
        logEntry.className = `log-entry log-${level}`;
        logEntry.innerHTML = `
            <span class="log-time">[${new Date().toLocaleTimeString()}]</span>
            <span class="log-level">[${level.toUpperCase()}]</span>
            <span class="log-message">${this.escapeHtml(message)}</span>
        `;
        
        this.elements.logOutput.appendChild(logEntry);
        
        // 限制日志条数（最多1000条）
        const logEntries = this.elements.logOutput.querySelectorAll('.log-entry');
        if (logEntries.length > 1000) {
            logEntries[0].remove();
        }
        
        // 应用当前过滤器
        this.applyLogFilter(logEntry);
        
        // 自动滚动到底部
        if (this.autoScroll) {
            this.elements.logOutput.scrollTop = this.elements.logOutput.scrollHeight;
        }
    }
    
    // HTML转义
    escapeHtml(text) {
        const map = {
            '&': '&amp;',
            '<': '&lt;',
            '>': '&gt;',
            '"': '&quot;',
            "'": '&#039;'
        };
        return text.replace(/[&<>"']/g, m => map[m]);
    }
    
    // 应用日志过滤器
    applyLogFilter(entry = null) {
        if (!this.elements.logFilter) return;
        
        const filterValue = this.elements.logFilter.value;
        const entries = entry ? [entry] : this.elements.logOutput.querySelectorAll('.log-entry');
        
        entries.forEach(logEntry => {
            if (filterValue === 'all' || logEntry.classList.contains(`log-${filterValue}`)) {
                logEntry.style.display = 'block';
            } else {
                logEntry.style.display = 'none';
            }
        });
    }
    
    // 过滤日志
    filterLogs() {
        this.applyLogFilter();
    }
    
    // 清空日志
    clearLogs() {
        if (this.elements.logOutput) {
            this.elements.logOutput.innerHTML = '';
        }
    }
    
    // 导出日志
    exportLogs() {
        if (!this.elements.logOutput) return;
        
        const logs = Array.from(this.elements.logOutput.querySelectorAll('.log-entry'))
            .filter(entry => entry.style.display !== 'none')
            .map(entry => entry.textContent)
            .join('\n');
        
        const blob = new Blob([logs], { type: 'text/plain' });
        const url = URL.createObjectURL(blob);
        
        const a = document.createElement('a');
        a.href = url;
        a.download = `webrtc-logs-${new Date().toISOString().slice(0, 19)}.txt`;
        a.click();
        
        URL.revokeObjectURL(url);
    }
    
    // 事件处理器
    async handleStartCamera() {
        if (this.mediaManager) {
            this.showLoading('正在开启摄像头...');
            try {
                await this.mediaManager.startCamera();
            } finally {
                this.hideLoading();
            }
        }
    }
    
    handleStopCamera() {
        if (this.mediaManager) {
            this.mediaManager.stopCamera();
        }
    }
    
    async handleStartAudio() {
        if (this.mediaManager) {
            this.showLoading('正在开启音频...');
            try {
                await this.mediaManager.startAudioOnly();
            } finally {
                this.hideLoading();
            }
        }
    }
    
    handleStopAudio() {
        if (this.mediaManager) {
            this.mediaManager.stopAudio();
        }
    }
    
    handleEndCall() {
        if (this.callManager) {
            this.callManager.endCall();
        }
    }
    
    handleCancelCall() {
        if (this.callManager) {
            this.callManager.cancelCall();
        }
    }
    
    handleReconnect() {
        if (window.webrtcDemo && window.webrtcDemo.connect) {
            this.showLoading('正在重新连接...');
            window.webrtcDemo.connect().finally(() => {
                this.hideLoading();
            });
        }
    }
    
    handleAcceptCall() {
        if (this._currentCallManager && this._currentCaller) {
            this._currentCallManager.acceptCall(this._currentCaller);
        }
        this.hideIncomingCall();
    }
    
    handleDeclineCall() {
        if (this._currentCallManager && this._currentCaller) {
            this._currentCallManager.rejectCall(this._currentCaller);
        }
        this.hideIncomingCall();
    }
    
    // 视频控制功能
    toggleVideo() {
        // 切换视频开关状态
        if (this.mediaManager && this.mediaManager.hasStream()) {
            const videoTracks = this.mediaManager.getCurrentStream().getVideoTracks();
            videoTracks.forEach(track => {
                track.enabled = !track.enabled;
            });
        }
    }
    
    toggleAudio() {
        // 切换音频开关状态
        if (this.mediaManager && this.mediaManager.hasStream()) {
            const audioTracks = this.mediaManager.getCurrentStream().getAudioTracks();
            audioTracks.forEach(track => {
                track.enabled = !track.enabled;
            });
        }
    }
    
    async shareScreen() {
        if (!navigator.mediaDevices || !navigator.mediaDevices.getDisplayMedia) {
            this.showError('当前浏览器不支持屏幕共享功能');
            return;
        }
        
        try {
            const screenStream = await navigator.mediaDevices.getDisplayMedia({
                video: true,
                audio: true
            });
            
            // 替换视频轨道（需要WebRTC管理器支持）
            logger.log('开始屏幕共享', 'info');
        } catch (error) {
            logger.log(`屏幕共享失败: ${error.message}`, 'error');
        }
    }
    
    toggleFullscreen() {
        const videoArea = document.querySelector('.video-area');
        if (!videoArea) return;
        
        if (this.isFullscreen) {
            document.exitFullscreen().catch(console.log);
        } else {
            videoArea.requestFullscreen().catch(console.log);
        }
    }
    
    updateFullscreenButton() {
        if (this.elements.fullscreenBtn) {
            this.elements.fullscreenBtn.innerHTML = this.isFullscreen ? 
                '🔍 退出全屏' : '🔍 全屏';
        }
    }
    
    // 键盘快捷键处理
    handleKeyboard(e) {
        // 空格键：切换音频
        if (e.code === 'Space' && e.ctrlKey) {
            e.preventDefault();
            this.toggleAudio();
        }
        
        // Ctrl+V：切换视频
        if (e.code === 'KeyV' && e.ctrlKey) {
            e.preventDefault();
            this.toggleVideo();
        }
        
        // F11：全屏
        if (e.code === 'F11') {
            e.preventDefault();
            this.toggleFullscreen();
        }
        
        // Esc：结束通话
        if (e.code === 'Escape' && this.callManager && this.callManager.getCallState().isInCall) {
            this.handleEndCall();
        }
    }
    
    // 播放通知声音
    playNotificationSound() {
        try {
            // 创建简单的提示音
            const audioContext = new (window.AudioContext || window.webkitAudioContext)();
            const oscillator = audioContext.createOscillator();
            const gainNode = audioContext.createGain();
            
            oscillator.connect(gainNode);
            gainNode.connect(audioContext.destination);
            
            oscillator.frequency.setValueAtTime(800, audioContext.currentTime);
            gainNode.gain.setValueAtTime(0.1, audioContext.currentTime);
            
            oscillator.start();
            oscillator.stop(audioContext.currentTime + 0.1);
            
            // 重复播放
            this._notificationInterval = setInterval(() => {
                const osc = audioContext.createOscillator();
                const gain = audioContext.createGain();
                osc.connect(gain);
                gain.connect(audioContext.destination);
                osc.frequency.setValueAtTime(800, audioContext.currentTime);
                gain.gain.setValueAtTime(0.1, audioContext.currentTime);
                osc.start();
                osc.stop(audioContext.currentTime + 0.1);
            }, 2000);
        } catch (error) {
            console.log('无法播放提示音:', error);
        }
    }
    
    stopNotificationSound() {
        if (this._notificationInterval) {
            clearInterval(this._notificationInterval);
            this._notificationInterval = null;
        }
    }
}

// 全局UI实例
const ui = new UIManager();