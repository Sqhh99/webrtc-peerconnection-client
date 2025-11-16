// 会议应用主入口
(async function() {
    'use strict';

    console.log('🚀 启动会议应用 v2.0.1');
    console.log('📅 加载时间:', new Date().toISOString());

    // 等待 LiveKit SDK 加载
    let retries = 0;
    while (!window.LivekitClient && retries < 50) {
        console.log('等待 LiveKit SDK 加载...');
        await new Promise(resolve => setTimeout(resolve, 100));
        retries++;
    }

    if (!window.LivekitClient) {
        alert('LiveKit SDK 加载失败，请刷新页面重试');
        return;
    }

    console.log('✅ LiveKit SDK 已加载');

    // 创建管理器和UI实例
    const manager = new ConferenceManager();
    const ui = new ConferenceUI();
    
    // 暴露到全局供manager使用
    window.conferenceUI = ui;
    window.conferenceManager = manager;

    // 初始化
    try {
        ui.showLoading('正在初始化...');
        
        await manager.initialize();
        
        ui.showLoading('正在连接会议...');
        await manager.connect();
        
        ui.hideLoading();
        ui.setLocalParticipantName(manager.userName);
        ui.updateRoomInfo(manager.roomName, manager.getParticipantCount());
        
        console.log('✅ 会议初始化完成');
        
    } catch (error) {
        console.error('❌ 初始化失败:', error);
        ui.hideLoading();
        alert('加入会议失败: ' + error.message);
        setTimeout(() => {
            window.location.href = '/';
        }, 2000);
        return;
    }

    // 设置控制按钮事件
    const micBtn = document.getElementById('micBtn');
    const cameraBtn = document.getElementById('cameraBtn');
    const shareScreenBtn = document.getElementById('shareScreenBtn');
    const leaveBtn = document.getElementById('leaveBtn');

    if (micBtn) {
        micBtn.addEventListener('click', async () => {
            try {
                const enabled = await manager.toggleMicrophone();
                ui.updateButtonState('micBtn', enabled);
                micBtn.querySelector('i').className = enabled ? 'bi bi-mic-fill' : 'bi bi-mic-mute-fill';
            } catch (error) {
                console.error('切换麦克风失败:', error);
            }
        });
    }

    if (cameraBtn) {
        cameraBtn.addEventListener('click', async () => {
            try {
                const enabled = await manager.toggleCamera();
                ui.updateButtonState('cameraBtn', enabled);
                cameraBtn.querySelector('i').className = enabled ? 'bi bi-camera-video-fill' : 'bi bi-camera-video-off-fill';
            } catch (error) {
                console.error('切换摄像头失败:', error);
            }
        });
    }

    if (shareScreenBtn) {
        shareScreenBtn.addEventListener('click', async () => {
            try {
                const sharing = await manager.toggleScreenShare();
                ui.updateButtonState('shareScreenBtn', sharing);
                shareScreenBtn.querySelector('i').className = sharing ? 'bi bi-stop-circle-fill' : 'bi bi-display';
            } catch (error) {
                console.error('屏幕共享失败:', error);
            }
        });
    }

    if (leaveBtn) {
        leaveBtn.addEventListener('click', async () => {
            if (confirm('确定要离开会议吗？')) {
                ui.showLoading('正在离开会议...');
                await manager.disconnect();
                window.location.href = '/';
            }
        });
    }

    // 页面关闭前清理
    window.addEventListener('beforeunload', () => {
        if (manager.room) {
            manager.disconnect();
        }
    });

    console.log('✅ 会议应用启动完成');
})();
