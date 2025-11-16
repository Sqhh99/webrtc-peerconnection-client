// 会议 UI 控制器
class ConferenceUI {
    constructor() {
        this.elements = {
            videoGrid: document.getElementById('videoGrid'),
            localVideo: document.getElementById('localVideo'),
            localName: document.getElementById('localName'),
            localTile: document.getElementById('localParticipant'),
            roomName: document.getElementById('roomName'),
            participantCount: document.getElementById('participantCount'),
            sidebarParticipantCount: document.getElementById('sidebarParticipantCount'),
            emptyState: document.getElementById('emptyState'),
            loadingOverlay: document.getElementById('loadingOverlay'),
            loadingText: document.getElementById('loadingText'),
            participantsSidebar: document.getElementById('participantsSidebar'),
            chatSidebar: document.getElementById('chatSidebar'),
            participantsList: document.getElementById('participantsList'),
            chatMessages: document.getElementById('chatMessages'),
            chatInput: document.getElementById('chatInput'),
            chatSendBtn: document.getElementById('chatSendBtn'),
            connectionStatus: document.getElementById('connectionStatus'),
            networkQuality: document.getElementById('networkQuality'),
            connectionStateText: document.getElementById('connectionStateText'),
            callTimer: document.getElementById('callTimer'),
            screenShareIndicator: document.getElementById('screenShareIndicator'),
            shareScreenBtn: document.getElementById('shareScreenBtn'),
            chatBadge: document.querySelector('#chatBtn .badge'),
            toastContainer: document.getElementById('toastContainer')
        };
        
        this.remoteParticipants = new Map();
        this.screenShareTiles = new Map();
        this.localScreenShareElement = null;
        this.callTimerInterval = null;
        this.unreadChatCount = 0;
        this.trackSource = window.LivekitClient?.Track?.Source || {};
        this.screenShareSource = this.trackSource.SCREEN_SHARE || 'screen_share';
        
        this.setupSidebarListeners();
        this.setupChatListeners();
        this.setConnectionState('idle');
        this.updateScreenShareIndicator();
    }

    setupSidebarListeners() {
        // 关闭侧边栏按钮
        document.querySelectorAll('.close-sidebar').forEach(btn => {
            btn.addEventListener('click', () => {
                this.elements.participantsSidebar?.classList.remove('active');
                this.elements.chatSidebar?.classList.remove('active');
            });
        });

        // 参与者按钮
        const participantsBtn = document.getElementById('participantsBtn');
        if (participantsBtn) {
            participantsBtn.addEventListener('click', () => {
                this.toggleSidebar('participants');
            });
        }

        // 聊天按钮
        const chatBtn = document.getElementById('chatBtn');
        if (chatBtn) {
            chatBtn.addEventListener('click', () => {
                this.toggleSidebar('chat');
            });
        }

        // 分享按钮
        const shareBtn = document.getElementById('shareBtn');
        if (shareBtn) {
            shareBtn.addEventListener('click', () => {
                this.shareLink();
            });
        }
    }

    setupChatListeners() {
        if (this.elements.chatSendBtn) {
            this.elements.chatSendBtn.addEventListener('click', () => {
                this.sendChatMessage();
            });
        }

        if (this.elements.chatInput) {
            this.elements.chatInput.addEventListener('keypress', (e) => {
                if (e.key === 'Enter' && !e.shiftKey) {
                    e.preventDefault();
                    this.sendChatMessage();
                }
            });
        }
    }

    setLocalParticipantName(name) {
        if (this.elements.localName) {
            this.elements.localName.textContent = name || '我';
        }
    }

    setConnectionState(state) {
        const stateText = {
            'idle': '准备中',
            'connecting': '连接中',
            'connected': '已连接',
            'reconnecting': '重连中',
            'disconnected': '已断开'
        };
        const stateClass = {
            'idle': 'status-pill',
            'connecting': 'status-pill status-connecting',
            'connected': 'status-pill status-connected',
            'reconnecting': 'status-pill status-warning',
            'disconnected': 'status-pill status-error'
        };

        if (this.elements.connectionStateText) {
            this.elements.connectionStateText.textContent = stateText[state] || '未知状态';
            this.elements.connectionStateText.className = stateClass[state] || 'status-pill';
        }

        if (this.elements.connectionStatus) {
            const dotClass = {
                'connected': 'status-dot connected',
                'connecting': 'status-dot connecting',
                'reconnecting': 'status-dot warning',
                'disconnected': 'status-dot disconnected'
            };
            this.elements.connectionStatus.className = dotClass[state] || 'status-dot';
        }
    }

