function loadThemeList() {

    Promise.all([
        fetch("/listFiles").then(r => r.json()),
        fetch("/getCurrentTheme").then(r => r.text())
    ])
    .then(([files, currentTheme]) => {

        const container =
            document.getElementById("themeList");

        container.innerHTML = "";

        files.forEach(file => {

            if (file.name.endsWith(".css")
                && file.name !== "global.css") {

                const div =
                    document.createElement("div");

                div.className = "file-entry";

                let label = "[Apply]";

                if (file.name.trim() === currentTheme.trim()) {
                    label = "[ACTIVE]";
                }

                div.innerHTML =
                    `<strong>${file.name}</strong>
                     <span class="delete-btn"
                     onclick="applyTheme('${file.name}')">
                     ${label}</span>`;

                container.appendChild(div);
            }

        });

    });
}




function applyTheme(filename) {

    fetch("/applyTheme?name="
        + encodeURIComponent(filename))
        .then(() => {

            location.href = "/?t=" + Date.now();

        });

}



// Load themes when page opens
window.addEventListener(
    "load",
    loadThemeList
);