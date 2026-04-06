const ctx = document.getElementById('tempChart').getContext('2d');
const tempChart = new Chart(ctx, {
	type: 'line',
	data: {
	labels: [],
	datasets: [{
		label: 'Temp (C)',
		data: [],
		borderColor: 'rgb(0, 174, 174)',
		tension: 0.2,
		pointRadius: 0,
		pointStyle: 'line'
	},
	{
		label: 'Pressure',
		data: [],
		borderColor: 'orange',
		yAxisID: 'pressureAxis',
		tension: 0.2,
		pointRadius: 0,
		pointStyle: 'line'
	},
	{
		label: 'Target',
		data: [],
		borderColor: 'rgb(200, 200, 200)',
		borderDash: [5, 5],
		tension: 0.2,
		pointRadius: 0,
		pointStyle: 'line'
	}
	]
	},
	options: {
	animation: false,
	responsive: true,
	scales: {
		x: { display: false },
		y: {
			type: 'linear',
			position: 'left',
			title: {
			display: true,
			text: 'Temp (C)',
			}
		},
		pressureAxis: {
		type: 'linear',
		position: 'right',
		min: 0,
		max: 12,
		title: {
			display: true,
			text: 'Pressure (Bar)',
		}
		},
	},
	plugins: {
		legend: {
		labels: {
			usePointStyle: true,     // This makes legend use pointStyle
			pointStyle: 'line'       // Ensures the line symbol appears
		}
		}
	}
	}
});

