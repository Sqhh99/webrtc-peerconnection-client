// LiveKit 会议管理器
class ConferenceManager {
    constructor() {
        this.room = null;
        this.token = null;
        this.wsUrl = null;
        this.roomName = null;
        this.userName = null;
        this.localTracks = new Map();
        this.remoteTracks = new Map();
        this.isScreenSharing = false;
        this.connectedAt = null;
    }

    async initialize() {
        // 从 sessionStorage 获取信息
        this.token = sessionStorage.getItem('livekit_token');
        this.wsUrl = sessionStorage.getItem('livekit_url');
        this.roomName = sessionStorage.getItem('room_name');
        this.userName = sessionStorage.getItem('participant_name');

        if (!this.token || !this.wsUrl) {
            throw new Error('缺少会议信息');
        }

        console.log('初始化会议管理器:', { roomName: this.roomName, userName: this.userName });
    }

    async connect() {
        try {
            const { Room, RoomEvent } = window.LivekitClient;
            
            this.room = new Room({
                adaptiveStream: true,
                dynacast: true,
                videoCaptureDefaults: {
                    resolution: {
                        width: 1280,
                        height: 720,
                        frameRate: 30
                    }
                }
            });

            // 设置事件监听
            this.setupEventListeners();

            window.conferenceUI?.setConnectionState('connecting');

            // 连接到房间
            await this.room.connect(this.wsUrl, this.token);
            this.connectedAt = Date.now();
            
            console.log('✅ 成功连接到房间');
            console.log('📊 房间信息:', {
                name: this.room.name,
                sid: this.room.sid,
                remoteParticipants: this.room.remoteParticipants?.size || 0
            });
            
            window.conferenceUI?.setConnectionState('connected');
            window.conferenceUI?.startCallTimer(this.connectedAt);
            
            // 启用本地媒体（TrackPublished 事件会处理视频附加）
            await this.enableLocalMedia();
            
            // 稍微等待一下，让本地轨道有时间发布
            await new Promise(resolve => setTimeout(resolve, 300));
            
            // 同步已存在的远程参与者
            this.syncExistingParticipants();
            
            return true;
        } catch (error) {
            console.error('❌ 连接失败:', error);
            throw error;
        }
    }

