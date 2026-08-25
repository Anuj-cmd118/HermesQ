#pragma once

#include <Arduino_RouterBridge.h>

// UNO Q RouterBridge: .result(out) returns bool and fills out by reference.
inline bool hizBridgeCall(const char* method, String& out, const char* arg = "") {
    bool ok;
    if (arg != nullptr && arg[0] != '\0') {
        ok = Bridge.call(method, arg).result(out);
    } else {
        ok = Bridge.call(method).result(out);
    }
    if (!ok) {
        out = "err:bridge call failed";
    }
    return ok;
}

inline bool hizBridgeOk(const String& resp, String& payload) {
    if (resp.startsWith("ok:")) {
        payload = resp.substring(3);
        return true;
    }
    if (resp.startsWith("err:")) {
        payload = resp.substring(4);
        return false;
    }
    payload = resp;
    return !resp.startsWith("err");
}
