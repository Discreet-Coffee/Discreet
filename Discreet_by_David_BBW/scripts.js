setInterval(async () => {
  try {
    const res = await fetch('/getValues');
    const data = await res.json();
    const time = new Date().toLocaleTimeString();

    // 1. Maintain your machine's pre-existing graph plotting engine logic
    if (typeof tempChart !== 'undefined') {
      tempChart.data.labels.push(time);
      tempChart.data.datasets[0].data.push(data.temp);
      tempChart.data.datasets[1].data.push(data.pressure);
      tempChart.data.datasets[2].data.push(data.setpoint);
      if (tempChart.data.labels.length > 160) {
        tempChart.data.labels.shift();
        tempChart.data.datasets.forEach(ds => ds.data.shift());
      }
      tempChart.update();
    }

    // 2. Map existing interface hardware text parameters
    if (document.getElementById('actimelabel')) document.getElementById('actimelabel').innerText = data.actime;
    if (document.getElementById('pressure')) document.getElementById('pressure').innerText = data.pressure;
    if (document.getElementById('pumppower')) document.getElementById('pumppower').innerText = data.pumppower;
    if (typeof gauge !== 'undefined') gauge.set(parseFloat(data.pressure));

    // ====================================================================
    // SMART SCALE FRONTEND MAPS
    // ====================================================================

       // Render Bluetooth Status Indicators with Symbols
	const bleIcon = document.getElementById('ble-status-icon');
	if (bleIcon) {
		if (data.scaleConnected === 1) {
		bleIcon.innerHTML = "ᛒ 📶 SCALE CONNECTED";
		bleIcon.className = "ble-connected";
	} else {
		bleIcon.innerHTML = "❌ ᛒ BLE DISCONNECTED";
		bleIcon.className = "ble-disconnected";
  }
}


    // Render Live Float Metric Numbers
    if (document.getElementById('scale-weight-val')) document.getElementById('scale-weight-val').innerText = parseFloat(data.scaleWeight).toFixed(1);
    if (document.getElementById('scale-flow-val')) document.getElementById('scale-flow-val').innerText = parseFloat(data.scaleFlow).toFixed(1);
    if(document.getElementById('target-weight-label')) document.getElementById('target-weight-label').innerText = parseFloat(data.targetWeight).toFixed(0);


    // Parse and style battery level states dynamically
    const battElem = document.getElementById('scale-batt-val');
    if (battElem) {
      const battPct = parseInt(data.scaleBattery);
      if (isNaN(battPct) || data.scaleConnected !== 1) {
        battElem.innerText = "--";
        battElem.className = "";
      } else {
        battElem.innerText = battPct;
        battElem.className = (battPct <= 20) ? "batt-low" : "batt-good";
      }
    }

    // Decode Stopwatch Integers to standard MM:SS string representations
    if (document.getElementById('scale-timer-val')) {
      let totalSec = parseInt(data.scaleTimer);
      let padMin = String(Math.floor(totalSec / 60)).padStart(2, '0');
      let padSec = String(totalSec % 60).padStart(2, '0');
      document.getElementById('scale-timer-val').innerText = `${padMin}:${padSec}`;
    }

    // Dynamic fallback update for labels to prevent stale values on boot
    updateLabelsFromData(data);

  } catch (err) {
    console.error('Network sync pending...', err);
  }
}, 250);

// Global operational action command dispatcher hook (Single, clean Async implementation)
async function adjust(variable, value) {
  try {
    const response = await fetch(`/adjust?var=${variable}&val=${value}`);
    if (response.ok) {
      // Re-fetch instantly after a click to ensure instant UI responsiveness
      const res = await fetch('/getValues');
      const data = await res.json();
      updateLabelsFromData(data);
    }
  } catch (err) {
    console.error('Command dropped:', err);
  }
}

// Safely updates layout values without relying on non-existent elements
function updateLabelsFromData(data) {
  if (document.getElementById('setpointlabel')) document.getElementById('setpointlabel').innerHTML = data.setpoint + "&deg;C";
  if (document.getElementById('preinftimelabel')) document.getElementById('preinftimelabel').innerText = data.preinftime + " S";
  if (document.getElementById('pressuresetpoint')) document.getElementById('pressuresetpoint').innerText = data.pressuresetpoint + " Bar";
  if (document.getElementById('bloomtimelabel')) document.getElementById('bloomtimelabel').innerText = data.bloomtime + " S"; // Fixed
  if (document.getElementById('Kp')) document.getElementById('Kp').innerText = data.Kp + " Kp";
  if (document.getElementById('Ki')) document.getElementById('Ki').innerText = data.Ki + " Ki";
  if (document.getElementById('Kd')) document.getElementById('Kd').innerText = data.Kd + " Kd";
}
