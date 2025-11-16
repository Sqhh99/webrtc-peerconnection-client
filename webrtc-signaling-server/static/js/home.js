// 主页逻辑
class HomePage {
    constructor() {
        this.form = document.getElementById('joinForm');
        this.userNameInput = document.getElementById('userName');
        this.roomNameInput = document.getElementById('roomName');
        this.joinBtn = document.getElementById('joinBtn');
        this.quickJoinBtn = document.getElementById('quickJoinBtn');
        this.createRoomBtn = document.getElementById('createRoomBtn');
        this.loading = document.querySelector('.loading');
        this.alert = document.getElementById('alert');

        this.init();
    }

    init() {
        // 从URL参数中获取房间名
        const urlParams = new URLSearchParams(window.location.search);
        const roomFromUrl = urlParams.get('room');
        if (roomFromUrl) {
            this.roomNameInput.value = roomFromUrl;
        }

        // 从本地存储中恢复用户名
        const savedName = localStorage.getItem('userName');
        if (savedName) {
            this.userNameInput.value = savedName;
        }

        this.setupEventListeners();
    }

    setupEventListeners() {
        this.form.addEventListener('submit', (e) => this.handleJoin(e));
        this.quickJoinBtn.addEventListener('click', () => this.handleQuickJoin());
        this.createRoomBtn.addEventListener('click', () => this.handleCreateRoom());
    }

    async handleJoin(e) {
        e.preventDefault();

        const userName = this.userNameInput.value.trim();
        const roomName = this.roomNameInput.value.trim();

        if (!userName || !roomName) {
            this.showAlert('请填写所有字段', 'error');
            return;
        }

        // 保存用户名到本地存储
        localStorage.setItem('userName', userName);

        this.showLoading('正在获取令牌...');

        try {
            const response = await fetch('/api/token', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    roomName: roomName,
                    participantName: userName
                })
            });

            if (!response.ok) {
                throw new Error('获取令牌失败');
            }

            const data = await response.json();
            
            // 将令牌和URL存储到sessionStorage
            sessionStorage.setItem('livekit_token', data.token);
            sessionStorage.setItem('livekit_url', data.url);
            sessionStorage.setItem('room_name', data.roomName);
            sessionStorage.setItem('participant_name', userName);

            this.showLoading('正在进入会议...');

            // 跳转到会议页面
            setTimeout(() => {
                window.location.href = 'conference.html';
            }, 500);

        } catch (error) {
            console.error('加入会议失败:', error);
            this.showAlert('加入会议失败: ' + error.message, 'error');
            this.hideLoading();
        }
    }

    handleQuickJoin() {
        // 快速加入 - 生成随机房间名和用户名
        const randomRoom = 'room-' + Math.random().toString(36).substr(2, 6);
        const randomUser = 'user-' + Math.random().toString(36).substr(2, 6);
        
        this.roomNameInput.value = randomRoom;
        this.userNameInput.value = randomUser;
        
        // 自动提交表单
        this.form.dispatchEvent(new Event('submit'));
    }

    async handleCreateRoom() {
        const userName = this.userNameInput.value.trim() || 'user-' + Math.random().toString(36).substr(2, 6);
        
        this.showLoading('正在创建会议室...');

        try {
            // 生成唯一的房间名
            const roomName = 'room-' + Date.now();

            // 创建房间
            const createResponse = await fetch('/api/rooms', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    name: roomName
                })
            });

            if (!createResponse.ok) {
                throw new Error('创建房间失败');
            }

            // 获取令牌并加入
            this.userNameInput.value = userName;
            this.roomNameInput.value = roomName;
            localStorage.setItem('userName', userName);

            const tokenResponse = await fetch('/api/token', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    roomName: roomName,
                    participantName: userName
                })
            });

            if (!tokenResponse.ok) {
                throw new Error('获取令牌失败');
            }

            const data = await tokenResponse.json();
            
            sessionStorage.setItem('livekit_token', data.token);
            sessionStorage.setItem('livekit_url', data.url);
            sessionStorage.setItem('room_name', data.roomName);
            sessionStorage.setItem('participant_name', userName);

            this.showLoading('正在进入会议...');

            setTimeout(() => {
                window.location.href = 'conference.html';
            }, 500);

        } catch (error) {
            console.error('创建会议失败:', error);
            this.showAlert('创建会议失败: ' + error.message, 'error');
            this.hideLoading();
        }
    }

    showLoading(text = '正在连接...') {
        this.loading.classList.add('show');
        document.getElementById('loadingText').textContent = text;
        this.joinBtn.disabled = true;
        this.quickJoinBtn.disabled = true;
        this.createRoomBtn.disabled = true;
    }

    hideLoading() {
        this.loading.classList.remove('show');
        this.joinBtn.disabled = false;
        this.quickJoinBtn.disabled = false;
        this.createRoomBtn.disabled = false;
    }

    showAlert(message, type = 'error') {
        this.alert.textContent = message;
        this.alert.className = `alert ${type} show`;
        
        setTimeout(() => {
            this.alert.classList.remove('show');
        }, 5000);
    }
}

// 页面加载完成后初始化
document.addEventListener('DOMContentLoaded', () => {
    new HomePage();
});
