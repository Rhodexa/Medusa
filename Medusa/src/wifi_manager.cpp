#include "wifi_manager.h"
#include <WiFi.h>
#include "esp_wifi.h"

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

// Builds AP SSID, loads saved credentials, starts AP, and attempts STA connection.
void WiFiManager::begin() {
    _buildAPSSID();
    _loadCredentials();
    _startAP();
    _tryConnect();
}

// Detects STA connect/disconnect events and retries connection every
// MEDUSA_RECONNECT_INTERVAL_MS when disconnected.
void WiFiManager::tick() {
    if (_staSSID.isEmpty()) return;  // nothing to reconnect to

    bool currentlyConnected = (WiFi.status() == WL_CONNECTED);
    if (currentlyConnected != _staConnected) {
        _staConnected = currentlyConnected;
        if (_staConnected) {
            Serial.printf("[WiFi] STA connected to %s — IP: %s\n",
                          WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        } else {
            Serial.println("[WiFi] STA disconnected.");
        }
    }

    if (!_staConnected) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt >= MEDUSA_RECONNECT_INTERVAL_MS) {
            _lastReconnectAttempt = now;
            Serial.println("[WiFi] Attempting reconnect...");
            _tryConnect();
        }
    }
}

// Returns true if STA is currently associated with a home network.
bool WiFiManager::isStaConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

// Returns the STA IP as a string, or "0.0.0.0" if not connected.
String WiFiManager::staIP() const {
    return WiFi.localIP().toString();
}

// Returns a copy of the credential stored in the given slot (0 or 1).
WiFiManager::Credential WiFiManager::getCredential(int slot) const {
    if (slot < 0 || slot >= MEDUSA_MAX_NETWORKS) return {};
    return _creds[slot];
}

// Persists a credential to NVS, updates the in-memory copy, and immediately
// tries to connect with the new credential.
void WiFiManager::saveCredential(int slot, const String& ssid, const String& password) {
    if (slot < 0 || slot >= MEDUSA_MAX_NETWORKS) return;
    _creds[slot] = {ssid, password};

    Preferences prefs;
    prefs.begin("medusa-wifi", false);
    String keySSID = "ssid"  + String(slot);
    String keyPass = "pass"  + String(slot);
    prefs.putString(keySSID.c_str(), ssid);
    prefs.putString(keyPass.c_str(), password);
    prefs.end();

    Serial.printf("[WiFi] Saved credential slot %d: %s\n", slot, ssid.c_str());

    // Immediately try the new credential if it's in slot 0, or slot 1 as fallback
    _tryConnect();
}

// Removes a credential from NVS and clears it in memory.
void WiFiManager::clearCredential(int slot) {
    if (slot < 0 || slot >= MEDUSA_MAX_NETWORKS) return;
    _creds[slot] = {};

    Preferences prefs;
    prefs.begin("medusa-wifi", false);
    prefs.remove(("ssid" + String(slot)).c_str());
    prefs.remove(("pass" + String(slot)).c_str());
    prefs.end();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

// Derives the AP SSID from the last 3 bytes of the STA MAC, e.g. "Medusa-098E74".
void WiFiManager::_buildAPSSID() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);
    _apSSID = String(MEDUSA_AP_SSID_PREFIX) + suffix;
}

// Starts the soft-AP on a fixed channel with the shared mesh password.
// AP stays up regardless of STA connection state.
void WiFiManager::_startAP() {
    WiFi.mode(WIFI_AP_STA);
    esp_wifi_set_max_tx_power(78);          // 78 quarter-dBm steps = 19.5 dBm (hardware max)
    WiFi.softAP(_apSSID.c_str(), MEDUSA_AP_PASS);

    // In AP+STA mode the AP channel gets overridden to match the home router's
    // channel once STA connects — log the actual channel so we can diagnose range.
    uint8_t actualCh = WiFi.channel();
    Serial.printf("[WiFi] AP started: %s  ch=%d  (192.168.4.1)\n", _apSSID.c_str(), actualCh);
}

// Reads both credential slots from NVS into memory. Opens read-write so the
// namespace is created silently on first boot.
void WiFiManager::_loadCredentials() {
    Preferences prefs;
    prefs.begin("medusa-wifi", false);
    for (int i = 0; i < MEDUSA_MAX_NETWORKS; i++) {
        _creds[i].ssid     = prefs.getString(("ssid" + String(i)).c_str(), "");
        _creds[i].password = prefs.getString(("pass" + String(i)).c_str(), "");
    }
    prefs.end();
}

// Tries each non-empty credential slot in order, waiting up to
// MEDUSA_CONNECT_TIMEOUT_MS per attempt. Returns true on first success.
bool WiFiManager::_tryConnect() {
    for (int i = 0; i < MEDUSA_MAX_NETWORKS; i++) {
        if (_creds[i].ssid.isEmpty()) continue;

        Serial.printf("[WiFi] Trying slot %d: %s\n", i, _creds[i].ssid.c_str());
        WiFi.begin(_creds[i].ssid.c_str(), _creds[i].password.c_str());

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < MEDUSA_CONNECT_TIMEOUT_MS) {
            delay(200);
        }

        if (WiFi.status() == WL_CONNECTED) {
            _staSSID      = _creds[i].ssid;
            _staConnected = true;
            _lastReconnectAttempt = millis();
            Serial.printf("[WiFi] Connected to %s — IP: %s\n",
                          _staSSID.c_str(), WiFi.localIP().toString().c_str());
            return true;
        }

        Serial.printf("[WiFi] Slot %d failed.\n", i);
        WiFi.disconnect();
    }

    _staSSID      = "";
    _staConnected = false;
    return false;
}
