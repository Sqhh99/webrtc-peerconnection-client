// 呼叫管理类
class CallManager {
    constructor(signalingManager, mediaManager) {
        this.signalingManager = signalingManager;
        this.mediaManager = mediaManager;
        this.currentCall = null;
        this.callState = 'idle'; // idle, calling, receiving, connected
        this.callRequestTimeout = null;
        this.offerTimeout = null;
        
        this.setupSignalingHandlers();
    }
    
    setupSignalingHandlers() {
        this.signalingManager.registerHandler('call-request', this.handleCallRequest.bind(this));
        this.signalingManager.registerHandler('call-response', this.handleCallResponse.bind(this));
        this.signalingManager.registerHandler('call-cancel', this.handleCallCancel.bind(this));
        this.signalingManager.registerHandler('call-end', this.handleCallEnd.bind(this));
        this.signalingManager.registerHandler('offer', this.handleOffer.bind(this));
        this.signalingManager.registerHandler('answer', this.handleAnswer.bind(this));
        this.signalingManager.registerHandler('ice-candidate', this.handleIceCandidate.bind(this));
        this.signalingManager.registerHandler('user-offline', this.handleUserOffline.bind(this));
    }
    
    // 发起呼叫
    async initiateCall(targetClientId) {
        if (!this.mediaManager.hasStream()) {
            ui.showError('请先开启摄像头或音频', '提示');
            return false;
        }
        
        if (this.callState !== 'idle') {
            ui.showError('当前正在通话中，请先结束当前通话', '提示');
            return false;
        }
        
        this.currentCall = targetClientId;
        this.callState = 'calling';
        
        logger.log(`发起呼叫到 ${targetClientId}`, 'info');
        
        // 发送呼叫请求
        const success = this.signalingManager.sendMessage({
            type: 'call-request',
            from: this.signalingManager.clientId,
            to: targetClientId,
            payload: { timestamp: Date.now() }
        });
        
        if (!success) {
            this.cancelCall();
            return false;
        }
        
        logger.log(`已发送呼叫请求到 ${targetClientId}，等待应答...`, 'info');
        
        // 设置30秒超时
        this.callRequestTimeout = setTimeout(() => {
            if (this.callState === 'calling') {
                logger.log('呼叫请求超时，对方未应答', 'warning');
                ui.showError('呼叫超时，对方未应答', '通话失败');
                this.cleanupCall();
            }
        }, 30000);
        
        this.onCallStateChange?.();
        return true;
    }
    
    // 取消呼叫
    cancelCall() {
        if (this.currentCall && this.callState === 'calling') {
            this.signalingManager.sendMessage({
                type: 'call-cancel',
                from: this.signalingManager.clientId,
                to: this.currentCall,
                payload: { reason: 'cancelled' }
            });
        }
        
        this.cleanupCall();
        logger.log('呼叫已取消', 'info');
    }
    
    // 接听呼叫
    async acceptCall(callerName) {
        // 确保有媒体流
        if (!this.mediaManager.hasStream()) {
            const hasCamera = await this.mediaManager.startCamera();
            if (!hasCamera) {
                const hasAudio = await this.mediaManager.startAudioOnly();
                if (!hasAudio) {
                    this.rejectCall(callerName);
                    return;
                }
            }
        }
        
        this.currentCall = callerName;
        this.callState = 'receiving';
        
        // 发送接受响应
        this.signalingManager.sendMessage({
            type: 'call-response',
            from: this.signalingManager.clientId,
            to: callerName,
            payload: { accepted: true }
        });
        
        logger.log(`已接听来自 ${callerName} 的呼叫，状态: ${this.callState}`, 'info');
        this.onCallStateChange?.();
    }
    
    // 拒绝呼叫
    rejectCall(callerName) {
        this.signalingManager.sendMessage({
            type: 'call-response',
            from: this.signalingManager.clientId,
            to: callerName,
            payload: { accepted: false, reason: 'rejected' }
        });
        
        this.cleanupCall();
        logger.log(`已拒绝来自 ${callerName} 的呼叫`, 'info');
    }
    
    // 结束通话
    endCall() {
        logger.log('正在结束通话...', 'info');
        
        // 发送通话结束通知给对方
        if (this.currentCall) {
            this.signalingManager.sendMessage({
                type: 'call-end',
                from: this.signalingManager.clientId,
                to: this.currentCall,
                payload: { reason: 'hangup' }
            });
            
            // 通知WebRTC管理器结束连接
            this.onCallEnd?.(this.currentCall);
        }
        
        this.cleanupCall();
        logger.log('通话已结束', 'info');
    }
    
