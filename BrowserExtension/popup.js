let pendingOrigins = [];
const status = document.getElementById("status");
const grant = document.getElementById("grant");
document.getElementById("load").addEventListener("click", () => {
  status.textContent = "Reading selected websites from Himaya…";
  chrome.runtime.sendMessage({type:"himaya-state"}, state => {
    if (chrome.runtime.lastError || !state || state.error) {
      status.textContent = "Himaya connection unavailable. Install Himaya and keep its service running.";
      return;
    }
    const domains = (state.sites || "youtube.com;instagram.com;facebook.com").split(";")
      .map(x => x.trim().toLowerCase()).filter(x => /^[a-z0-9.-]+$/.test(x) && x.includes("."));
    if (!domains.length) { status.textContent = "No valid domains are configured in Himaya."; return; }
    pendingOrigins = domains.flatMap(domain => ["*://" + domain + "/*", "*://*." + domain + "/*"]);
    status.textContent = "Selected domains: " + domains.join(", ") + ". Press the permission button to continue.";
    grant.hidden = false;
  });
});
grant.addEventListener("click", async () => {
  try {
    const granted = await chrome.permissions.request({ origins: pendingOrigins });
    if (!granted) throw new Error("Permission was not granted.");
    const existing = await chrome.scripting.getRegisteredContentScripts();
    if (existing.some(item => item.id === "himaya-custom-sites"))
      await chrome.scripting.unregisterContentScripts({ ids: ["himaya-custom-sites"] });
    await chrome.scripting.registerContentScripts([{
      id: "himaya-custom-sites", matches: pendingOrigins, js: ["content.js"],
      runAt: "document_start", persistAcrossSessions: true
    }]);
    status.textContent = "Enabled for the selected domains. Refresh those website tabs.";
    grant.hidden = true;
  } catch (error) {
    status.textContent = "Could not enable: " + (error.message || String(error));
  }
});