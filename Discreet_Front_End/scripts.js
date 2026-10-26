// Set an element's text without throwing if the element isn't on the page.
// (A missing element would otherwise stop the whole poll, including the chart.)
function setText(id, text) {
  const el = document.getElementById(id);
  if (el) el.innerText = text;
}

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
	    tempChart.data.datasets[3].data.push(data.weight);
    tempChart.data.datasets[4].data.push(data.targetweight);
    if (tempChart.data.labels.length > 160) {
      tempChart.data.labels.shift();
      tempChart.data.datasets.forEach(ds => ds.data.shift());
    }
    tempChart.update();

    // Labels
    setText('actimelabel', data.actime);
    setText('pressure', data.pressure);
    setText('pumppower', data.pumppower);

    // Live scale weight ("--" when the scale isn't connected)
    setText('weight', data.scaleOn ? data.weight.toFixed(1) + " g" : "--");

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
      const sp = document.getElementById('setpointlabel');
      if (sp) sp.innerHTML = data.setpoint + "&deg;C";

      setText('preinftimelabel', data.preinftime + " S");
      setText('bloomtime', data.bloomtime + " S");
      setText('pressuresetpoint', data.pressuresetpoint + " Bar");
	  setText('prepressure', data.prepressure + " Bar");
      setText('Kp', data.Kp + " Kp");
      setText('Ki', data.Ki + " Ki");
      setText('Kd', data.Kd + " Kd");
      setText('targetweight', data.targetweight + " g");
      setText('weightoffset', data.weightoffset + " g");
    })
    .catch(err => console.error("Label update failed", err));
}

updateLabels();
