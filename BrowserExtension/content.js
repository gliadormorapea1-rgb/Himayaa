(() => {
  "use strict";
  let state = { enabled: false, sites: "" };
  let lastPath = location.pathname;
  let lastRefresh = 0;
  const defaultPaths = [
    { suffix: "youtube.com", path: /^\/shorts(?:\/|$)/i },
    { suffix: "instagram.com", path: /^\/reels?(?:\/|$)/i },
    { suffix: "facebook.com", path: /^\/(?:reel|reels)(?:\/|$)/i }
  ];
  const selectedHosts = () => (state.sites || "youtube.com;instagram.com;facebook.com")
    .split(";").map(value => value.trim().toLowerCase()).filter(Boolean);
  const hostMatches = (host, domain) => host === domain || host.endsWith("." + domain);
  const pathForHost = host => {
    const known = defaultPaths.find(item => hostMatches(host, item.suffix));
    return known ? known.path : /^\/(?:shorts?|reels?)(?:\/|$)/i;
  };
  const siteRule = host => selectedHosts().find(domain => hostMatches(host, domain));
  const isBlockedPath = () => {
    if (!state.enabled || (state.until && Date.now() >= state.until * 1000)) return false;
    const domain = siteRule(location.hostname);
    return !!domain && pathForHost(domain).test(location.pathname);
  };
  const showNotice = () => {
    if (document.getElementById("himaya-reels-notice")) return;
    const notice = document.createElement("div");
    notice.id = "himaya-reels-notice";
    notice.textContent = "Himaya: Shorts & Reels are blocked during your focus session.";
    Object.assign(notice.style, {
      position: "fixed", zIndex: "2147483647", inset: "0 0 auto 0",
      padding: "18px", background: "#17243a", color: "white",
      font: "600 16px Segoe UI, sans-serif", textAlign: "center",
      boxShadow: "0 3px 18px #0008"
    });
    (document.documentElement || document).appendChild(notice);
  };
  const enforceRoute = () => {
    if (isBlockedPath()) {
      showNotice();
      if (location.pathname !== "/") location.replace("/");
    } else {
      const notice = document.getElementById("himaya-reels-notice");
      if (notice) notice.remove();
    }
  };
  const cleanLinks = () => {
    document.querySelectorAll("a[data-himaya-blocked='1']").forEach(anchor => {
      anchor.style.display = anchor.dataset.himayaDisplay || "";
      delete anchor.dataset.himayaDisplay;
      delete anchor.dataset.himayaBlocked;
      anchor.removeAttribute("aria-disabled");
    });
    if (!state.enabled) return;
    document.querySelectorAll("a[href]").forEach(anchor => {
      try {
        const target = new URL(anchor.href, location.href);
        const domain = siteRule(target.hostname);
        if (domain && pathForHost(domain).test(target.pathname)) {
          anchor.dataset.himayaDisplay = anchor.style.display;
          anchor.dataset.himayaBlocked = "1";
          anchor.setAttribute("aria-disabled", "true");
          anchor.style.setProperty("display", "none", "important");
        }
      } catch (_) {}
    });
  };
  const refresh = () => {
    lastRefresh = Date.now();
    chrome.runtime.sendMessage({ type: "himaya-state" }, result => {
      if (chrome.runtime.lastError || !result) state = { enabled: false };
      else state = result;
      enforceRoute();
      cleanLinks();
    });
  };
  refresh();
  setInterval(() => {
    if (location.pathname !== lastPath) { lastPath = location.pathname; refresh(); }
    else if (Date.now() - lastRefresh > 5000) refresh();
    if (state.enabled && state.until && Date.now() >= state.until * 1000) {
      state.enabled = false; enforceRoute(); cleanLinks();
    }
  }, 700);
  new MutationObserver(() => cleanLinks()).observe(document, { childList: true, subtree: true });
})();