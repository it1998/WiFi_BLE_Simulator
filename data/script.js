const popup = document.getElementById('popup');
const popupText = document.getElementById('message');
const form = document.getElementById('info_form');
const wifiToggle = document.getElementById('wifi_toggle');
const bleToggle  = document.getElementById('ble_toggle');
const wifiSection = document.getElementById('wifi_section');
const bleSection  = document.getElementById('ble_section');
const ssidInput = document.getElementById('ssid');
const macInput  = document.getElementById('mac');
const bleMacInput = document.getElementById('ble_mac');
const bleRawInput = document.getElementById('ble_raw');

// 统一的区域显隐切换
function toggleSection(type) {
    if (type === 'wifi') {
        wifiSection.classList.toggle('active', wifiToggle.checked);
    } else if (type === 'ble') {
        bleSection.classList.toggle('active', bleToggle.checked);
    }
}

form.onsubmit = async (event) => {
    event.preventDefault();

    // ---- 至少开启一个 ----
    if (!wifiToggle.checked && !bleToggle.checked) {
        toast('请至少开启 WiFi 或蓝牙中的一项');
        return;
    }

    // ---- WiFi 校验 ----
    if (wifiToggle.checked) {
        if (!ssidInput.value.trim()) {
            toast('请填写WiFi名称'); return;
        }
        if (!macInput.checkValidity()) {
            toast('WiFi MAC地址格式错误 (aa:bb:cc:dd:ee:ff)'); return;
        }
    }

    // ---- 蓝牙校验 ----
    if (bleToggle.checked) {
        if (!bleMacInput.value.trim()) {
            toast('请填写蓝牙MAC地址'); return;
        }
        if (!bleMacInput.checkValidity()) {
            toast('蓝牙MAC地址格式错误 (aa:bb:cc:dd:ee:ff)'); return;
        }
        if (!bleRawInput.value.trim()) {
            toast('请填写蓝牙广播原始数据'); return;
        }
    }

    const data = new URLSearchParams(new FormData(form));
    const response = await fetch('/', { method: 'POST', body: data });
    if (response.ok) {
        toast(await response.text());
    } else {
        toast('发送失败 ' + response.statusText);
    }
};

function toast(message) {
    popup.style.visibility = 'visible';
    popupText.textContent = message;
    setTimeout(() => { popup.style.visibility = 'hidden'; }, 5000);
}
