function cssVar(name) {
  return getComputedStyle(document.documentElement)
         .getPropertyValue(name)
         .trim();
}

// Make gauge global
var gauge;

window.addEventListener("load", function () {

  console.log(cssVar('--gauge-low')); // keep this test

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
      labels: [0,1,2,3,4,5,6,7,8,9,10,11,12],
      color: "#999999",
      fractionDigits: 0
    },

    staticZones: [
      {strokeStyle: cssVar('--gauge-low'), min: 0, max: 3},
      {strokeStyle: cssVar('--gauge-mid'), min: 3, max: 7},
      {strokeStyle: cssVar('--gauge-high'), min: 7, max: 10},
      {strokeStyle: cssVar('--gauge-danger'), min: 10, max: 12}
    ]
  };

  var target = document.getElementById('gauge');

  // Assign to global variable
  gauge = new Gauge(target).setOptions(opts);

  gauge.maxValue = 12;
  gauge.setMinValue(0);
  gauge.animationSpeed = 32;
  gauge.set(0);

});