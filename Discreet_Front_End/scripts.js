setInterval(async () => {
  try {
    const res = await fetch('/getValues');
    const data = await res.json();
    const time = new Date().toLocaleTimeString();

    // Chart
    tempChart.data.labels.push(time);
    tempChart.data.datasets[0].data.push(data.temp);
    tempChart.data.datasets[1].data.push(data.pressure);
    tempChart.data.datasets[2].data.push(data.setpoint);
    if (tempChart.data.labels.length > 160) {
      tempChart.data.labels.shift();
      tempChart.data.datasets.forEach(ds => ds.data.shift());
    }
    tempChart.update();

    // Labels
    document.getElementById('actimelabel').innerText = data.actime;
    document.getElementById('pressure').innerText = data.pressure;
    document.getElementById('pumppower').innerText = data.pumppower;

    // Gauge
    gauge.set(parseFloat(data.pressure));

  } catch (err) {
    console.error('Poll failed', err);
  }
}, 250);

function adjust(variable, value) {
  fetch(`/adjust?var=${variable}&val=${value}`)
  .then(() => updateLabels())
  .catch(err => console.error("Adjust failed", err));
}

function updateLabels() {
  fetch('/getValues')
    .then(res => res.json())
    .then(data => {
      document.getElementById('setpointlabel').innerHTML = data.setpoint + "&deg;C";
      document.getElementById('preinftimelabel').innerText = (data.preinftime) + " S";
      document.getElementById('bloomtime').innerText = (data.bloomtime) + " S";
      document.getElementById('pressuresetpoint').innerText = data.pressuresetpoint + " Bar";
	  document.getElementById('Kp').innerText = data.Kp  + " Kp";
	  document.getElementById('Ki').innerText = data.Ki  + " Ki";
	  document.getElementById('Kd').innerText = data.Kd  + " Kd";
      if (data.weight !== undefined) document.getElementById('bbw-weight').innerText = data.weight;
      if (data.targetweight !== undefined) document.getElementById('bbw-target').innerText = data.targetweight;
      if (data.bbw !== undefined) document.getElementById('bbw-enabled').checked = !!data.bbw;
    });
}

function setTargetWeight(delta) {
  fetch('/getValues').then(r => r.json()).then(data => {
    const cur = parseFloat(data.targetweight || 36);
    fetch(`/adjust?var=targetweight&val=${cur + delta}`);
  }).catch(err => console.error("Target weight failed", err));
}

function setBbwEnabled(on) {
  fetch(`/adjust?var=bbw&val=${on ? 1 : 0}`)
    .catch(err => console.error("BBW toggle failed", err));
}

updateLabels();