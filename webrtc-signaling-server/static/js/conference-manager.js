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
            window.conferenceUI?.setConnectionState('connected');
            window.conferenceUI?.startCallTimer(this.connectedAt);
            
            // 同步已存在的远程参与者
            this.syncExistingParticipants();
            
            // 启用本地媒体
            await this.enableLocalMedia();
            
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
            console.log('本地轨道已发布:', publication.kind, publication.source);
            const track = publication.track;
            if (!track || !window.conferenceUI) return;

            if (publication.source === Track.Source.CAMERA && publication.kind === 'video') {
                window.conferenceUI.attachLocalVideo(track);
            }

            if (publication.source === Track.Source.SCREEN_SHARE) {
                window.conferenceUI.onLocalScreenShareStarted(track);
            }
        });

        this.room.localParticipant.on(ParticipantEvent.TrackUnpublished, (publication) => {
            if (publication.source === Track.Source.SCREEN_SHARE) {
                window.conferenceUI?.onLocalScreenShareStopped();
            }
        });
        
        this.room.on(RoomEvent.ParticipantConnected, (participant) => {
            console.log('参与者加入:', participant.identity);
            if (window.conferenceUI) {
                window.conferenceUI.onParticipantConnected(participant);
            }
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
            console.log('订阅轨道:', track.kind, 'from', participant.identity, 'source:', publication?.source);
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

        console.log('同步已存在的参与者，数量:', participants.length);

        participants.forEach((participant) => {
            console.log('添加已存在的参与者:', participant.identity);
            window.conferenceUI?.onParticipantConnected(participant);

            participant.videoTracks?.forEach((publication) => {
                if (publication.track) {
                    window.conferenceUI?.onTrackSubscribed(publication.track, participant, {
                        isScreenShare: publication.source === Track.Source.SCREEN_SHARE
                    });
                }
            });

            participant.audioTracks?.forEach((publication) => {
                if (publication.track) {
                    window.conferenceUI?.onTrackSubscribed(publication.track, participant, {
                        isScreenShareAudio: publication.source === Track.Source.SCREEN_SHARE_AUDIO
                    });
                }
            });
        });

        window.conferenceUI?.updateParticipantCount();
    }

    async enableLocalMedia() {
        try {
            await this.room.localParticipant.setCameraEnabled(true);
            await this.room.localParticipant.setMicrophoneEnabled(true);
            
            // 等待轨道发布完成
            await new Promise(resolve => setTimeout(resolve, 500));
            
            // 获取本地视频轨道
            if (this.room.localParticipant.videoTracks && this.room.localParticipant.videoTracks.size > 0) {
                const videoPublication = Array.from(this.room.localParticipant.videoTracks.values())[0];
                const videoTrack = videoPublication?.track;
                if (videoTrack && window.conferenceUI) {
                    window.conferenceUI.attachLocalVideo(videoTrack);
                }
            }
            
            console.log('✅ 本地媒体已启用');
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
