if (window.location.search.includes("success=1")) {
  document.getElementById("status").innerText = "File uploaded successfully.";
  setTimeout(() => {
    history.replaceState(null, "", "/upload.html");
  }, 3000);
}

// Fetch and list files
function loadFileList() {
  fetch("/listFiles")
    .then(res => res.json())
    .then(files => {
      const container = document.getElementById("fileList");
      container.innerHTML = "";
      if (files.length === 0) {
        container.innerText = "No files found";
        return;
      }
      files.forEach(file => {
        const div = document.createElement("div");
        div.className = "file-entry";
        div.innerHTML = `<strong>${file.name}</strong> (${file.size} bytes)
          <span class="delete-btn" onclick="deleteFile('${file.name}')">[Delete]</span>`;
        container.appendChild(div);
      });
    });
}

// Delete file
function deleteFile(filename) {
  if (confirm(`Are you sure you want to delete "${filename}"?`)) {
    fetch("/delete?name=" + encodeURIComponent(filename))
      .then(res => res.text())
      .then(msg => {
        alert(msg);
        loadFileList();
      });
  }
}

window.addEventListener("DOMContentLoaded", function() {
  document.getElementById("fileUpload")
    .addEventListener("change", function() {
      document.getElementById("fileName").textContent =
        this.files[0]?.name || "No file chosen";
    });
});

// Load files on page load
window.addEventListener("load", loadFileList);