// Read CSS variables for chart colors
function cssVar(name) {
  return getComputedStyle(document.documentElement)
         .getPropertyValue(name)
         .trim();
}

var tempChart;  // Make chart global

window.addEventListener("load", function () {

const ctx = document.getElementById('tempChart').getContext('2d');

tempChart = new Chart(ctx, {
	type: 'line',
	data: {
	labels: [],
	datasets: [

	{
		label: 'Temp (C)',
		data: [],
		borderColor: cssVar('--chart-temp'),
		tension: 0.2,
		pointRadius: 0,
		pointStyle: 'line'
	},

	{
		label: 'Pressure',
		data: [],
		borderColor: cssVar('--chart-pressure'),
		yAxisID: 'pressureAxis',
		tension: 0.2,
		pointRadius: 0,
		pointStyle: 'line'
	},

	{
		label: 'Target',
		data: [],
		borderColor: cssVar('--chart-target'),
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

		x: {
			display: false,
			grid: {
				color: cssVar('--chart-grid')
			},
			ticks: {
				color: cssVar('--chart-text')
			}
		},

		y: {
			type: 'linear',
			position: 'left',

			grid: {
				color: cssVar('--chart-grid')
			},

			ticks: {
				color: cssVar('--chart-text')
			},

			title: {
				display: true,
				text: 'Temp (C)',
				color: cssVar('--chart-text')
			}
		},

		pressureAxis: {
			type: 'linear',
			position: 'right',
			min: 0,
			max: 12,

			grid: {
				color: cssVar('--chart-grid')
			},

			ticks: {
				color: cssVar('--chart-text')
			},

			title: {
				display: true,
				text: 'Pressure (Bar)',
				color: cssVar('--chart-text')
			}
		}
	},

	plugins: {
		legend: {
			labels: {
				color: cssVar('--chart-text'),
				usePointStyle: true,
				pointStyle: 'line'
			}
		}
	}

	}

});

});

