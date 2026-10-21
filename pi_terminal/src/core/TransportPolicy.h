#pragma once

#include <QJsonObject>
#include <QString>

// Product profiles select one primary session lane at a time. The PBR1
// control channel is independent and never carries package/business payloads.
struct TransportPolicy final {
    enum class Mode { Invalid, BleOnly, LanOnly };
    enum class Package { Invalid, ActiveDataLane, HttpUrl, HotspotHttp };

    Mode mode = Mode::Invalid;
    Package package = Package::Invalid;

    static bool applyModuleConfig(QJsonObject *hostProfile, const QJsonObject &moduleConfig) {
        if (!hostProfile || moduleConfig.value("schema").toString() != "module-transport-v1") return false;
        const auto mode = moduleConfig.value("transport_mode").toString();
        const auto packageName = moduleConfig.value("package_transport").toString();
        if ((mode != "fixed_ble" && mode != "fixed_hotspot_lan") ||
            (packageName != "data" && packageName != "hotspot_lan")) return false;
        if (mode == "fixed_hotspot_lan" && packageName != "data") return false;
        (*hostProfile)["transport_mode"] = mode;
        (*hostProfile)["package_transport"] = packageName;
        return true;
    }

    static TransportPolicy fromProfile(const QJsonObject &profile) {
        TransportPolicy policy;
        const auto modeName = profile.value("transport_mode").toString();
        if (modeName == "fixed_ble") policy.mode = Mode::BleOnly;
        else if (modeName == "fixed_hotspot_lan") policy.mode = Mode::LanOnly;

        auto packageName = profile.value("package_transport").toString();
        // Default all modes to the authenticated active data lane. This keeps
        // LAN-only sensor data and .so packages strictly on LAN; a BLE-only
        // module can opt into a short-lived hotspot HTTP package lease using
        // the explicit "hotspot_lan" value.
        if (packageName.isEmpty()) packageName = "data";
        if (packageName == "data") policy.package = Package::ActiveDataLane;
        else if (packageName == "http") policy.package = Package::HttpUrl;
        else if (packageName == "hotspot_lan") policy.package = Package::HotspotHttp;
        return policy;
    }

    bool valid() const { return mode != Mode::Invalid && package != Package::Invalid; }
    bool allowsBle() const { return mode == Mode::BleOnly; }
    bool allowsLan() const { return mode == Mode::LanOnly; }
    bool sessionNeedsHotspot() const { return mode == Mode::LanOnly; }
    bool packageUsesDataLane() const { return package == Package::ActiveDataLane; }
    bool packageNeedsHotspot() const { return package == Package::HotspotHttp; }
    quint8 initialTransport() const { return allowsLan() ? 2 : 1; }
};
