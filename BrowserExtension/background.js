const HOST = "com.himaya.security";
function nativeState() {
  return new Promise(resolve => {
    let port;
    let finished = false;
    const done = value => { if (!finished) { finished = true; try { port?.disconnect(); } catch (_) {} resolve(value); } };
    try {
      port = chrome.runtime.connectNative(HOST);
      port.onMessage.addListener(done);
      port.onDisconnect.addListener(() => done({ enabled: false, error: chrome.runtime.lastError?.message || "Himaya host unavailable" }));
      port.postMessage({ action: "get_state" });
    } catch (error) { done({ enabled: false, error: String(error) }); }
  });
}
chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message?.type === "himaya-state") {
    nativeState().then(sendResponse);
    return true;
  }
});
