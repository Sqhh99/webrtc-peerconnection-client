// 媒体管理类
class MediaManager {
    constructor() {
        this.localStream = null;
        this.remoteStream = null;
        this.localVideo = null;
        this.remoteVideo = null;
        this.isCameraOn = false;
        this.isSecureContext = this.checkSecureContext();
        
        // 回调函数
        this.onStreamChange = null;
    }
    
    // 检查是否为安全上下文
    checkSecureContext() {
        // 首先检查 navigator.mediaDevices 是否可用
        const hasMediaDevices = navigator.mediaDevices && navigator.mediaDevices.getUserMedia;
        
        const isSecure = window.isSecureContext || 
                        location.protocol === 'https:' || 
                        location.hostname === 'localhost' || 
                        location.hostname === '127.0.0.1';
        
        // 即使是安全上下文，如果没有 mediaDevices API 也不行
        const isFullySupported = isSecure && hasMediaDevices;
        
        if (!isFullySupported) {
            logger.log('检测到媒体设备不可用，可能需要HTTPS协议', 'warning');
            this.showHTTPSWarning();
        }
        
        return isFullySupported;
    }
    
    // 显示HTTPS警告
    showHTTPSWarning() {
        const warningDiv = document.createElement('div');
        warningDiv.style.cssText = `
            position: fixed;
            top: 10px;
            right: 10px;
            background: #e74c3c;
            color: white;
            padding: 15px;
            border-radius: 8px;
            box-shadow: 0 4px 12px rgba(0,0,0,0.15);
            z-index: 1000;
            max-width: 350px;
            font-size: 14px;
            font-family: Arial, sans-serif;
        `;
        
        const currentPort = window.location.port || (window.location.protocol === 'https:' ? '443' : '80');
        const currentHost = window.location.hostname;
        const httpsUrl = currentHost === 'localhost' || currentHost === '127.0.0.1' 
            ? `https://localhost:8443` 
            : `https://${currentHost}:8443`;
        
        warningDiv.innerHTML = `
            <strong>🔒 需要HTTPS协议</strong><br>
            摄像头访问需要安全连接。<br>
            <a href="${httpsUrl}" 
               style="color: #fff; text-decoration: underline;"
               target="_blank">
                点击访问HTTPS版本 →
            </a>
            <button onclick="this.parentElement.remove()" 
                    style="float: right; background: none; border: none; color: white; cursor: pointer; font-size: 16px;">✕</button>
        `;
        document.body.appendChild(warningDiv);
        
        // 10秒后自动隐藏
        setTimeout(() => {
            if (warningDiv.parentElement) {
                warningDiv.remove();
            }
        }, 10000);
    }
    
    init(localVideoElement, remoteVideoElement) {
        this.localVideo = localVideoElement;
        this.remoteVideo = remoteVideoElement;
    }
    
