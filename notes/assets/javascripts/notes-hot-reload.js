(function () {
  if (!window.location || !window.fetch) {
    return;
  }

  var pagePort = Number(window.location.port || (window.location.protocol === "https:" ? 443 : 80));
  if (!Number.isFinite(pagePort)) {
    return;
  }

  var reloadUrl = window.location.protocol + "//" + window.location.hostname + ":" + (pagePort + 1) + "/version";
  var currentVersion = null;

  function checkReloadVersion() {
    fetch(reloadUrl, { cache: "no-store", mode: "cors" })
      .then(function (response) {
        if (!response.ok) {
          throw new Error("reload endpoint unavailable");
        }
        return response.json();
      })
      .then(function (payload) {
        if (!payload || typeof payload.version === "undefined") {
          return;
        }
        if (currentVersion === null) {
          currentVersion = payload.version;
          return;
        }
        if (payload.version !== currentVersion) {
          window.location.reload();
        }
      })
      .catch(function () {
        // The reload endpoint is only available while serve_site is running.
      });
  }

  window.setInterval(checkReloadVersion, 1000);
  checkReloadVersion();
})();
