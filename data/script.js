const popup = document.getElementById('popup');
const popupText = document.getElementById('message');
const form = document.getElementById('info_form');
const wifiToggle = document.getElementById('wifi_toggle');
const bleToggle  = document.getElementById('ble_toggle');
const wifiFields = document.getElementById('wifi_fields');
const bleFields  = document.getElementById('ble_fields');
const ssidInput = document.getElementById('ssid');
const macInput  = document.getElementById('mac');
const bleUuidInput = document.getElementById('ble_uuid');

function toggleSection(type) {
    if (type === 'wifi') wifiFields.classList.toggle('active', wifiToggle.checked);
    if (type === 'ble')  bleFields.classList.toggle('active', bleToggle.checked);
}

form.onsubmit = async (event) => {
    event.preventDefault();

    if (!wifiToggle.checked && !bleToggle.checked) {
        toast('请至少开启 WiFi 或蓝牙中的一项');
        return;
    }

    if (wifiToggle.checked) {
        if (!ssidInput.value.trim()) { toast('请填写WiFi名称'); return; }
        if (!macInput.checkValidity()) { toast('WiFi MAC格式错误'); return; }
    }

    if (bleToggle.checked) {
        if (!bleUuidInput.value.trim()) { toast('请填写UUID/MAC'); return; }
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
    popupText.textContent = message;
    popup.classList.add('show');
    clearTimeout(popup._timer);
    popup._timer = setTimeout(() => popup.classList.remove('show'), 4000);
}