    startCallTimer(startTimestamp = Date.now()) {
        this.stopCallTimer();
        if (!this.elements.callTimer) return;

        const updateTimer = () => {
            const diff = Date.now() - startTimestamp;
            this.elements.callTimer.textContent = this.formatDuration(diff);
        };

        updateTimer();
        this.callTimerInterval = setInterval(updateTimer, 1000);
    }

    stopCallTimer() {
        if (this.callTimerInterval) {
            clearInterval(this.callTimerInterval);
            this.callTimerInterval = null;
        }
        if (this.elements.callTimer) {
            this.elements.callTimer.textContent = '00:00';
        }
    }

    formatDuration(durationMs) {
        const totalSeconds = Math.floor(durationMs / 1000);
        const hours = Math.floor(totalSeconds / 3600);
        const minutes = Math.floor((totalSeconds % 3600) / 60);
        const seconds = totalSeconds % 60;

        if (hours > 0) {
            return `${String(hours).padStart(2, '0')}:${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
        }
        return `${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
    }

    updateScreenShareIndicator() {
        if (!this.elements.screenShareIndicator) return;

        const hasScreenShare = this.screenShareTiles.size > 0 || !!this.localScreenShareElement;
        if (hasScreenShare) {
            this.elements.screenShareIndicator.style.display = 'inline-flex';
            this.elements.screenShareIndicator.classList.add('active');
            this.elements.screenShareIndicator.textContent = '屏幕共享中';
        } else {
            this.elements.screenShareIndicator.style.display = 'none';
            this.elements.screenShareIndicator.classList.remove('active');
            this.elements.screenShareIndicator.textContent = '屏幕共享';
        }
    }

    toggleSidebar(type) {
        if (type === 'participants') {
            const isActive = this.elements.participantsSidebar?.classList.toggle('active');
            this.elements.chatSidebar?.classList.remove('active');
            if (isActive) {
                this.updateParticipantsList();
            }
        } else if (type === 'chat') {
            const isActive = this.elements.chatSidebar?.classList.toggle('active');
            this.elements.participantsSidebar?.classList.remove('active');
            if (isActive) {
                this.clearChatUnread();
            }
        }
    }

    updateParticipantsList() {
        if (!this.elements.participantsList) return;
        
        const manager = window.conferenceManager;
        if (!manager || !manager.room) return;

        const participants = [];
        
        // 添加本地参与者
        if (manager.room.localParticipant) {
            participants.push({
                identity: manager.userName + ' (我)',
                isMicEnabled: manager.room.localParticipant.isMicrophoneEnabled,
                isCameraEnabled: manager.room.localParticipant.isCameraEnabled,
                isLocal: true,
                isScreenSharing: !!this.localScreenShareElement
            });
        }

        // 添加远程参与者
        const remoteParticipants = manager.getRemoteParticipants();
        if (remoteParticipants && remoteParticipants.length > 0) {
            remoteParticipants.forEach(p => {
                participants.push({
                    identity: p.identity,
                    isMicEnabled: p.isMicrophoneEnabled,
                    isCameraEnabled: p.isCameraEnabled,
                    isLocal: false,
                    isScreenSharing: this.screenShareTiles.has(p.sid)
                });
            });
        }

        this.elements.participantsList.innerHTML = participants.map(p => `
            <div class="participant-item">
                <div class="participant-avatar">
                    <i class="bi bi-person-circle"></i>
                </div>
                <div class="participant-details">
                    <div class="participant-name">
                        ${this.escapeHtml(p.identity)}
                        ${p.isScreenSharing ? '<span class="tag">屏幕共享</span>' : ''}
                    </div>
                    <div class="participant-status-icons">
                        <i class="bi ${p.isMicEnabled ? 'bi-mic-fill' : 'bi-mic-mute-fill'}" 
                           style="color: ${p.isMicEnabled ? '#10b981' : '#ef4444'}"></i>
                        <i class="bi ${p.isCameraEnabled ? 'bi-camera-video-fill' : 'bi-camera-video-off-fill'}"
                           style="color: ${p.isCameraEnabled ? '#10b981' : '#ef4444'}"></i>
                    </div>
                </div>
            </div>
        `).join('');
    }

    sendChatMessage() {
        const message = this.elements.chatInput?.value.trim();
        if (!message) return;

        const manager = window.conferenceManager;
        if (manager) {
            manager.sendChatMessage(message);
            this.elements.chatInput.value = '';
        }
    }

    onChatMessage(message, participant) {
        if (!this.elements.chatMessages) return;

        const isLocal = !participant; // 本地消息没有 participant
        const sender = isLocal ? '我' : (participant?.identity || '未知');
        const time = new Date(message.timestamp).toLocaleTimeString('zh-CN', { 
            hour: '2-digit', 
            minute: '2-digit' 
        });

        const messageEl = document.createElement('div');
        messageEl.className = `chat-message ${isLocal ? 'local' : 'remote'}`;
        messageEl.innerHTML = `
            <div class="message-header">
                <span class="message-sender">${this.escapeHtml(sender)}</span>
                <span class="message-time">${time}</span>
            </div>
            <div class="message-content">${this.escapeHtml(message.message)}</div>
        `;

        this.elements.chatMessages.appendChild(messageEl);
        this.elements.chatMessages.scrollTop = this.elements.chatMessages.scrollHeight;

        if (!isLocal && !this.elements.chatSidebar?.classList.contains('active')) {
            this.incrementChatUnread();
            this.showToast(`${sender}: ${message.message}`, 'info');
        }
    }

    incrementChatUnread() {
        this.unreadChatCount += 1;
        if (this.elements.chatBadge) {
            this.elements.chatBadge.style.display = 'inline-block';
            this.elements.chatBadge.textContent = this.unreadChatCount;
        }
    }

    clearChatUnread() {
        this.unreadChatCount = 0;
        if (this.elements.chatBadge) {
            this.elements.chatBadge.style.display = 'none';
        }
    }

    shareLink() {
        const room = this.elements.roomName?.textContent || '';
        const baseUrl = `${window.location.origin}/`;
        const url = `${baseUrl}?room=${encodeURIComponent(room)}`;

        if (navigator.share) {
            navigator.share({
                title: '邀请加入会议',
                text: `加入会议 ${room}`,
                url
            }).catch(() => {
                // 用户取消分享无需提示
            });
            return;
        }

        if (navigator.clipboard) {
            navigator.clipboard.writeText(url).then(() => {
                this.showToast('会议链接已复制到剪贴板', 'success');
            }).catch(() => {
                this.showToast('复制失败，请手动复制: ' + url, 'error');
            });
        } else {
            prompt('会议链接:', url);
        }
    }

    showLoading(text) {
        if (this.elements.loadingOverlay) {
            this.elements.loadingOverlay.style.display = 'flex';
            if (this.elements.loadingText) {
                this.elements.loadingText.textContent = text;
            }
        }
    }

    hideLoading() {
        if (this.elements.loadingOverlay) {
            this.elements.loadingOverlay.style.display = 'none';
        }
    }

    updateRoomInfo(roomName, participantCount) {
        if (this.elements.roomName) {
            this.elements.roomName.textContent = roomName;
        }
        if (this.elements.participantCount) {
            this.elements.participantCount.textContent = participantCount;
        }
        if (this.elements.sidebarParticipantCount) {
            this.elements.sidebarParticipantCount.textContent = participantCount;
        }

        const participantsBtn = document.getElementById('participantsBtn');
        const badge = participantsBtn?.querySelector('.badge');
        if (badge) {
            badge.style.display = participantCount > 0 ? 'inline-block' : 'none';
            badge.textContent = participantCount;
        }

        const safeName = roomName || '视频会议';
        document.title = `LiveKit · ${safeName} (${participantCount}人)`;

        // 更新参与者列表（如果打开）
        if (this.elements.participantsSidebar?.classList.contains('active')) {
            this.updateParticipantsList();
        }
    }

    updateEmptyState() {
        const totalParticipants = this.remoteParticipants.size + 1;
        const hasScreenShare = this.screenShareTiles.size > 0 || !!this.localScreenShareElement;
        if (this.elements.emptyState) {
            this.elements.emptyState.style.display = (totalParticipants <= 1 && !hasScreenShare) ? 'flex' : 'none';
        }
        console.log('更新空状态显示，参与者数量:', totalParticipants, '屏幕共享:', hasScreenShare);
    }

    attachLocalVideo(track) {
        if (this.elements.localVideo && track) {
            track.attach(this.elements.localVideo);
            console.log('✅ 本地视频已附加');
        }
    }

    onParticipantConnected(participant) {
        console.log('UI: 添加参与者', participant.identity);
        this.addParticipant(participant);
        this.updateParticipantCount();
        this.updateEmptyState();
        this.showToast(`${participant.identity} 加入了会议`, 'info');
    }

    onParticipantDisconnected(participant) {
        console.log('UI: 移除参与者', participant.identity);
        this.removeParticipant(participant.sid);
        this.removeScreenShareTile(participant.sid);
        this.updateParticipantCount();
        this.updateEmptyState();
        this.showToast(`${participant.identity} 离开了会议`, 'info');
    }

    onTrackSubscribed(track, participant, options = {}) {
        if (!track || !participant) return;

        if (track.kind === 'video') {
            if (options.isScreenShare) {
                this.attachScreenShareTrack(track, participant);
                return;
            }

            const element = this.remoteParticipants.get(participant.sid);
            if (element) {
                const video = element.querySelector('video');
                if (video) {
                    track.attach(video);
                    console.log('✅ 远程视频已附加:', participant.identity);
                }
            }
        } else if (track.kind === 'audio') {
            const audioElement = document.createElement('audio');
            audioElement.autoplay = true;
            audioElement.id = `audio-${participant.sid}${options.isScreenShareAudio ? '-screen' : ''}`;
            track.attach(audioElement);
            document.body.appendChild(audioElement);
        }
    }

    onTrackUnsubscribed(track, participant, options = {}) {
        if (!track || !participant) return;

        if (options.isScreenShare) {
            this.removeScreenShareTile(participant.sid);
            return;
        }

        if (track.kind === 'audio') {
            const suffix = options.isScreenShareAudio ? '-screen' : '';
            const audioElement = document.getElementById(`audio-${participant.sid}${suffix}`);
            if (audioElement) {
                audioElement.remove();
            }
        }
    }

    onConnectionQualityChanged(participant, quality) {
        const qualityMap = {
            'excellent': '优秀',
            'good': '良好',
            'poor': '较差',
            'lost': '断开'
        };

        const localSid = window.conferenceManager?.room?.localParticipant?.sid;
        const participantSid = participant?.sid;

        if (!participant || participantSid === localSid) {
            if (this.elements.networkQuality) {
                this.elements.networkQuality.textContent = qualityMap[quality] || '未知';
            }
            if (this.elements.connectionStatus) {
                const dotClass = (quality === 'excellent' || quality === 'good') ? 'connected' : 'warning';
                this.elements.connectionStatus.className = `status-dot ${dotClass}`;
            }
        } else if (participantSid && this.remoteParticipants.has(participantSid)) {
            const tile = this.remoteParticipants.get(participantSid);
            tile.dataset.quality = quality;
        }
    }

    onActiveSpeakersChanged(speakers = []) {
        const activeSet = new Set();
        speakers.forEach((speaker) => {
            if (speaker?.sid) {
                activeSet.add(speaker.sid);
            }
        });

        this.remoteParticipants.forEach((tile, sid) => {
            tile.classList.toggle('active-speaker', activeSet.has(sid));
        });

        const localSid = window.conferenceManager?.room?.localParticipant?.sid;
        if (this.elements.localTile && localSid) {
            this.elements.localTile.classList.toggle('active-speaker', activeSet.has(localSid));
        }
    }

    addParticipant(participant) {
        if (this.remoteParticipants.has(participant.sid)) return;

        const tile = document.createElement('div');
        tile.className = 'video-tile';
        tile.id = `participant-${participant.sid}`;
        tile.innerHTML = `
            <video autoplay playsinline></video>
            <div class="participant-info">
                <div class="name-badge">
                    <i class="bi bi-mic-fill audio-indicator"></i>
                    <span>${this.escapeHtml(participant.identity)}</span>
                </div>
            </div>
        `;

        this.remoteParticipants.set(participant.sid, tile);
        this.elements.videoGrid.appendChild(tile);

        if (participant.videoTracks && participant.videoTracks.size > 0) {
            participant.videoTracks.forEach((publication) => {
                if (!publication.track) return;
                if (publication.source === this.screenShareSource) {
                    this.attachScreenShareTrack(publication.track, participant);
                } else {
                    const video = tile.querySelector('video');
                    publication.track.attach(video);
                }
            });
        }

        if (participant.audioTracks && participant.audioTracks.size > 0) {
            participant.audioTracks.forEach((publication) => {
                if (publication.track) {
                    const audioElement = document.createElement('audio');
                    audioElement.autoplay = true;
                    audioElement.id = `audio-${participant.sid}`;
                    publication.track.attach(audioElement);
                    document.body.appendChild(audioElement);
                }
            });
        }

        // 监听麦克风状态变化
        participant.on('trackMuted', (publication) => {
            if (publication.kind === 'audio') {
                this.updateAudioIndicator(participant.sid, false);
            }
        });

        participant.on('trackUnmuted', (publication) => {
            if (publication.kind === 'audio') {
                this.updateAudioIndicator(participant.sid, true);
            }
        });
    }

    updateAudioIndicator(sid, enabled) {
        const element = this.remoteParticipants.get(sid);
        if (!element) return;

        const indicator = element.querySelector('.audio-indicator');
        if (indicator) {
            indicator.className = enabled ? 'bi bi-mic-fill audio-indicator' : 'bi bi-mic-mute-fill audio-indicator';
            indicator.style.color = enabled ? '#10b981' : '#ef4444';
        }
    }

    removeParticipant(sid) {
        const element = this.remoteParticipants.get(sid);
        if (element) {
            element.remove();
            this.remoteParticipants.delete(sid);
        }

        // 移除音频元素
        const audioElement = document.getElementById(`audio-${sid}`);
        if (audioElement) {
            audioElement.remove();
        }

        this.removeScreenShareTile(sid);
    }

    attachScreenShareTrack(track, participant) {
        if (!track || !participant?.sid) return;

        const sid = participant.sid;
        let tile = this.screenShareTiles.get(sid);
        if (!tile) {
            const label = (participant.identity || '参会者') + ' 的屏幕';
            tile = this.createScreenShareTile(`screen-${sid}`, label);
            this.screenShareTiles.set(sid, tile);
            this.elements.videoGrid.appendChild(tile);
        }

        const video = tile.querySelector('video');
        track.attach(video);
        tile.classList.add('active');
        this.updateScreenShareIndicator();
        this.showToast(`${participant.identity} 开始共享屏幕`, 'info');
        this.updateEmptyState();
    }

    createScreenShareTile(id, title) {
        const tile = document.createElement('div');
        tile.className = 'video-tile screen-share';
        tile.id = id;
        tile.innerHTML = `
            <video autoplay playsinline muted></video>
            <div class="participant-info">
                <div class="name-badge">
                    <i class="bi bi-display"></i>
                    <span>${this.escapeHtml(title)}</span>
                </div>
            </div>
        `;
        return tile;
    }

    removeScreenShareTile(participantSid) {
        if (!participantSid) return;
        const tile = this.screenShareTiles.get(participantSid);
        if (tile) {
            tile.remove();
            this.screenShareTiles.delete(participantSid);
            this.showToast('屏幕共享已结束', 'info');
        }

        this.updateScreenShareIndicator();
        this.updateEmptyState();
    }

    onLocalScreenShareStarted(track) {
        if (!track) return;
        if (!this.localScreenShareElement) {
            this.localScreenShareElement = this.createScreenShareTile('local-screen', '我的屏幕');
            this.localScreenShareElement.classList.add('local-share');
            this.elements.videoGrid.appendChild(this.localScreenShareElement);
        }

        const video = this.localScreenShareElement.querySelector('video');
        track.attach(video);
        this.updateScreenShareIndicator();
        this.showToast('屏幕共享已开启', 'success');
        this.updateEmptyState();
    }

    onLocalScreenShareStopped() {
        if (this.localScreenShareElement) {
            this.localScreenShareElement.remove();
            this.localScreenShareElement = null;
        }
        this.updateScreenShareIndicator();
        this.showToast('屏幕共享已结束', 'info');
        this.updateEmptyState();
    }

    updateParticipantCount() {
        const manager = window.conferenceManager;
        const count = manager ? manager.getParticipantCount() : (this.remoteParticipants.size + 1);
        this.updateRoomInfo(this.elements.roomName.textContent, count);
    }

    updateButtonState(buttonId, active) {
        const button = document.getElementById(buttonId);
        if (button) {
            if (active) {
                button.classList.add('active');
            } else {
                button.classList.remove('active');
            }
        }
    }

    showToast(message, type = 'info') {
        if (!this.elements.toastContainer) {
            console.log(`[${type}] ${message}`);
            return;
        }

        const toast = document.createElement('div');
        toast.className = `toast toast-${type}`;
        
        const iconMap = {
            'success': 'bi-check-circle-fill',
            'error': 'bi-x-circle-fill',
            'info': 'bi-info-circle-fill',
            'warning': 'bi-exclamation-triangle-fill'
        };
        
        toast.innerHTML = `
            <i class="bi ${iconMap[type] || iconMap.info}"></i>
            <span>${this.escapeHtml(message)}</span>
        `;

        this.elements.toastContainer.appendChild(toast);

        // 触发动画
        setTimeout(() => toast.classList.add('show'), 10);

        // 自动移除
        setTimeout(() => {
            toast.classList.remove('show');
            setTimeout(() => toast.remove(), 300);
        }, 3000);
    }

    escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }
}