    setupEventListeners() {
        const { RoomEvent, ParticipantEvent, Track } = window.LivekitClient;
        
        // 本地轨道发布事件
        this.room.localParticipant.on(ParticipantEvent.TrackPublished, (publication) => {
            console.log('📢 本地轨道已发布:', publication.kind, publication.source);
            const track = publication.track;
            if (!track || !window.conferenceUI) {
                console.warn('  ⚠️ 轨道或UI不存在');
                return;
            }

            if (publication.source === Track.Source.CAMERA && publication.kind === 'video') {
                console.log('  → 附加本地摄像头视频');
                window.conferenceUI.attachLocalVideo(track);
            }

            if (publication.source === Track.Source.SCREEN_SHARE) {
                console.log('  → 附加本地屏幕共享');
                window.conferenceUI.onLocalScreenShareStarted(track);
            }
        });

        this.room.localParticipant.on(ParticipantEvent.TrackUnpublished, (publication) => {
            if (publication.source === Track.Source.SCREEN_SHARE) {
                window.conferenceUI?.onLocalScreenShareStopped();
            }
        });
        
        this.room.on(RoomEvent.ParticipantConnected, (participant) => {
            console.log('👤 ParticipantConnected 事件触发:', participant.identity, 'SID:', participant.sid);
            if (window.conferenceUI) {
                window.conferenceUI.onParticipantConnected(participant);
            }
            
            // 监听参与者的轨道发布事件
            participant.on('trackPublished', (publication) => {
                console.log('📢 远程参与者发布了新轨道:', participant.identity, publication.kind, publication.source);
                // TrackSubscribed 事件会自动处理
            });
        });

        this.room.on(RoomEvent.ParticipantDisconnected, (participant) => {
            console.log('参与者离开:', participant.identity);
            if (window.conferenceUI) {
                window.conferenceUI.onParticipantDisconnected(participant);
            }
        });

        this.room.on(RoomEvent.TrackSubscribed, (track, publication, participant) => {
            const isScreenShare = publication?.source === Track.Source.SCREEN_SHARE;
            const isScreenShareAudio = publication?.source === Track.Source.SCREEN_SHARE_AUDIO;
            console.log('🎬 TrackSubscribed 事件触发:', {
                participant: participant.identity,
                kind: track.kind,
                source: publication?.source,
                isScreenShare,
                isScreenShareAudio,
                trackSid: publication?.trackSid
            });
            if (window.conferenceUI) {
                window.conferenceUI.onTrackSubscribed(track, participant, {
                    isScreenShare,
                    isScreenShareAudio
                });
            }
        });

        this.room.on(RoomEvent.TrackUnsubscribed, (track, publication, participant) => {
            const isScreenShare = publication?.source === Track.Source.SCREEN_SHARE;
            const isScreenShareAudio = publication?.source === Track.Source.SCREEN_SHARE_AUDIO;
            console.log('取消订阅:', track.kind, 'from', participant.identity, 'source:', publication?.source);
            if (window.conferenceUI) {
                window.conferenceUI.onTrackUnsubscribed(track, participant, {
                    isScreenShare,
                    isScreenShareAudio
                });
            }
        });

        this.room.on(RoomEvent.DataReceived, (payload, participant) => {
            try {
                const decoder = new TextDecoder();
                const message = JSON.parse(decoder.decode(payload));
                if (window.conferenceUI) {
                    window.conferenceUI.onChatMessage(message, participant);
                }
            } catch (error) {
                console.error('解析聊天消息失败:', error);
            }
        });

        this.room.on(RoomEvent.Disconnected, () => {
            console.log('已断开连接');
            window.conferenceUI?.setConnectionState('disconnected');
            setTimeout(() => {
                window.location.href = '/';
            }, 2000);
        });

        this.room.on(RoomEvent.ConnectionQualityChanged, (participant, quality) => {
            if (window.conferenceUI) {
                window.conferenceUI.onConnectionQualityChanged(participant, quality);
            }
        });

        this.room.on(RoomEvent.ActiveSpeakersChanged, (speakers) => {
            window.conferenceUI?.onActiveSpeakersChanged(speakers);
        });

        this.room.on(RoomEvent.ConnectionStateChanged, (state) => {
            window.conferenceUI?.setConnectionState(state.toLowerCase());
        });
    }

    syncExistingParticipants() {
        const { Track } = window.LivekitClient;
        const participants = this.getRemoteParticipantList();

        console.log('\n🔄 === 开始同步已存在的参与者 ===');
        console.log('📊 远程参与者数量:', participants.length);

        if (participants.length === 0) {
            console.log('✅ 没有已存在的参与者，显示空状态');
            // 只有自己时才显示空状态
            window.conferenceUI?.updateEmptyState();
            console.log('=== 同步完成 ===\n');
            return;
        }

        console.log(`📢 发现 ${participants.length} 个已存在的参与者，准备同步`);

        // 第一步：为所有参与者创建UI
        participants.forEach((participant, index) => {
            console.log(`\n👤 [${index + 1}/${participants.length}] 处理参与者:`, participant.identity);
            console.log('  SID:', participant.sid);
            console.log('  视频轨道数:', participant.videoTracks?.size || 0);
            console.log('  音频轨道数:', participant.audioTracks?.size || 0);
            
            // 创建UI（这会将参与者添加到 remoteParticipants Map）
            window.conferenceUI?.onParticipantConnected(participant);

            // 监听后续轨道发布
            participant.on('trackPublished', (publication) => {
                console.log('📢 参与者发布新轨道:', participant.identity, publication.kind);
            });
        });

        // 第二步：更新计数和状态（此时 remoteParticipants.size 应该 > 0）
        console.log('\n📊 更新UI状态');
        window.conferenceUI?.updateParticipantCount();
        window.conferenceUI?.updateEmptyState(); // 应该隐藏空状态

        // 第三步：附加已有的轨道
        console.log('\n🎬 === 开始附加已有轨道 ===');
        participants.forEach((participant) => {
            // 处理视频轨道
            if (participant.videoTracks && participant.videoTracks.size > 0) {
                participant.videoTracks.forEach((publication) => {
                    const track = publication.track;
                    if (track) {
                        const isScreenShare = publication.source === Track.Source.SCREEN_SHARE;
                        console.log(`  🎥 附加 ${participant.identity} 的${isScreenShare ? '屏幕共享' : '视频'}`);
                        window.conferenceUI?.onTrackSubscribed(track, participant, { isScreenShare });
                    } else {
                        console.log(`  ⏳ ${participant.identity} 的视频轨道未就绪，等待 TrackSubscribed 事件`);
                    }
                });
            }

            // 处理音频轨道
            if (participant.audioTracks && participant.audioTracks.size > 0) {
                participant.audioTracks.forEach((publication) => {
                    const track = publication.track;
                    if (track) {
                        const isScreenShareAudio = publication.source === Track.Source.SCREEN_SHARE_AUDIO;
                        console.log(`  🔊 附加 ${participant.identity} 的音频`);
                        window.conferenceUI?.onTrackSubscribed(track, participant, { isScreenShareAudio });
                    } else {
                        console.log(`  ⏳ ${participant.identity} 的音频轨道未就绪，等待 TrackSubscribed 事件`);
                    }
                });
            }
        });

        console.log('✅ === 同步完成 ===\n');
    }

