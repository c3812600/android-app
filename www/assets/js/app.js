const state = {
  dualItems: [
    { id: 'zhuzhai', label: '住宅', icon: 'building', current: null },
    { id: 'huisuo', label: '会所', icon: 'glass-water', current: null },
    { id: 'bangong', label: '办公', icon: 'briefcase', current: null },
    { id: 'shicai', label: '石材柱灯槽', icon: 'columns', current: null },
  ],
  toggleItems: [
    { id: 'qunfang', label: '裙房', icon: 'store', isOn: false },
    { id: 'taguan', label: '塔冠', icon: 'crown', isOn: false },
    { id: 'shouceng', label: '首层景观', icon: 'trees', isOn: false },
  ]
};

const controlGrid = document.getElementById('control-grid');

function initialRender() {
  controlGrid.innerHTML = `
    <div class="lg:col-span-3 mb-4" style="animation: fadeInUp 0.5s ease-out 0s forwards; opacity: 0;">
      <div class="flex items-center justify-between bg-white/20 p-6 rounded-3xl border border-white/30">
        <div class="flex items-center gap-4">
          <div id="master-icon" class="icon-wrapper p-4 rounded-2xl">
            <i data-lucide="power" class="w-8 h-8"></i>
          </div>
          <div>
            <h2 class="text-2xl font-bold text-gray-900">总控 (Master)</h2>
            <p class="text-sm text-gray-600">控制所有照明系统</p>
          </div>
        </div>
        
        <div class="flex items-center gap-3">
          <button onclick="turnAllOn()" class="px-6 py-2.5 bg-[#2096f3] hover:bg-blue-600 text-white rounded-xl font-bold shadow-md transition-all active:scale-95 flex items-center gap-2 border border-blue-400">
            <i data-lucide="sun" class="w-5 h-5"></i> 全亮
          </button>
          <button onclick="turnAllOff()" class="px-6 py-2.5 bg-gray-500 hover:bg-gray-600 text-white rounded-xl font-bold shadow-md transition-all active:scale-95 flex items-center gap-2 border border-gray-400">
            <i data-lucide="moon" class="w-5 h-5"></i> 全暗
          </button>
        </div>
      </div>
    </div>
  `;

  state.dualItems.forEach((item, index) => {
    const delay = 0.1 + index * 0.05;
    const html = `
      <div class="glass-card p-6 flex items-center justify-between transition-all duration-300 hover:bg-white/40 group" 
           style="animation: fadeInUp 0.5s ease-out ${delay}s forwards; opacity: 0;">
        <div class="flex items-center gap-4">
          <div id="icon-${item.id}" class="icon-wrapper p-3 rounded-2xl">
            <i data-lucide="${item.icon}"></i>
          </div>
          <span class="text-lg font-semibold text-gray-800 tracking-tight">${item.label}</span>
        </div>
        
        <div id="switch-container-${item.id}" onclick="toggleDual('${item.id}')" 
             class="relative w-20 h-10 rounded-full cursor-pointer p-1 shadow-inner overflow-hidden border transition-colors duration-300">
          <div id="switch-text-${item.id}" class="switch-knob w-8 h-8 bg-white rounded-full shadow-md flex items-center justify-center text-[10px] font-bold text-gray-600 z-10 relative">
            静态
          </div>
          <div class="absolute inset-0 flex justify-between items-center px-2 text-[10px] font-bold text-white pointer-events-none">
            <span>静态</span>
            <span>动态</span>
          </div>
        </div>
      </div>
    `;
    controlGrid.insertAdjacentHTML('beforeend', html);
  });

  state.toggleItems.forEach((item, index) => {
    const delay = 0.1 + (state.dualItems.length + index) * 0.05;
    const html = `
      <div class="glass-card p-6 flex items-center justify-center cursor-pointer transition-all duration-300 hover:bg-white/40 group active:scale-95" 
           style="animation: fadeInUp 0.5s ease-out ${delay}s forwards; opacity: 0;"
           onclick="toggleSingle('${item.id}')"
           id="card-${item.id}">
        <div class="flex items-center gap-4">
          <div id="icon-${item.id}" class="icon-wrapper p-3 rounded-2xl">
            <i data-lucide="${item.icon}"></i>
          </div>
          <span id="text-${item.id}" class="text-lg font-semibold tracking-tight transition-colors duration-300">${item.label}</span>
        </div>
      </div>
    `;
    controlGrid.insertAdjacentHTML('beforeend', html);
  });

  lucide.createIcons();
  updateUI();
}

