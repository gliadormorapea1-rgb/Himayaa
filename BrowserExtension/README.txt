Himaya Shorts & Reels Guard
===========================

Install Himaya Security first. The installer copies this extension and registers
its read-only Native Messaging host for Chrome and Edge.

One-time browser activation:
1. Open chrome://extensions (Chrome) or edge://extensions (Edge).
2. Enable Developer mode.
3. Choose "Load unpacked" and select the BrowserExtension folder inside the
   Himaya installation directory.
4. Open the extension popup and press "Enable selected websites". Grant access
   only to the domains listed in Himaya.

The extension blocks Shorts/Reels-style routes and matching links. YouTube,
Instagram and Facebook use their known route patterns. Additional domains saved
in Himaya can be granted individually through the popup; common /shorts, /reel
and /reels routes are blocked on those domains.

The native host reads only the Himaya settings database and does not have write
access to it. If the host is unavailable, the extension fails open; existing
Himaya application, website and DNS protections remain separate.
