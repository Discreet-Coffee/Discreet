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
    });
}

updateLabels();