function updateUI() {
  const isAllOn = state.dualItems.every(item => item.current !== null) &&
                  state.toggleItems.every(item => item.isOn === true);

  const masterIcon = document.getElementById('master-icon');

  if (isAllOn) {
    masterIcon.className = 'icon-wrapper p-4 rounded-2xl bg-blue-500 text-white shadow-lg shadow-blue-500/30';
  } else {
    masterIcon.className = 'icon-wrapper p-4 rounded-2xl bg-white/40 text-gray-600';
  }

  state.dualItems.forEach(item => {
    const iconEl = document.getElementById(`icon-${item.id}`);
    const switchContainer = document.getElementById(`switch-container-${item.id}`);
    const switchText = document.getElementById(`switch-text-${item.id}`);

    if (item.current === 'dynamic') {
      iconEl.className = 'icon-wrapper p-3 rounded-2xl bg-green-500/20 text-green-600';
      switchContainer.className = 'relative w-20 h-10 rounded-full cursor-pointer p-1 shadow-inner overflow-hidden border transition-colors duration-300 bg-[#4cb050] border-[#43a047] switch-dynamic';
      switchText.innerText = '动态';
    } else if (item.current === 'static') {
      iconEl.className = 'icon-wrapper p-3 rounded-2xl bg-blue-500/20 text-blue-600';
      switchContainer.className = 'relative w-20 h-10 rounded-full cursor-pointer p-1 shadow-inner overflow-hidden border transition-colors duration-300 bg-[#2096f3] border-[#1e88e5]';
      switchText.innerText = '静态';
    } else {
      iconEl.className = 'icon-wrapper p-3 rounded-2xl bg-white/40 text-gray-600';
      switchContainer.className = 'relative w-20 h-10 rounded-full cursor-pointer p-1 shadow-inner overflow-hidden border transition-colors duration-300 bg-[#bcbcbc] border-[#a6a6a6]';
      switchText.innerText = '关';
    }
  });

  state.toggleItems.forEach(item => {
    const iconEl = document.getElementById(`icon-${item.id}`);
    const textEl = document.getElementById(`text-${item.id}`);

    if (item.isOn) {
      iconEl.className = 'icon-wrapper p-3 rounded-2xl bg-green-500/20 text-green-600';
      textEl.className = 'text-lg font-semibold tracking-tight transition-colors duration-300 text-green-600';
    } else {
      iconEl.className = 'icon-wrapper p-3 rounded-2xl bg-white/40 text-gray-600';
      textEl.className = 'text-lg font-semibold tracking-tight transition-colors duration-300 text-gray-800';
    }
  });
}

let wsManager = null;

function sendCommand(id, action) {
  if (wsManager && window.BUTTON_HEX && window.BUTTON_HEX.buttons) {
    const itemHex = window.BUTTON_HEX.buttons[id];
    if (itemHex && itemHex[action]) {
      console.log(`发送指令: ${id} -> ${action} (${itemHex[action]})`);
      wsManager.sendHex(itemHex[action]);
    } else {
      console.warn(`未找到按钮 ${id} 的 ${action} HEX 配置`);
    }
  }
}

window.toggleDual = function(id) {
  const item = state.dualItems.find(i => i.id === id);
  if (item) {
    if (item.current === null) {
      item.current = 'static';
      sendCommand(item.id, 'static');
    } else {
      item.current = item.current === 'static' ? 'dynamic' : 'static';
      sendCommand(item.id, item.current);
    }
    updateUI();
  }
};

window.toggleSingle = function(id) {
  const item = state.toggleItems.find(i => i.id === id);
  if (item) {
    // 强制设置为开，不用关的逻辑
    item.isOn = true;
    sendCommand(item.id, 'on');
    updateUI();
  }
};

window.turnAllOn = function() {
  state.dualItems.forEach(item => { item.current = 'static'; });
  state.toggleItems.forEach(item => { item.isOn = true; });
  sendCommand('master', 'on');
  updateUI();
};

window.turnAllOff = function() {
  state.dualItems.forEach(item => { item.current = null; });
  state.toggleItems.forEach(item => { item.isOn = false; });
  sendCommand('master', 'off');
  updateUI();
};

initialRender();
wsManager = new window.WebSocketManager();

const dateOptions = { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric' };
document.getElementById('current-date').innerText = new Date().toLocaleDateString('zh-CN', dateOptions);
