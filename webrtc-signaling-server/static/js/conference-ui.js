// 会议 UI 控制器（适配新版布局）
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
            loadingOverlay: document.getElementById('loadingOverlay'),
            loadingText: document.getElementById('loadingText'),
            participantsSidebar: document.getElementById('participantsSidebar'),
            chatSidebar: document.getElementById('chatSidebar'),
            participantsList: document.getElementById('participantsList'),
            chatMessages: document.getElementById('chatMessages'),
            chatInput: document.getElementById('chatInput'),
            chatSendBtn: document.getElementById('chatSendBtn'),
            chatBadge: document.querySelector('#chatBtn .badge'),
            connectionStatus: document.getElementById('connectionStatus'),
            networkQuality: document.getElementById('networkQuality'),
            connectionStateText: document.getElementById('connectionStateText'),
            miniConnectionState: document.getElementById('miniConnectionState'),
            callTimer: document.getElementById('callTimer'),
            screenShareIndicator: document.getElementById('screenShareIndicator'),
            shareScreenBtn: document.getElementById('shareScreenBtn'),
            toastContainer: document.getElementById('toastContainer'),
            railCount: document.getElementById('railCount'),
            railEmptyHint: document.getElementById('railEmptyHint'),
            stageVideo: document.getElementById('stageVideo'),
            stageLabel: document.getElementById('stageLabel'),
            emptyState: document.getElementById('emptyState'),
            resetSpotlightBtn: document.getElementById('resetSpotlightBtn'),
            previewSpotlightBtn: document.getElementById('previewSpotlightBtn'),
            typingIndicator: document.getElementById('typingIndicator'),
            collapseChatBtn: document.getElementById('collapseChatBtn'),
            chatSidebarToggle: document.getElementById('chatBtnBottom'),
            participantsSidebarToggle: document.getElementById('participantsBtnBottom'),
            copyLinkBtn: document.getElementById('copyStageBtn'),
            stageSurface: document.getElementById('stageSurface')
        };

        this.remoteParticipants = new Map(); // sid -> tile
        this.participantNames = new Map(); // sid -> name
        this.remoteCameraTracks = new Map(); // sid -> track
        this.remoteShareTracks = new Map(); // sid -> track

        this.localCameraTrack = null;
        this.localScreenShareTrack = null;
        this.stageTrack = null;
        this.stageParticipantSid = null;
        this.stageIsScreenShare = false;
        this.stageForcedByShare = null;
        this.userPinnedSid = null;
        this.lastActiveSpeakerSid = null;
        this.callTimerInterval = null;

        this.unreadChatCount = 0;
        this.typingParticipants = new Map(); // sid -> { name, timeout }
        this.isTyping = false;
        this.typingTimeoutId = null;

        this.setupSidebarListeners();
        this.setupChatListeners();
        this.bindStageControls();
        this.setConnectionState('idle');
        this.updateScreenShareIndicator();
        this.updateRailState();
    }

    setupSidebarListeners() {
        document.querySelectorAll('.close-sidebar').forEach(btn => {
            btn.addEventListener('click', () => {
                this.elements.participantsSidebar?.classList.remove('active');
                this.elements.chatSidebar?.classList.remove('active');
            });
        });

        const bindToggle = (button, type) => {
            if (!button) return;
            button.addEventListener('click', () => this.toggleSidebar(type));
        };

        bindToggle(document.getElementById('participantsBtn'), 'participants');
        bindToggle(this.elements.participantsSidebarToggle, 'participants');
        bindToggle(document.getElementById('chatBtn'), 'chat');
        bindToggle(this.elements.chatSidebarToggle, 'chat');

        [document.getElementById('shareBtn'), this.elements.copyLinkBtn].forEach(btn => {
            if (btn) {
                btn.addEventListener('click', () => this.shareLink());
            }
        });
    }

    bindStageControls() {
        if (this.elements.collapseChatBtn) {
            this.elements.collapseChatBtn.addEventListener('click', () => {
                this.elements.chatSidebar?.classList.toggle('active');
            });
        }

        if (this.elements.resetSpotlightBtn) {
            this.elements.resetSpotlightBtn.addEventListener('click', () => {
                this.clearUserSpotlight();
            });
        }

        if (this.elements.previewSpotlightBtn) {
            this.elements.previewSpotlightBtn.addEventListener('click', () => {
                this.previewLocalOnStage();
            });
        }

        if (this.elements.stageSurface) {
            this.elements.stageSurface.addEventListener('dblclick', () => {
                if (this.stageParticipantSid) {
                    this.userPinnedSid = this.stageParticipantSid;
                    this.setPinnedTile(this.stageParticipantSid);
                    this.showToast('已固定当前画面', 'info');
                }
            });
        }
    }

    setupChatListeners() {
        if (this.elements.chatSendBtn) {
            this.elements.chatSendBtn.addEventListener('click', () => this.sendChatMessage());
        }

        if (this.elements.chatInput) {
            this.elements.chatInput.addEventListener('keydown', (e) => {
                if (e.key === 'Enter' && (e.ctrlKey || !e.shiftKey)) {
                    e.preventDefault();
                    this.sendChatMessage();
                }
            });

            this.elements.chatInput.addEventListener('input', () => this.handleLocalTyping());
            this.elements.chatInput.addEventListener('blur', () => this.stopLocalTyping());
        }
    }

    setLocalParticipantName(name) {
        if (this.elements.localName) {
            this.elements.localName.textContent = name || '我';
        }
    }

    getLocalParticipantSid() {
        return window.conferenceManager?.room?.localParticipant?.sid || 'local';
    }

    setConnectionState(state) {
        const stateText = {
            idle: '准备中',
            connecting: '连接中',
            connected: '已连接',
            reconnecting: '重连中',
            disconnected: '已断开'
        };

        const stateClass = {
            idle: 'status-pill',
            connecting: 'status-pill status-connecting',
            connected: 'status-pill status-connected',
            reconnecting: 'status-pill status-warning',
            disconnected: 'status-pill status-error'
        };

        const text = stateText[state] || '未知状态';
        const cls = stateClass[state] || 'status-pill';

        if (this.elements.connectionStateText) {
            this.elements.connectionStateText.textContent = text;
            this.elements.connectionStateText.className = cls;
        }
        if (this.elements.miniConnectionState) {
            this.elements.miniConnectionState.textContent = text;
            this.elements.miniConnectionState.className = cls;
        }
        if (this.elements.connectionStatus) {
            const dotClass = {
                connected: 'status-dot connected',
                connecting: 'status-dot connecting',
                reconnecting: 'status-dot warning',
                disconnected: 'status-dot disconnected'
            };
            this.elements.connectionStatus.className = dotClass[state] || 'status-dot';
        }
    }

    startCallTimer(startTimestamp = Date.now()) {
        this.stopCallTimer();
        if (!this.elements.callTimer) return;

        const update = () => {
            const diff = Date.now() - startTimestamp;
            this.elements.callTimer.textContent = this.formatDuration(diff);
        };

        update();
        this.callTimerInterval = setInterval(update, 1000);
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
        const hasShare = this.remoteShareTracks.size > 0 || !!this.localScreenShareTrack;
        if (hasShare) {
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

        if (manager.room.localParticipant) {
            participants.push({
                identity: `${manager.userName || '我'} (我)`,
                isMicEnabled: manager.room.localParticipant.isMicrophoneEnabled,
                isCameraEnabled: manager.room.localParticipant.isCameraEnabled,
                isLocal: true,
                isScreenSharing: !!this.localScreenShareTrack
            });
        }

        const remotes = manager.getRemoteParticipants() || [];
        remotes.forEach((p) => {
            participants.push({
                identity: p.identity,
                isMicEnabled: p.isMicrophoneEnabled,
                isCameraEnabled: p.isCameraEnabled,
                isLocal: false,
                isScreenSharing: this.remoteShareTracks.has(p.sid)
            });
        });

        this.elements.participantsList.innerHTML = participants.map(p => `
            <div class="participant-item">
                <div class="participant-avatar">
                    <i class="bi ${p.isLocal ? 'bi-person-fill' : 'bi-person-circle'}"></i>
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
        if (!manager) return;

        manager.sendChatMessage(message);
        this.elements.chatInput.value = '';
        this.stopLocalTyping();
    }

    onChatMessage(message, participant) {
        if (!this.elements.chatMessages) return;

        const isLocal = !participant;
        const sender = isLocal ? '我' : (participant?.identity || message.sender || '参会者');
        const timeStr = new Date(message.timestamp || Date.now()).toLocaleTimeString('zh-CN', {
            hour: '2-digit',
            minute: '2-digit'
        });

        const wrapper = document.createElement('div');
        wrapper.className = `chat-message ${isLocal ? 'local' : 'remote'}`;
        wrapper.innerHTML = `
            <div class="message-header">
                <span class="message-sender">${this.escapeHtml(sender)}</span>
                <span class="message-time">${timeStr}</span>
            </div>
            <div class="message-bubble">${this.escapeHtml(message.message)}</div>
        `;

        this.elements.chatMessages.appendChild(wrapper);
        this.elements.chatMessages.scrollTop = this.elements.chatMessages.scrollHeight;

        if (!isLocal && !this.elements.chatSidebar?.classList.contains('active')) {
            this.incrementChatUnread();
            this.showToast(`${sender}: ${message.message}`, 'info');
        }
    }

    onTypingEvent(payload, participant) {
        if (!this.elements.typingIndicator) return;
        const sid = participant?.sid || payload?.sid;
        const name = participant?.identity || payload?.sender || '参会者';
        if (!sid) return;

        if (payload.isTyping) {
            if (this.typingParticipants.has(sid)) {
                clearTimeout(this.typingParticipants.get(sid).timer);
            }
            const timer = setTimeout(() => {
                this.typingParticipants.delete(sid);
                this.updateTypingIndicator();
            }, 5000);
            this.typingParticipants.set(sid, { name, timer });
        } else if (this.typingParticipants.has(sid)) {
            clearTimeout(this.typingParticipants.get(sid).timer);
            this.typingParticipants.delete(sid);
        }

        this.updateTypingIndicator();
    }

    handleLocalTyping() {
        if (this.isTyping) {
            if (this.typingTimeoutId) {
                clearTimeout(this.typingTimeoutId);
            }
            this.typingTimeoutId = setTimeout(() => this.stopLocalTyping(), 4000);
            return;
        }

        this.isTyping = true;
        window.conferenceManager?.sendTypingState(true);
        this.typingTimeoutId = setTimeout(() => this.stopLocalTyping(), 4000);
    }

    stopLocalTyping() {
        if (!this.isTyping) return;
        this.isTyping = false;
        window.conferenceManager?.sendTypingState(false);
        if (this.typingTimeoutId) {
            clearTimeout(this.typingTimeoutId);
            this.typingTimeoutId = null;
        }
    }

    updateTypingIndicator() {
        if (!this.elements.typingIndicator) return;
        if (this.typingParticipants.size === 0) {
            this.elements.typingIndicator.textContent = '';
            return;
        }
        const names = Array.from(this.typingParticipants.values()).map(item => item.name);
        const text = names.length > 2
            ? `${names.slice(0, 2).join('、')} 等多人正在输入...`
            : `${names.join('、')} 正在输入...`;
        this.elements.typingIndicator.textContent = text;
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
            }).catch(() => {});
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
        if (this.elements.railCount) {
            this.elements.railCount.textContent = this.remoteParticipants.size;
        }

        const participantsBtn = document.getElementById('participantsBtn');
        const badge = participantsBtn?.querySelector('.badge');
        if (badge) {
            badge.style.display = participantCount > 1 ? 'inline-block' : 'none';
            badge.textContent = participantCount;
        }

        const safeName = roomName || '视频会议';
        document.title = `LiveKit · ${safeName} (${participantCount}人)`;

        if (this.elements.participantsSidebar?.classList.contains('active')) {
            this.updateParticipantsList();
        }
    }

    updateRailState() {
        if (this.elements.railCount) {
            this.elements.railCount.textContent = this.remoteParticipants.size;
        }
        if (this.elements.railEmptyHint) {
            this.elements.railEmptyHint.style.display = this.remoteParticipants.size === 0 ? 'block' : 'none';
        }
    }

    updateEmptyState() {
        if (!this.elements.emptyState) return;
        const hasStageTrack = !!this.stageTrack;
        const hasRemote = this.remoteParticipants.size > 0;
        const hasShare = this.remoteShareTracks.size > 0 || !!this.localScreenShareTrack;
        const shouldShow = !(hasStageTrack || hasRemote || hasShare);
        this.elements.emptyState.style.display = shouldShow ? 'flex' : 'none';
    }

    attachLocalVideo(track) {
        if (this.elements.localVideo && track) {
            this.localCameraTrack = track;
            track.attach(this.elements.localVideo);
            if (this.userPinnedSid === 'local' || this.stageParticipantSid === 'local') {
                this.setStageTrack(track, {
                    sid: 'local',
                    identity: this.elements.localName?.textContent || '我'
                });
            }
        }
    }

    onParticipantConnected(participant) {
        if (!participant?.sid) return;
        this.participantNames.set(participant.sid, participant.identity || '参会者');
        this.addParticipant(participant);
        this.updateParticipantCount();
        this.updateRailState();
        this.updateEmptyState();
        this.showToast(`${participant.identity} 加入了会议`, 'info');
    }

    onParticipantDisconnected(participant) {
        if (!participant?.sid) return;
        this.remoteCameraTracks.delete(participant.sid);
        this.remoteShareTracks.delete(participant.sid);
        this.participantNames.delete(participant.sid);
        this.removeParticipant(participant.sid);

        if (this.stageParticipantSid === participant.sid) {
            this.stageTrack?.detach(this.elements.stageVideo);
            this.stageTrack = null;
            this.stageParticipantSid = null;
            this.stageIsScreenShare = false;
            this.maybeAutoSelectStage('participant-disconnected');
        }

        if (this.userPinnedSid === participant.sid) {
            this.userPinnedSid = null;
        }

        if (this.stageForcedByShare === participant.sid) {
            this.stageForcedByShare = null;
        }

        this.updateParticipantCount();
        this.updateRailState();
        this.updateEmptyState();
        this.showToast(`${participant.identity} 离开了会议`, 'info');
    }

    onTrackSubscribed(track, participant, options = {}) {
        if (!track || !participant) return;
        const isScreenShare = options.isScreenShare;
        this.participantNames.set(participant.sid, participant.identity || this.participantNames.get(participant.sid) || '参会者');

        if (track.kind === 'video') {
            if (isScreenShare) {
                this.attachScreenShareTrack(track, participant);
                return;
            }
            this.ensureParticipantTile(participant);
            this.remoteCameraTracks.set(participant.sid, track);
            this.attachTrackToTile(participant.sid, track);

            if (this.userPinnedSid === participant.sid) {
                this.setStageTrack(track, participant);
            } else {
                this.maybeAutoSelectStage('video-subscribed');
            }
        } else if (track.kind === 'audio') {
            const audioId = `audio-${participant.sid}${options.isScreenShareAudio ? '-screen' : ''}`;
            if (!document.getElementById(audioId)) {
                const audioElement = document.createElement('audio');
                audioElement.autoplay = true;
                audioElement.id = audioId;
                track.attach(audioElement);
                document.body.appendChild(audioElement);
            }
        }
    }

    onTrackUnsubscribed(track, participant, options = {}) {
        if (!track || !participant) return;

        if (options.isScreenShare) {
            this.removeScreenShare(participant.sid);
            return;
        }

        if (track.kind === 'video') {
            this.remoteCameraTracks.delete(participant.sid);
            this.detachTrackFromTile(participant.sid);
            if (this.stageParticipantSid === participant.sid && !this.stageForcedByShare) {
                this.stageTrack?.detach(this.elements.stageVideo);
                this.stageTrack = null;
                this.stageParticipantSid = null;
                this.stageIsScreenShare = false;
                this.maybeAutoSelectStage('video-unsubscribed');
            }
        } else if (track.kind === 'audio') {
            const suffix = options.isScreenShareAudio ? '-screen' : '';
            const audioElement = document.getElementById(`audio-${participant.sid}${suffix}`);
            audioElement?.remove();
        }
    }

    onConnectionQualityChanged(participant, quality) {
        const qualityMap = {
            excellent: '优秀',
            good: '良好',
            poor: '较差',
            lost: '断开'
        };

        const localSid = this.getLocalParticipantSid();
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

        const localSid = this.getLocalParticipantSid();
        if (this.elements.localTile) {
            this.elements.localTile.classList.toggle('active-speaker', activeSet.has(localSid));
        }

        if (!this.userPinnedSid && !this.stageForcedByShare) {
            const firstSpeaker = speakers.find(sp => this.remoteCameraTracks.has(sp.sid));
            if (firstSpeaker) {
                this.lastActiveSpeakerSid = firstSpeaker.sid;
                this.setStageToSid(firstSpeaker.sid);
            }
        }
    }

    addParticipant(participant) {
        if (this.remoteParticipants.has(participant.sid)) return;
        const tile = document.createElement('div');
        tile.className = 'video-tile rail-tile';
        tile.id = `participant-${participant.sid}`;
        tile.innerHTML = `
            <video autoplay playsinline muted></video>
            <div class="participant-info">
                <div class="name-badge">
                    <i class="bi bi-mic-fill audio-indicator"></i>
                    <span>${this.escapeHtml(participant.identity)}</span>
                </div>
            </div>
        `;

        tile.addEventListener('click', () => this.handleTileClick(participant.sid));
        this.remoteParticipants.set(participant.sid, tile);
        this.elements.videoGrid?.appendChild(tile);

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

    ensureParticipantTile(participant) {
        if (!participant?.sid) return null;
        if (!this.remoteParticipants.has(participant.sid)) {
            this.addParticipant(participant);
            this.updateRailState();
            this.updateEmptyState();
        }
        return this.remoteParticipants.get(participant.sid);
    }

    handleTileClick(sid) {
        this.userPinnedSid = sid;
        this.stageForcedByShare = null;
        this.setPinnedTile(sid);
        if (!this.setStageToSid(sid, { manual: true })) {
            this.showToast('该成员暂无可显示的视频轨道', 'warning');
        } else {
            const name = this.participantNames.get(sid) || '参会者';
            this.showToast(`已固定 ${name}`, 'info');
        }
    }

    setPinnedTile(sid) {
        this.remoteParticipants.forEach((tile, participantSid) => {
            tile.classList.toggle('pinned', participantSid === sid);
        });
        if (this.elements.localTile) {
            const localSid = this.getLocalParticipantSid();
            const isPinnedLocal = sid === 'local' || sid === localSid;
            this.elements.localTile.classList.toggle('pinned', isPinnedLocal);
        }
    }

    clearUserSpotlight() {
        this.userPinnedSid = null;
        this.setPinnedTile(null);
        this.showToast('已恢复自动跟随模式', 'info');
        this.maybeAutoSelectStage('clear-spotlight');
    }

    previewLocalOnStage() {
        if (!this.localCameraTrack) {
            this.showToast('摄像头未开启', 'warning');
            return;
        }
        this.userPinnedSid = 'local';
        this.stageForcedByShare = null;
        this.setPinnedTile('local');
        this.setStageTrack(this.localCameraTrack, {
            sid: 'local',
            identity: this.elements.localName?.textContent || '我'
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
        const audioElement = document.getElementById(`audio-${sid}`);
        audioElement?.remove();
    }

    attachTrackToTile(sid, track) {
        const tile = this.remoteParticipants.get(sid);
        const video = tile?.querySelector('video');
        if (video) {
            track.attach(video);
        }
    }

    detachTrackFromTile(sid) {
        const tile = this.remoteParticipants.get(sid);
        const video = tile?.querySelector('video');
        if (video) {
            video.srcObject = null;
            video.load();
        }
    }

    attachScreenShareTrack(track, participant) {
        this.remoteShareTracks.set(participant.sid, track);
        this.stageForcedByShare = participant.sid;
        this.setStageTrack(track, participant, { isScreenShare: true });
        this.updateScreenShareIndicator();
        this.showToast(`${participant.identity} 开始共享屏幕`, 'info');
        this.updateEmptyState();
    }

    removeScreenShare(participantSid) {
        this.remoteShareTracks.delete(participantSid);
        if (this.stageForcedByShare === participantSid) {
            this.stageForcedByShare = null;
            this.maybeAutoSelectStage('share-ended');
        }
        this.updateScreenShareIndicator();
        this.updateEmptyState();
        this.showToast('屏幕共享已结束', 'info');
    }

    onLocalScreenShareStarted(track) {
        if (!track) return;
        this.localScreenShareTrack = track;
        this.stageForcedByShare = 'local';
        this.setStageTrack(track, {
            sid: 'local',
            identity: this.elements.localName?.textContent || '我'
        }, { isScreenShare: true });
        this.updateScreenShareIndicator();
        this.showToast('屏幕共享已开启', 'success');
        this.updateEmptyState();
    }

    onLocalScreenShareStopped() {
        this.localScreenShareTrack = null;
        if (this.stageForcedByShare === 'local') {
            this.stageForcedByShare = null;
            this.maybeAutoSelectStage('local-share-stop');
        }
        this.updateScreenShareIndicator();
        this.showToast('屏幕共享已结束', 'info');
        this.updateEmptyState();
    }

    updateParticipantCount() {
        const manager = window.conferenceManager;
        const count = manager ? manager.getParticipantCount() : (this.remoteParticipants.size + 1);
        this.updateRoomInfo(this.elements.roomName?.textContent || '', count);
    }

    updateButtonState(buttonId, active) {
        const button = document.getElementById(buttonId);
        if (button) {
            button.classList.toggle('active', !!active);
        }
    }

    setStageToSid(sid, options = {}) {
        if (!sid) return false;
        if (sid === 'local' || sid === this.getLocalParticipantSid()) {
            if (this.localCameraTrack) {
                this.setStageTrack(this.localCameraTrack, {
                    sid: 'local',
                    identity: this.elements.localName?.textContent || '我'
                }, options);
                return true;
            }
            return false;
        }

        let track = this.remoteCameraTracks.get(sid);
        let isScreenShare = false;
        if (!track && this.remoteShareTracks.has(sid)) {
            track = this.remoteShareTracks.get(sid);
            isScreenShare = true;
        }
        if (!track) return false;

        const participant = {
            sid,
            identity: this.participantNames.get(sid) || '参会者'
        };
        this.setStageTrack(track, participant, { ...options, isScreenShare });
        return true;
    }

    setStageTrack(track, participant, options = {}) {
        if (!this.elements.stageVideo || !track || !participant) return;

        if (this.stageTrack && this.stageTrack !== track) {
            this.stageTrack.detach(this.elements.stageVideo);
        }

        track.attach(this.elements.stageVideo);
        this.stageTrack = track;
        this.stageParticipantSid = participant.sid;
        this.stageIsScreenShare = !!options.isScreenShare;
        this.updateStageLabel(participant.identity, this.stageIsScreenShare);
        this.setPinnedTile(this.userPinnedSid === participant.sid ? participant.sid : this.userPinnedSid);
        this.updateEmptyState();
    }

    updateStageLabel(name, isScreenShare = false) {
        if (!this.elements.stageLabel) return;
        if (!name) {
            this.elements.stageLabel.textContent = '尚未选定';
            return;
        }
        this.elements.stageLabel.textContent = isScreenShare
            ? `${name} 的屏幕`
            : `${name} 的视频`;
    }

    maybeAutoSelectStage(reason = '') {
        if (this.stageForcedByShare) {
            if (this.stageForcedByShare === 'local' && this.localScreenShareTrack) {
                this.setStageTrack(this.localScreenShareTrack, {
                    sid: 'local',
                    identity: this.elements.localName?.textContent || '我'
                }, { isScreenShare: true });
                return;
            }
            const shareTrack = this.remoteShareTracks.get(this.stageForcedByShare);
            if (shareTrack) {
                this.setStageTrack(shareTrack, {
                    sid: this.stageForcedByShare,
                    identity: this.participantNames.get(this.stageForcedByShare) || '参会者'
                }, { isScreenShare: true });
                return;
            }
            this.stageForcedByShare = null;
        }

        if (this.userPinnedSid) {
            if (this.setStageToSid(this.userPinnedSid)) {
                return;
            }
            this.userPinnedSid = null;
        }

        if (this.lastActiveSpeakerSid && this.setStageToSid(this.lastActiveSpeakerSid)) {
            return;
        }

        const fallbackSid = this.findFirstAvailableSid();
        if (fallbackSid) {
            this.setStageToSid(fallbackSid);
        } else if (this.stageTrack) {
            this.stageTrack.detach(this.elements.stageVideo);
            this.stageTrack = null;
            this.stageParticipantSid = null;
            this.stageIsScreenShare = false;
            this.updateStageLabel(null);
            this.updateEmptyState();
        }
    }

    findFirstAvailableSid() {
        if (this.remoteCameraTracks.size > 0) {
            return this.remoteCameraTracks.keys().next().value;
        }
        if (this.remoteShareTracks.size > 0) {
            return this.remoteShareTracks.keys().next().value;
        }
        return null;
    }

    showToast(message, type = 'info') {
        if (!this.elements.toastContainer) {
            console.log(`[${type}] ${message}`);
            return;
        }

        const toast = document.createElement('div');
        toast.className = `toast-message toast-${type}`;
        toast.textContent = message;
        this.elements.toastContainer.appendChild(toast);

        setTimeout(() => {
            toast.classList.add('show');
        }, 50);

        setTimeout(() => {
            toast.classList.remove('show');
            setTimeout(() => toast.remove(), 300);
        }, 4000);
    }

    escapeHtml(text) {
        if (!text) return '';
        const map = {
            '&': '&amp;',
            '<': '&lt;',
            '>': '&gt;',
            '"': '&quot;',
            "'": '&#039;'
        };
        return text.replace(/[&<>"']/g, m => map[m]);
    }
}
