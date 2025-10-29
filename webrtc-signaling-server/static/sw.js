// Service Worker for WebRTC Video Call Platform
const CACHE_NAME = 'webrtc-call-v1.0.0';
const STATIC_RESOURCES = [
    '/',
    '/index.html',
    '/css/style.css',
    '/js/app.js',
    '/js/ui-manager.js',
    '/js/webrtc-manager.js',
    '/js/media-manager.js',
    '/js/call-manager.js',
    '/js/signaling-manager.js',
    '/manifest.json'
];

// 安装 Service Worker
self.addEventListener('install', event => {
    console.log('[SW] 安装中...');
    
    event.waitUntil(
        caches.open(CACHE_NAME)
            .then(cache => {
                console.log('[SW] 缓存静态资源');
                return cache.addAll(STATIC_RESOURCES);
            })
            .then(() => {
                console.log('[SW] 安装完成');
                return self.skipWaiting();
            })
            .catch(error => {
                console.error('[SW] 安装失败:', error);
            })
    );
});

// 激活 Service Worker
self.addEventListener('activate', event => {
    console.log('[SW] 激活中...');
    
    event.waitUntil(
        caches.keys()
            .then(cacheNames => {
                return Promise.all(
                    cacheNames
                        .filter(cacheName => cacheName !== CACHE_NAME)
                        .map(cacheName => {
                            console.log('[SW] 删除旧缓存:', cacheName);
                            return caches.delete(cacheName);
                        })
                );
            })
            .then(() => {
                console.log('[SW] 激活完成');
                return self.clients.claim();
            })
            .catch(error => {
                console.error('[SW] 激活失败:', error);
            })
    );
});

// 拦截网络请求
self.addEventListener('fetch', event => {
    const { request } = event;
    const url = new URL(request.url);
    
    // 只处理同源请求
    if (url.origin !== location.origin) {
        return;
    }
    
    // 跳过WebSocket和API请求
    if (url.pathname.startsWith('/ws') || url.pathname.startsWith('/api')) {
        return;
    }
    
    event.respondWith(
        caches.match(request)
            .then(response => {
                // 缓存命中，返回缓存版本
                if (response) {
                    console.log('[SW] 缓存命中:', request.url);
                    return response;
                }
                
                // 缓存未命中，从网络获取
                console.log('[SW] 网络请求:', request.url);
                return fetch(request)
                    .then(response => {
                        // 检查响应有效性
                        if (!response || response.status !== 200 || response.type !== 'basic') {
                            return response;
                        }
                        
                        // 克隆响应用于缓存
                        const responseToCache = response.clone();
                        
                        // 缓存静态资源
                        if (shouldCache(request)) {
                            caches.open(CACHE_NAME)
                                .then(cache => {
                                    console.log('[SW] 缓存新资源:', request.url);
                                    cache.put(request, responseToCache);
                                });
                        }
                        
                        return response;
                    })
                    .catch(error => {
                        console.log('[SW] 网络请求失败:', request.url, error);
                        
                        // 网络失败时尝试返回缓存的首页
                        if (request.mode === 'navigate') {
                            return caches.match('/index.html');
                        }
                        
                        throw error;
                    });
            })
    );
});

// 判断是否应该缓存请求
function shouldCache(request) {
    const url = new URL(request.url);
    
    // 缓存静态资源
    if (url.pathname.endsWith('.html') ||
        url.pathname.endsWith('.css') ||
        url.pathname.endsWith('.js') ||
        url.pathname.endsWith('.json') ||
        url.pathname.endsWith('.png') ||
        url.pathname.endsWith('.jpg') ||
        url.pathname.endsWith('.svg') ||
        url.pathname.endsWith('.ico')) {
        return true;
    }
    
    // 缓存根路径
    if (url.pathname === '/') {
        return true;
    }
    
    return false;
}

// 处理消息
self.addEventListener('message', event => {
    const { type, data } = event.data;
    
    switch (type) {
        case 'GET_VERSION':
            event.ports[0].postMessage({
                version: CACHE_NAME,
                cached: STATIC_RESOURCES.length
            });
            break;
            
        case 'SKIP_WAITING':
            self.skipWaiting();
            break;
            
        case 'CLEAR_CACHE':
            caches.delete(CACHE_NAME)
                .then(() => {
                    event.ports[0].postMessage({ success: true });
                })
                .catch(error => {
                    event.ports[0].postMessage({ success: false, error: error.message });
                });
            break;
            
        default:
            console.log('[SW] 未知消息类型:', type);
    }
});

// 处理推送通知（如果需要）
self.addEventListener('push', event => {
    console.log('[SW] 推送消息接收:', event);
    
    const options = {
        body: event.data ? event.data.text() : '您有新的通话请求',
        icon: '/manifest-icon-192.png',
        badge: '/manifest-icon-192.png',
        vibrate: [100, 50, 100],
        data: {
            dateOfArrival: Date.now(),
            primaryKey: '2'
        },
        actions: [
            {
                action: 'accept',
                title: '接听',
                icon: '/accept-icon.png'
            },
            {
                action: 'decline',
                title: '拒绝',
                icon: '/decline-icon.png'
            }
        ]
    };
    
    event.waitUntil(
        self.registration.showNotification('视频通话', options)
    );
});

// 处理通知点击
self.addEventListener('notificationclick', event => {
    console.log('[SW] 通知点击:', event);
    
    event.notification.close();
    
    if (event.action === 'accept') {
        // 接听通话
        event.waitUntil(
            clients.openWindow('/?action=accept&id=' + event.notification.data.primaryKey)
        );
    } else if (event.action === 'decline') {
        // 拒绝通话
        event.waitUntil(
            clients.openWindow('/?action=decline&id=' + event.notification.data.primaryKey)
        );
    } else {
        // 默认行为：打开应用
        event.waitUntil(
            clients.openWindow('/')
        );
    }
});

// 后台同步（如果支持）
self.addEventListener('sync', event => {
    console.log('[SW] 后台同步:', event.tag);
    
    if (event.tag === 'background-sync') {
        event.waitUntil(
            // 这里可以处理离线时错过的通话等
            doBackgroundSync()
        );
    }
});

async function doBackgroundSync() {
    try {
        // 实现后台同步逻辑
        console.log('[SW] 执行后台同步');
    } catch (error) {
        console.error('[SW] 后台同步失败:', error);
    }
}
