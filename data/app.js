function openTab(id) {
  document.querySelectorAll(".tab").forEach((t) => (t.style.display = "none"));
  document.getElementById(id).style.display = "block";
}

let modbusRows = [];
// Load config on page load
fetch("/getConfig")
  .then((res) => res.json())
  .then((cfg) => {
    if (cfg.ap) {
      ap_ssid.value = cfg.ap.ssid || "";
      ap_pass.value = cfg.ap.pass || "";
    }

    mqtt_host.value = cfg.mqtt.host;
    mqtt_port.value = cfg.mqtt.port;
    mqtt_client.value = cfg.mqtt.clientId;
    mqtt_user.value = cfg.mqtt.user;
    mqtt_pass.value = cfg.mqtt.pass;
    mqtt_topic.value = cfg.mqtt.topic;
    poll.value = cfg.poll_interval;
  })
  .catch((err) => {
    alert("Failed to load config");
    console.log(err);
  });

fetch("/getModbus")
  .then((res) => res.json())
  .then((rows) => {
    modbusRows = rows;
    renderModbusTable();
  });

function saveDeviceConfig() {
  const device = {
    ap: {
      ssid: ap_ssid.value,
      pass: ap_pass.value,
    },
  };

  fetch("/saveDevice", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(device),
  })
    .then(() => alert("AP settings saved. Rebooting..."))
    .catch(() => alert("Save failed"));
}

// Save config
function saveConfig() {
  const config = {
    gprs: {
      apn: "airtelgprs.com",
    },
    mqtt: {
      host: mqtt_host.value,
      port: parseInt(mqtt_port.value),
      clientId: mqtt_client.value,
      user: mqtt_user.value,
      pass: mqtt_pass.value,
      topic: mqtt_topic.value,
    },
    poll_interval: parseInt(poll.value),
  };

  fetch("/saveConfig", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(config),
  })
    .then(() => {
      alert("Saved! Device rebooting...");
    })
    .catch((err) => {
      alert("Save failed");
      console.log(err);
    });
}

function sendTestMqtt() {
  const msg = document.getElementById("mqtt_test_msg").value;

  fetch("/testMqtt", {
    method: "POST",
    headers: { "Content-Type": "text/plain" },
    body: msg,
  })
    .then((res) => res.text())
    .then((txt) => {
      document.getElementById("test_result").innerText = txt;
    })
    .catch(() => {
      document.getElementById("test_result").innerText = "Failed";
    });
}

function clearConsole() {
  fetch("/clearLogs", { method: "POST" })
    .then(() => {
      document.getElementById("console_log").value = "";
    })
    .catch(() => {
      alert("Failed to clear logs");
    });
}

function sendConsoleCmd() {
  const cmd = document.getElementById("console_cmd").value;
  appendLog(">> " + cmd);
  // TODO: send to ESP32 backend
}

let logTimer = null;

function toggleLog() {
  if (logTimer) {
    clearInterval(logTimer);
    logTimer = null;
    appendLog("[Log Monitor OFF]");
    return;
  }

  appendLog("[Log Monitor ON]");

  logTimer = setInterval(() => {
    fetch("/logs")
      .then((res) => res.text())
      .then((txt) => {
        const log = document.getElementById("console_log");
        log.value = txt;
        log.scrollTop = log.scrollHeight;
      })
      .catch(() => {
        appendLog("[Log fetch failed]");
      });
  }, 1000); // poll every 1 second
}

function openModbusForm() {
  document.getElementById("modbusOverlay").style.display = "flex";
}

function closeModbusForm() {
  document.getElementById("modbusOverlay").style.display = "none";
}

function saveModbusRow() {
  const regs = [];
  const table = document.getElementById("regTable");

  for (let i = 1; i < table.rows.length; i++) {
    const cells = table.rows[i].cells;
    regs.push({
      index: i - 1,
      name: cells[1].children[0].value,
      scale: parseFloat(cells[2].querySelector("select").value),
    });
  }

  const modbusCfg = {
    slave: Number(mb_slave.value),
    func: Number(mb_func.value),
    start: Number(mb_start.value),
    count: Number(mb_count.value),
    poll: Number(mb_poll.value),
    registers: regs,
  };

  modbusRows.push(modbusCfg);

  fetch("/saveModbus", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(modbusRows),
  });

  renderModbusTable();
  closeModbusForm();
}

