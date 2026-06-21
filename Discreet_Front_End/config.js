// Load config when page opens
window.addEventListener("load", loadConfig);


function loadConfig() {

  fetch("/getConfig")
    .then(response => response.json())
    .then(data => {

      document.getElementById("ssid").value =
        data.ssid || "";

      document.getElementById("password").value =
        data.password || "";

      document.getElementById("Kp").value =
        data.Kp || 0;

      document.getElementById("Ki").value =
        data.Ki || 0;

      document.getElementById("Kd").value =
        data.Kd || 0;
		
	  document.getElementById("setpoint").value =
        data.setpoint || 0;

      document.getElementById("offset").value =
        data.offset || 0;
	  
	  document.getElementById("steamSetpoint").value =
        data.steamSetpoint || 0;
		
	  document.getElementById("PIDonly").checked =
        data.PIDonly || false;

    })
    .catch(error => {
      console.log("Config load failed:", error);
    });

}


function saveConfig() {

  let config = {

    ssid: document.getElementById("ssid").value,

    password: document.getElementById("password").value,

    Kp: Number(
      document.getElementById("Kp").value
    ),

    Ki: Number(
      document.getElementById("Ki").value
    ),

    Kd: Number(
      document.getElementById("Kd").value
    ),
	
    setpoint: Number(
      document.getElementById("setpoint").value
    ),

    offset: Number(
      document.getElementById("offset").value
    ),
	
	steamSetpoint: Number(
      document.getElementById("steamSetpoint").value
    ),
	
	PIDonly: document.getElementById("PIDonly").checked

  };


  fetch("/saveConfig", {

    method: "POST",

    headers: {
      "Content-Type": "application/json"
    },

    body: JSON.stringify(config)

  })
  .then(response => {
    console.log("Config saved");
    alert("Configuration Saved");
  })
  .catch(error => {
    console.log("Save failed:", error);
    alert("Save Failed");
  });

}