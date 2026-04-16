/**
 * browser-sync config — Medusa UI dev server
 *
 * `npm run dev`  — serves data/ with live-reload, proxies /api/* to the ESP32.
 *
 * Change ESP32_IP to match your device (AP default: 192.168.4.1,
 * or whatever IP it gets on your home network).
 */

const ESP32_IP = "192.168.4.1";

module.exports = {
    server: {
        baseDir: "data",
        // Also serve ui-dev/ at /ui-dev/ so component sandboxes are reachable
        routes: { "/ui-dev": "ui-dev" }
    },
    files: ["data/**", "ui-dev/**"],
    port: 3000,
    notify: false,
    open: true,

    // Proxy /api/* and /update/* to the real ESP32
    middleware: [
        {
            route: /^\/(api|update)\//,
            handle: require("http-proxy-middleware").createProxyMiddleware({
                target: `http://${ESP32_IP}`,
                changeOrigin: true,
                logLevel: "silent",
                on: {
                    error: (err, req, res) => {
                        res.writeHead(502, { "Content-Type": "application/json" });
                        res.end(JSON.stringify({ error: "ESP32 unreachable", detail: err.message }));
                    }
                }
            })
        }
    ]
};
