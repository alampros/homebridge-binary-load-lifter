#pragma once

// Copy this file to secrets.h, then replace every placeholder.
// secrets.h is excluded from Git so your Wi-Fi password and API token stay local.

constexpr char WIFI_SSID[] = "YOUR_WIFI_NETWORK";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

// Use a long, randomly generated value (at least 32 characters). Homebridge
// will send this value as: Authorization: Bearer <API_TOKEN>
constexpr char API_TOKEN[] = "REPLACE_WITH_A_LONG_RANDOM_TOKEN";
