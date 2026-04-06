var opts = {
angle: -0.39, 
lineWidth: 0.1,
radiusScale: 1,
pointer: {
	length: 0.5,
	strokeWidth: 0.04,
	color: '#999999'
},
limitMax: true,
limitMin: false,
colorStart: '#6FADCF',
colorStop: '#8FC0DA',
strokeColor: '#E0E0E0',
highDpiSupport: true,
staticLabels: {
	font: "15px sans-serif",
	labels: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12],
	color: "#999999",
	fractionDigits: 0
},
staticZones: [
	{strokeStyle: "#3c3c3c", min: 0, max: 3},
	{strokeStyle: "#a07820", min: 3, max: 7},
	{strokeStyle: "#f0c040", min: 7, max: 10},
	{strokeStyle: "#5e0000", min: 10, max: 12}
]
};

var target = document.getElementById('gauge');
var gauge = new Gauge(target).setOptions(opts);
gauge.maxValue = 12;
gauge.setMinValue(0);
gauge.animationSpeed = 32;
gauge.set(0);