    async startCamera() {
        try {
            // 首先检查安全上下文和媒体设备支持
            if (!this.isSecureContext) {
                throw new Error('摄像头访问需要HTTPS协议。请访问HTTPS版本以使用完整功能。');
            }
            
            // 双重检查浏览器是否支持 getUserMedia
            if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
                throw new Error('浏览器不支持媒体设备访问。请使用现代浏览器并确保在HTTPS协议下运行。');
            }
            
            // 检测设备类型
            const isMobile = this.detectMobileDevice();
            
            // 针对移动端优化的媒体约束
            const constraints = this.getOptimalConstraints(isMobile);
            
            logger.log(`请求媒体访问: ${JSON.stringify(constraints)}`, 'debug');
            
            this.localStream = await navigator.mediaDevices.getUserMedia(constraints);
            
            if (this.localVideo) {
                this.localVideo.srcObject = this.localStream;
                
                // 监听视频开始播放事件
                this.localVideo.addEventListener('loadeddata', () => {
                    logger.log('本地视频数据加载完成', 'debug');
                    // 隐藏占位符
                    const placeholder = document.getElementById('localVideoPlaceholder');
                    if (placeholder) {
                        placeholder.style.display = 'none';
                    }
                    // 触发UI更新
                    if (this.onStreamChange) {
                        this.onStreamChange();
                    }
                }, { once: true });
                
                this.localVideo.addEventListener('playing', () => {
                    logger.log('本地视频开始播放', 'debug');
                    // 再次确保占位符隐藏
                    const placeholder = document.getElementById('localVideoPlaceholder');
                    if (placeholder) {
                        placeholder.style.display = 'none';
                    }
                }, { once: true });
                
                // 确保视频可以播放
                try {
                    // 设置视频属性
                    this.localVideo.muted = true;
                    this.localVideo.autoplay = true;
                    this.localVideo.playsInline = true;
                    
                    await this.localVideo.play();
                    logger.log('本地视频播放成功', 'success');
                } catch (playError) {
                    logger.log(`视频播放警告: ${playError.message}`, 'warning');
                    // 尝试手动触发播放
                    setTimeout(() => {
                        this.localVideo.play().catch(e => {
                            logger.log(`延迟播放也失败: ${e.message}`, 'warning');
                        });
                    }, 100);
                }
            }
            
            this.isCameraOn = true;
            
            // 立即触发UI更新
            if (this.onStreamChange) {
                this.onStreamChange();
            }
            
            // 监听媒体轨道状态
            this.localStream.getTracks().forEach(track => {
                track.onended = () => {
                    logger.log(`媒体轨道已结束: ${track.kind}`, 'warning');
                };
            });
            
            logger.log('摄像头已开启', 'success');
            return true;
        } catch (error) {
            logger.log(`开启摄像头失败: ${error.message}`, 'error');
            
            // 如果是媒体设备不可用导致的错误，提供解决方案
            if (!this.isSecureContext || !navigator.mediaDevices) {
                this.showHTTPSInstructions();
            }
            
            return false;
        }
    }
    
    
    // 显示HTTPS使用说明
    showHTTPSInstructions() {
        const modal = document.createElement('div');
        modal.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0,0,0,0.7);
            display: flex;
            align-items: center;
            justify-content: center;
            z-index: 9999;
        `;
        
        const content = document.createElement('div');
        content.style.cssText = `
            background: white;
            padding: 30px;
            border-radius: 12px;
            max-width: 500px;
            text-align: center;
            box-shadow: 0 8px 32px rgba(0,0,0,0.3);
        `;
        
        const currentHost = window.location.hostname;
        const httpsUrl = `https://${currentHost}:8443`;
        
        content.innerHTML = `
            <h3 style="color: #e74c3c; margin-bottom: 20px;">🔒 需要HTTPS协议</h3>
            <p style="margin-bottom: 20px;">
                摄像头访问需要安全连接。请点击下面的链接切换到HTTPS版本：
            </p>
            <a href="${httpsUrl}" 
               style="display: inline-block; padding: 12px 24px; background: #3498db; color: white; 
                      text-decoration: none; border-radius: 6px; margin: 10px;"
               target="_blank">
                🔗 访问HTTPS版本
            </a>
            <p style="font-size: 12px; color: #666; margin-top: 15px;">
                注意：首次访问可能显示安全警告，请选择"继续访问"
            </p>
            <button onclick="this.closest('.https-modal').remove()" 
                    style="margin-top: 15px; padding: 8px 16px; background: #95a5a6; 
                           color: white; border: none; border-radius: 4px; cursor: pointer;">
                关闭
            </button>
        `;
        
        modal.className = 'https-modal';
        modal.appendChild(content);
        document.body.appendChild(modal);
        
        // 点击背景关闭
        modal.addEventListener('click', (e) => {
            if (e.target === modal) {
                modal.remove();
            }
        });
    }

    async startAudioOnly() {
        try {
            // 检查安全上下文
            if (!this.isSecureContext) {
                throw new Error('音频访问需要HTTPS协议。请访问 https://your-domain:8443 或在localhost环境下运行。');
            }
            
            // 检查浏览器是否支持 getUserMedia
            if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
                throw new Error('浏览器不支持媒体设备访问。请使用现代浏览器并确保在HTTPS协议下运行。');
            }
            
            // 优化的音频约束
            const audioConstraints = {
                audio: {
                    echoCancellation: true,
                    noiseSuppression: true,
                    autoGainControl: true,
                    sampleRate: 44100
                },
                video: false
            };
            
            this.localStream = await navigator.mediaDevices.getUserMedia(audioConstraints);
            
            // 如果有video元素，设置stream（音频模式下video会显示空白）
            if (this.localVideo) {
                this.localVideo.srcObject = this.localStream;
            }
            
            this.isCameraOn = false;
            logger.log('纯音频模式已开启', 'info');
            return true;
        } catch (error) {
            logger.log(`开启音频失败: ${error.message}`, 'error');
            
            // 如果是非安全上下文导致的错误，提供解决方案
            if (!this.isSecureContext) {
                this.showHTTPSInstructions();
            }
            
            return false;
        }
    }
    
    stopCamera() {
        if (this.localStream) {
            this.localStream.getTracks().forEach(track => track.stop());
            this.localStream = null;
            if (this.localVideo) {
                this.localVideo.srcObject = null;
            }
            this.isCameraOn = false;
            logger.log('摄像头已关闭', 'info');
            
            // 显示占位符
            const placeholder = document.getElementById('localVideoPlaceholder');
            if (placeholder) {
                placeholder.style.display = 'flex';
            }
            
            // 触发UI更新
            if (this.onStreamChange) {
                this.onStreamChange();
            }
        }
    }
    
    stopAudio() {
        if (this.localStream) {
            const audioTracks = this.localStream.getAudioTracks();
            audioTracks.forEach(track => track.stop());
            logger.log('音频已关闭', 'info');
        }
    }
    
    stopAll() {
        this.stopCamera();
        this.clearRemoteStream();
    }
    
    setRemoteStream(stream) {
        this.remoteStream = stream;
        if (this.remoteVideo) {
            this.remoteVideo.srcObject = stream;
            logger.log('远程视频流已设置', 'success');
        } else {
            logger.log('远程视频元素未找到', 'error');
        }
    }
    
    clearRemoteStream() {
        this.remoteStream = null;
        if (this.remoteVideo) {
            this.remoteVideo.srcObject = null;
        }
        logger.log('远程视频流已清除', 'info');
    }
    
    getCurrentStream() {
        return this.localStream;
    }
    
    getTracks() {
        return this.localStream ? this.localStream.getTracks() : [];
    }
    
    hasStream() {
        return !!this.localStream;
    }
    
    getMediaState() {
        return {
            hasVideo: this.isCameraOn && this.hasStream(),
            hasAudio: this.hasStream() && this.localStream.getAudioTracks().length > 0,
            hasStream: this.hasStream(),
            isMobile: this.detectMobileDevice()
        };
    }
    
    // 检测移动设备
    detectMobileDevice() {
        const userAgent = navigator.userAgent || navigator.vendor || window.opera;
        
        // 检测常见移动设备
        const isMobile = /android|webos|iphone|ipad|ipod|blackberry|iemobile|opera mini/i.test(userAgent.toLowerCase());
        
        // 检测触摸屏
        const hasTouch = ('ontouchstart' in window) || 
                        (navigator.maxTouchPoints > 0) || 
                        (navigator.msMaxTouchPoints > 0);
        
        // 检测屏幕宽度
        const isSmallScreen = window.innerWidth <= 768;
        
        return isMobile || (hasTouch && isSmallScreen);
    }
    
    // 获取最佳媒体约束
    getOptimalConstraints(isMobile) {
        let videoConstraints;
        let audioConstraints = {
            echoCancellation: true,
            noiseSuppression: true,
            autoGainControl: true,
            sampleRate: isMobile ? 16000 : 44100 // 移动端使用较低采样率
        };
        
        if (isMobile) {
            // 移动端优化设置
            videoConstraints = {
                facingMode: 'user', // 前置摄像头
                width: { ideal: 640, max: 1280 },
                height: { ideal: 480, max: 720 },
                frameRate: { ideal: 15, max: 30 },
                // 尝试不同的约束格式以提高兼容性
                aspectRatio: { ideal: 1.3333 }
            };
            
            logger.log('使用移动端优化约束', 'info');
        } else {
            // PC端设置
            videoConstraints = {
                width: { ideal: 1280, max: 1920 },
                height: { ideal: 720, max: 1080 },
                frameRate: { ideal: 30, max: 60 },
                aspectRatio: { ideal: 1.7778 }
            };
            
            logger.log('使用PC端高质量约束', 'info');
        }
        
        return {
            video: videoConstraints,
            audio: audioConstraints
        };
    }
    
    // 尝试不同的媒体约束（用于回退）
    async tryDifferentConstraints() {
        const fallbackConstraints = [
            // 第一次回退：简单约束
            { video: true, audio: true },
            // 第二次回退：更低分辨率
            { 
                video: { width: 640, height: 480 }, 
                audio: true 
            },
            // 第三次回退：只有音频
            { video: false, audio: true }
        ];
        
        for (const constraints of fallbackConstraints) {
            try {
                logger.log(`尝试回退约束: ${JSON.stringify(constraints)}`, 'info');
                this.localStream = await navigator.mediaDevices.getUserMedia(constraints);
                return true;
            } catch (error) {
                logger.log(`回退失败: ${error.message}`, 'warning');
                continue;
            }
        }
        
        return false;
    }
}