    // 清理通话资源
    cleanupCall() {
        this.clearCallTimeouts();
        this.mediaManager.clearRemoteStream();
        
        // 隐藏来电通知
        ui.hideIncomingCall();
        
        // 重置状态
        const wasInCall = this.callState !== 'idle';
        this.currentCall = null;
        this.callState = 'idle';
        
        // 触发状态变化
        if (wasInCall) {
            this.onCallStateChange?.();
        }
        
        logger.log('通话资源清理完成', 'debug');
    }
    
    // 处理呼叫请求
    handleCallRequest(message) {
        if (this.callState !== 'idle') {
            // 正在通话中，拒绝新呼叫
            this.signalingManager.sendMessage({
                type: 'call-response',
                from: this.signalingManager.clientId,
                to: message.from,
                payload: { accepted: false, reason: 'busy' }
            });
            return;
        }
        
        logger.log(`收到来自 ${message.from} 的呼叫请求`, 'info');
        ui.showIncomingCall(message.from, this);
    }
    
    // 处理呼叫响应
    async handleCallResponse(message) {
        this.clearCallTimeouts();
        
        if (message.payload.accepted) {
            logger.log('对方接听了呼叫，开始建立连接', 'success');
            this.callState = 'connecting';
            this.onCallStateChange?.();
            
            // 通知WebRTC管理器开始连接
            this.onStartWebRTCConnection?.(this.currentCall);
        } else {
            const reason = message.payload.reason === 'busy' ? '对方正在通话中' : '对方拒绝了呼叫';
            logger.log(`呼叫失败: ${reason}`, 'warning');
            ui.showError(reason, '通话失败');
            this.cleanupCall();
        }
    }
    
    // 处理呼叫取消
    handleCallCancel(message) {
        logger.log(`${message.from} 取消了呼叫`, 'info');
        this.cleanupCall();
    }
    
    // 处理通话结束
    handleCallEnd(message) {
        logger.log(`${message.from} 结束了通话`, 'info');
        
        // 通知WebRTC管理器结束连接
        if (this.currentCall === message.from) {
            this.onCallEnd?.(this.currentCall);
        }
        
        this.cleanupCall();
    }
    
    // 处理用户离线
    handleUserOffline(message) {
        const offlineUser = message.payload?.clientId;
        if (offlineUser && this.currentCall === offlineUser) {
            logger.log(`通话对方 ${offlineUser} 已离线`, 'warning');
            ui.showError('对方已离线，通话结束', '通话中断');
            this.cleanupCall();
        }
    }
    
    // 处理Offer
    handleOffer(message) {
        if (this.currentCall !== message.from) {
            logger.log(`收到来自 ${message.from} 的offer，但当前通话对象是 ${this.currentCall}`, 'warning');
            return;
        }
        
        this.onOfferReceived?.(message);
    }
    
    // 处理Answer
    handleAnswer(message) {
        if (this.currentCall !== message.from) {
            logger.log(`收到来自 ${message.from} 的answer，但当前通话对象是 ${this.currentCall}`, 'warning');
            return;
        }
        
        this.callState = 'connected';
        logger.log(`通话状态已变为connected，与 ${message.from} 的通话建立完成`, 'success');
        this.onCallStateChange?.();
        this.onAnswerReceived?.(message);
    }
    
    // 处理ICE候选
    handleIceCandidate(message) {
        if (this.currentCall !== message.from) {
            return;
        }
        
        this.onIceCandidateReceived?.(message);
    }
    
    clearCallTimeouts() {
        if (this.callRequestTimeout) {
            clearTimeout(this.callRequestTimeout);
            this.callRequestTimeout = null;
        }
        
        if (this.offerTimeout) {
            clearTimeout(this.offerTimeout);
            this.offerTimeout = null;
        }
    }
    
    getCallState() {
        return {
            currentCall: this.currentCall,
            callState: this.callState,
            isInCall: this.callState === 'connected',
            isCalling: this.callState === 'calling',
            isReceiving: this.callState === 'receiving',
            isConnecting: this.callState === 'connecting'
        };
    }
}