    async enableLocalMedia() {
        try {
            console.log('🎥 启用本地媒体...');
            
            // 启用摄像头和麦克风（TrackPublished 事件会自动处理附加）
            await this.room.localParticipant.setCameraEnabled(true);
            await this.room.localParticipant.setMicrophoneEnabled(true);
            
            console.log('✅ 本地媒体已启用（等待轨道发布事件）');
        } catch (error) {
            console.error('❌ 启用媒体失败:', error);
            throw error;
        }
    }

    async toggleMicrophone() {
        if (!this.room) return false;
        const enabled = this.room.localParticipant.isMicrophoneEnabled;
        await this.room.localParticipant.setMicrophoneEnabled(!enabled);
        return !enabled;
    }

    async toggleCamera() {
        if (!this.room) return false;
        const enabled = this.room.localParticipant.isCameraEnabled;
        await this.room.localParticipant.setCameraEnabled(!enabled);
        return !enabled;
    }

    async toggleScreenShare() {
        if (!this.room) return false;
        
        try {
            if (this.isScreenSharing) {
                await this.room.localParticipant.setScreenShareEnabled(false);
                this.isScreenSharing = false;
            } else {
                await this.room.localParticipant.setScreenShareEnabled(true);
                this.isScreenSharing = true;
            }
            return this.isScreenSharing;
        } catch (error) {
            console.error('屏幕共享失败:', error);
            throw error;
        }
    }

    async sendChatMessage(message) {
        if (!this.room) return;
        
        const data = {
            type: 'chat',
            message: message,
            timestamp: Date.now(),
            sender: this.userName
        };
        
        const encoder = new TextEncoder();
        const payload = encoder.encode(JSON.stringify(data));
        await this.room.localParticipant.publishData(payload, { reliable: true });
    }

    async disconnect() {
        if (this.room) {
            await this.room.disconnect();
            this.room = null;
        }
        window.conferenceUI?.stopCallTimer?.();
    }

    getParticipantCount() {
        return this.getRemoteParticipantList().length + 1;
    }

    getRemoteParticipants() {
        return this.getRemoteParticipantList();
    }

    getRemoteParticipantList() {
        if (!this.room) return [];
        const participantMap = this.room.remoteParticipants || this.room.participants;
        if (!participantMap) return [];

        if (participantMap instanceof Map) {
            return Array.from(participantMap.values());
        }

        if (typeof participantMap.forEach === 'function') {
            const list = [];
            participantMap.forEach((p) => list.push(p));
            return list;
        }

        return Array.isArray(participantMap) ? participantMap : Object.values(participantMap);
    }
}