function renderModbusTable() {
  const table = document.querySelector("#modbusReg table");
  table.innerHTML = `
    <tr>
      <th>Slave</th><th>Func</th><th>Start</th>
      <th>Count</th><th>Poll</th><th>Action</th>
    </tr>`;

  modbusRows.forEach((r, i) => {
    const row = table.insertRow();
    row.innerHTML = `
      <td>${r.slave}</td>
      <td>${r.func}</td>
      <td>${r.start}</td>
      <td>${r.count}</td>
      <td>${r.poll}</td>
      <td>
        <button onclick="editRow(${i})">Edit</button>
        <button onclick="deleteRow(${i})">Delete</button>
      </td>`;
  });
}

function deleteRow(i) {
  modbusRows.splice(i, 1);
  renderModbusTable();

  fetch("/saveModbus", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(modbusRows),
  });
}

function generateRegisterRows() {
  const count = Number(document.getElementById("mb_count").value);
  const table = document.getElementById("regTable");

  // Clear old rows (keep header)
  table.innerHTML = `
    <tr>
      <th>Index</th>
      <th>Register Name</th>
      <th>Scale</th>
    </tr>
  `;

  for (let i = 0; i < count; i++) {
    const row = table.insertRow();
    row.innerHTML = `
      <td>${i}</td>
      <td><input placeholder="Enter Key" /></td>
      <td>
        <select>
          <option value="1">1</option>
          <option value="0.1">0.1</option>
          <option value="0.01">0.01</option>
          <option value="0.001">0.001</option>
        </select>
      </td>

    `;
  }
}

function editRow(i) {
  const r = modbusRows[i];

  mb_slave.value = r.slave;
  mb_func.value = r.func;
  mb_start.value = r.start;
  mb_count.value = r.count;
  mb_poll.value = r.poll;

  generateRegisterRows();

  const table = document.getElementById("regTable");
  r.registers.forEach((reg, idx) => {
    table.rows[idx + 1].cells[1].children[0].value = reg.name;
    table.rows[idx + 1].cells[2].querySelector("select").value = reg.scale;
  });

  modbusRows.splice(i, 1);
  openModbusForm();
}
function downloadFile(url, filename) {
  fetch(url)
    .then((res) => res.blob())
    .then((blob) => {
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = filename;
      a.click();
      URL.revokeObjectURL(a.href);
    });
}

function downloadConfig() {
  downloadFile("/api/config/download", "config.json");
}

function downloadModbus() {
  downloadFile("/api/modbus/download", "modbus.json");
}

document.addEventListener("DOMContentLoaded", () => {
  const cfg = document.getElementById("configFile");
  const mb = document.getElementById("modbusFile");

  if (cfg) {
    cfg.addEventListener("change", (e) => {
      uploadFile(e.target.files[0], "/api/config/upload");
    });
  }

  if (mb) {
    mb.addEventListener("change", (e) => {
      uploadFile(e.target.files[0], "/api/modbus/upload");
    });
  }
});

function uploadFile(file, url) {
  if (!file) return;

  const form = new FormData();
  form.append("file", file);

  fetch(url, { method: "POST", body: form }).then(() =>
    alert("Upload successful"),
  );
}

function setRTCTime() {
  fetch("/api/system/rtc", { method: "POST" });
}

function resetDevice() {
  fetch("/api/system/reset", { method: "POST" });
}

function factoryReset() {
  if (!confirm("Factory reset will erase all settings. Continue?")) return;
  fetch("/api/system/factory-reset", { method: "POST" });
}

function applyRS485() {
  const cfg = {
    baud: Number(document.getElementById("baud").value),
    databits: Number(document.getElementById("databits").value),
    parity: document.getElementById("parity").value,
    stopbits: Number(document.getElementById("stopbits").value),
  };

  fetch("/api/rs485", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(cfg),
  });
}

function appendLog(msg) {
  const log = document.getElementById("console_log");
  log.value += msg + "\n";
  log.scrollTop = log.scrollHeight;
}

function openTab(id, btn) {
  // hide all tabs (your original logic)
  document.querySelectorAll(".tab").forEach((t) => {
    t.style.display = "none";
  });

  // show selected tab
  document.getElementById(id).style.display = "block";

  // NEW: remove highlight from all buttons
  document.querySelectorAll(".tab-btn").forEach((b) => {
    b.classList.remove("active");
  });

  // NEW: highlight clicked button
  if (btn) btn.classList.add("active");
}

// default tab
openTab("ip");
