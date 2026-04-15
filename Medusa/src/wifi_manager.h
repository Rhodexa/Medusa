#pragma once
#include <Arduino.h>
#include <Preferences.h>

#define MEDUSA_AP_SSID_PREFIX "Medusa-"
#define MEDUSA_AP_PASS        "wnUjknMdjSUS"  // shared with node firmware; change by reflashing
#define MEDUSA_MAX_NETWORKS   2
#define MEDUSA_CONNECT_TIMEOUT_MS 10000
#define MEDUSA_RECONNECT_INTERVAL_MS 30000

class WiFiManager {
public:
    struct Credential {
        String ssid;
        String password;
    };

    // Call once in setup(). Mounts nothing — just WiFi.
    void begin();

    // Call every loop(). Handles reconnect attempts if STA drops.
    void tick();

    bool isStaConnected() const;
    String apSSID()  const { return _apSSID; }
    String staSSID() const { return _staSSID; }
    String staIP()   const;

    Credential getCredential(int slot) const;
    void saveCredential(int slot, const String& ssid, const String& password);
    void clearCredential(int slot);

private:
    String     _apSSID;
    String     _staSSID;
    Credential _creds[MEDUSA_MAX_NETWORKS];
    unsigned long _lastReconnectAttempt = 0;
    bool       _staConnected = false;

    void _buildAPSSID();
    void _startAP();
    void _loadCredentials();
    bool _tryConnect();
};
