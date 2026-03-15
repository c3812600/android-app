class WebSocketManager {
    constructor(url = 'ws://192.168.18.252:81') {
        this.url = url;
        this.ws = null;
        this.reconnectTimer = null;
        this.init();
    }

    init() {
        try {
            this.updateConnectionStatus('connecting');
            this.ws = new WebSocket(this.url);
            // this.ws.binaryType = 'arraybuffer';
            
            this.ws.onopen = () => {
                console.log('WebSocket 连接已建立');
                this.updateConnectionStatus('connected');
            };

            this.ws.onclose = () => {
                console.log('WebSocket 连接已关闭');
                this.updateConnectionStatus('disconnected');
                // 3秒后尝试重连
                if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
                this.reconnectTimer = setTimeout(() => this.init(), 3000);
            };

            this.ws.onerror = (error) => {
                console.error('WebSocket 错误:', error);
                this.updateConnectionStatus('disconnected');
            };

            this.ws.onmessage = (event) => {
                console.log('收到消息:', event.data);
            };
        } catch (error) {
            console.error('WebSocket 连接失败:', error);
            this.updateConnectionStatus('disconnected');
        }
    }

    updateConnectionStatus(state) {
        const statusElement = document.getElementById('connectionStatus');
        if (statusElement) {
            statusElement.classList.remove('connected', 'disconnected', 'connecting');
            statusElement.classList.add(state);
            statusElement.title = state === 'connected' ? '连接已建立' : state === 'connecting' ? '正在连接' : '连接断开';
        }
    }

    sendMessage(data) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(data);
        } else {
            console.warn('WebSocket 未连接，消息发送失败');
        }
    }

    sendHex(hexString) {
        if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
            console.warn('WebSocket 未连接，HEX 发送失败');
            return;
        }
        const bytes = WebSocketManager.hexToBytes(hexString);
        if (!bytes) {
            console.warn('HEX 格式不正确，发送失败:', hexString);
            return;
        }
        this.ws.send(bytes.buffer);
    }

    static hexToBytes(hexString) {
        if (typeof hexString !== 'string') return null;
        let s = hexString.trim();
        if (s.startsWith('0x') || s.startsWith('0X')) s = s.slice(2);
        s = s.replace(/\s+/g, '');
        if (s.length === 0 || s.length % 2 !== 0) return null;
        if (!/^[0-9a-fA-F]+$/.test(s)) return null;
        const out = new Uint8Array(s.length / 2);
        for (let i = 0; i < s.length; i += 2) {
            out[i / 2] = parseInt(s.slice(i, i + 2), 16);
        }
        return out;
    }
}

function loadJson(url) {
    if (typeof fetch === 'function') {
        return fetch(url, { cache: 'no-cache' }).then(function (r) {
            if (!r.ok) throw new Error('HTTP ' + r.status);
            return r.json();
        });
    }
    return new Promise(function (resolve, reject) {
        var xhr = new XMLHttpRequest();
        xhr.open('GET', url, true);
        xhr.onreadystatechange = function () {
            if (xhr.readyState !== 4) return;
            if (xhr.status >= 200 && xhr.status < 300) {
                try {
                    resolve(JSON.parse(xhr.responseText));
                } catch (e) {
                    reject(e);
                }
            } else {
                reject(new Error('HTTP ' + xhr.status));
            }
        };
        xhr.send();
    });
}

function initVillaWsControls(options) {
    var wsUrl = (options && options.wsUrl) || window.VILLA_WS_URL;
    var configUrl = (options && options.configUrl) || null;

    var manager = new WebSocketManager(wsUrl);
    var inputs = Array.prototype.slice.call(document.querySelectorAll('input[type="checkbox"][data-button-id]'));

    function attachAllSync() {
        var allEl = null;
        var others = [];
        for (var i = 0; i < inputs.length; i++) {
            var el = inputs[i];
            var id = el.getAttribute('data-button-id');
            if (id === 'allLights') allEl = el;
            else others.push(el);
        }
        if (!allEl) return;
        var syncing = false;
        function setChecked(el, val, fire) {
            if (el.checked === val) return;
            el.checked = val;
            if (fire) {
                try {
                    el.dispatchEvent(new Event('change', { bubbles: true }));
                } catch (_) {
                    var evt = document.createEvent('Event');
                    evt.initEvent('change', true, true);
                    el.dispatchEvent(evt);
                }
            }
        }
        allEl.addEventListener('change', function () {
            if (syncing) return;
            syncing = true;
            var val = allEl.checked;
            for (var j = 0; j < others.length; j++) {
                setChecked(others[j], val, false);
            }
            syncing = false;
        });
        for (var k = 0; k < others.length; k++) {
            (function (el) {
                el.addEventListener('change', function () {
                    if (syncing) return;
                    var allOn = true;
                    var allOff = true;
                    for (var m = 0; m < others.length; m++) {
                        if (!others[m].checked) allOn = false;
                        if (others[m].checked) allOff = false;
                    }
                    if (allOn && !allEl.checked) setChecked(allEl, true, false);
                    else if (allOff && allEl.checked) setChecked(allEl, false, false);
                });
            })(others[k]);
        }
    }

    function setEnabled(enabled) {
        for (var i = 0; i < inputs.length; i++) {
            inputs[i].disabled = !enabled;
        }
    }

    setEnabled(false);

    var providedCfg = (options && options.config) || (typeof window !== 'undefined' && window.BUTTON_HEX);
    if (providedCfg && providedCfg.buttons) {
        var mapA = providedCfg.buttons;
        for (var iA = 0; iA < inputs.length; iA++) {
            (function (el) {
                var id = el.getAttribute('data-button-id');
                el.addEventListener('change', function () {
                    var item = mapA[id];
                    if (!item) {
                        console.warn('未找到按钮 HEX 配置:', id);
                        return;
                    }
                    manager.sendMessage(el.checked ? item.on : item.off);
                });
            })(inputs[iA]);
        }
        setEnabled(true);
        attachAllSync();
    } else {
        loadJson(configUrl).then(function (cfg) {
            var map = (cfg && cfg.buttons) || {};
            for (var i = 0; i < inputs.length; i++) {
                (function (el) {
                    var id = el.getAttribute('data-button-id');
                    el.addEventListener('change', function () {
                        var item = map[id];
                        if (!item) {
                            console.warn('未找到按钮 HEX 配置:', id);
                            return;
                        }
                        manager.sendMessage(el.checked ? item.on : item.off);
                    });
                })(inputs[i]);
            }
            setEnabled(true);
            attachAllSync();
        }).catch(function (err) {
            if (typeof window !== 'undefined' && window.BUTTON_HEX && window.BUTTON_HEX.buttons) {
                var mapB = window.BUTTON_HEX.buttons;
                for (var j = 0; j < inputs.length; j++) {
                    (function (el) {
                        var id = el.getAttribute('data-button-id');
                        el.addEventListener('change', function () {
                            var item = mapB[id];
                            if (!item) {
                                console.warn('未找到按钮 HEX 配置:', id);
                                return;
                            }
                            manager.sendMessage(el.checked ? item.on : item.off);
                        });
                    })(inputs[j]);
                }
                setEnabled(true);
                attachAllSync();
            } else {
                console.error('加载按钮 HEX 配置失败:', err);
                setEnabled(false);
            }
        });
    }

    return manager;
}

window.WebSocketManager = WebSocketManager;
window.VillaControl = window.VillaControl || {};
window.VillaControl.initWsControls = initVillaWsControls;
