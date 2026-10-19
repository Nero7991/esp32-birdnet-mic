#pragma once
#include <stdint.h>
#include <stddef.h>

// Table-driven discovery, adapted from Dan Logue's v15 contribution. Entity
// identifiers and JSON field order are pinned against the previous firmware.
// The caller resolves only the command topic needed for this entity.
namespace mqtt_discovery {
enum class Command : uint8_t {
    None, Rtsp, Reboot, Stream1Enabled, Stream2Enabled, Stream1Target, Stream2Target
};
struct Entity {
    const char* component;
    const char* objectId;
    const char* name;
    const char* uniqueSuffix; // nullptr uses objectId
    const char* stateKey;     // nullptr omits state fields
    bool booleanState;
    Command command;
    const char* attributes;   // fixed JSON fields, including their leading comma
};

static const Entity entities[] = {
    {"sensor", "wifi_rssi", "WiFi RSSI", nullptr, "wifi_rssi", false, Command::None,
     ",\"unit_of_meas\":\"dBm\",\"dev_cla\":\"signal_strength\",\"stat_cla\":\"measurement\""},
    {"sensor", "heap_kb", "Free Heap", nullptr, "free_heap_kb", false, Command::None,
     ",\"unit_of_meas\":\"KB\",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\""},
    {"sensor", "packet_rate", "Packet Rate", "pkt_rate", "current_rate_pkt_s", false, Command::None,
     ",\"unit_of_meas\":\"pkt/s\",\"stat_cla\":\"measurement\""},
    {"sensor", "temperature_c", "Temperature", "temp_c", "temperature_c", false, Command::None,
     ",\"unit_of_meas\":\"\u00B0C\",\"dev_cla\":\"temperature\",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\""},
    {"sensor", "max_temperature_c", "Peak Temperature", "max_temp_c", "max_temperature_c", false, Command::None,
     ",\"unit_of_meas\":\"\u00B0C\",\"dev_cla\":\"temperature\",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:thermometer-high\""},
    {"sensor", "uptime_s", "Uptime", nullptr, "uptime_s", false, Command::None,
     ",\"unit_of_meas\":\"s\",\"dev_cla\":\"duration\",\"stat_cla\":\"total_increasing\",\"ent_cat\":\"diagnostic\""},
    {"binary_sensor", "streaming", "Streaming", nullptr, "streaming", true, Command::None,
     ",\"dev_cla\":\"running\""},
    {"switch", "rtsp_server", "RTSP Server", nullptr, "rtsp_server_enabled", true, Command::Rtsp,
     ",\"ic\":\"mdi:radio-tower\""},
    {"sensor", "rtsp_client", "RTSP Client", nullptr, "client", false, Command::None,
     ",\"ent_cat\":\"diagnostic\""},
    {"sensor", "fw_version", "Firmware", nullptr, "fw_version", false, Command::None,
     ",\"ent_cat\":\"diagnostic\""},
    {"sensor", "fw_build", "Build Date", nullptr, "fw_build", false, Command::None,
     ",\"ent_cat\":\"diagnostic\""},
    {"sensor", "reboot_reason", "Reboot Reason", nullptr, "reboot_reason", false, Command::None,
     ",\"ent_cat\":\"diagnostic\""},
    {"sensor", "restart_counter", "Restart Counter", nullptr, "restart_counter", false, Command::None,
     ",\"stat_cla\":\"total_increasing\",\"ent_cat\":\"diagnostic\""},
    {"sensor", "wifi_ssid", "WiFi SSID", nullptr, "wifi_ssid", false, Command::None,
     ",\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:wifi\""},
    {"sensor", "wifi_reconnect_count", "WiFi Reconnects", nullptr, "wifi_reconnect_count", false, Command::None,
     ",\"stat_cla\":\"total_increasing\",\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:wifi-refresh\""},
    {"sensor", "stream_uptime_s", "Stream Uptime", nullptr, "stream_uptime_s", false, Command::None,
     ",\"unit_of_meas\":\"s\",\"dev_cla\":\"duration\",\"stat_cla\":\"measurement\""},
    {"sensor", "client_count", "RTSP Client Count", nullptr, "client_count", false, Command::None,
     ",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:account-multiple\""},
    {"switch", "stream1_enabled", "Stream 1 Enabled", nullptr, "stream1_enabled", true, Command::Stream1Enabled,
     ",\"ic\":\"mdi:microphone\""},
    {"switch", "stream2_enabled", "Stream 2 Enabled", nullptr, "stream2_enabled", true, Command::Stream2Enabled,
     ",\"ic\":\"mdi:microphone\""},
    {"binary_sensor", "stream1_streaming", "Stream 1 Streaming", nullptr, "stream1_streaming", true, Command::None,
     ",\"dev_cla\":\"running\""},
    {"binary_sensor", "stream2_streaming", "Stream 2 Streaming", nullptr, "stream2_streaming", true, Command::None,
     ",\"dev_cla\":\"running\""},
    {"sensor", "stream1_clients", "Stream 1 Clients", nullptr, "stream1_clients", false, Command::None,
     ",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:account-multiple\""},
    {"sensor", "stream2_clients", "Stream 2 Clients", nullptr, "stream2_clients", false, Command::None,
     ",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:account-multiple\""},
    {"sensor", "stream1_packet_rate", "Stream 1 Packet Rate", nullptr, "stream1_packet_rate", false, Command::None,
     ",\"unit_of_meas\":\"pkt/s\",\"stat_cla\":\"measurement\""},
    {"sensor", "stream2_packet_rate", "Stream 2 Packet Rate", nullptr, "stream2_packet_rate", false, Command::None,
     ",\"unit_of_meas\":\"pkt/s\",\"stat_cla\":\"measurement\""},
    {"sensor", "stream1_url", "Stream 1 URL", nullptr, "stream1_url_ip", false, Command::None,
     ",\"ic\":\"mdi:link-variant\""},
    {"sensor", "stream2_url", "Stream 2 URL", nullptr, "stream2_url_ip", false, Command::None,
     ",\"ic\":\"mdi:link-variant\""},
    {"select", "stream1_target", "Stream 1 Target", nullptr, "stream1_target", false, Command::Stream1Target,
     ",\"options\":[\"BirdNET-Go\",\"BirdNET-Pi\"],\"ic\":\"mdi:target\""},
    {"select", "stream2_target", "Stream 2 Target", nullptr, "stream2_target", false, Command::Stream2Target,
     ",\"options\":[\"BirdNET-Go\",\"BirdNET-Pi\"],\"ic\":\"mdi:target\""},
    {"sensor", "sample_rate_hz", "Sample Rate", nullptr, "sample_rate", false, Command::None,
     ",\"unit_of_meas\":\"Hz\",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\""},
    {"sensor", "audio_format", "Audio Format", nullptr, "audio_format", false, Command::None,
     ",\"ent_cat\":\"diagnostic\""},
    {"button", "reboot", "Reboot Device", nullptr, nullptr, false, Command::Reboot,
     ",\"pl_prs\":\"PRESS\",\"ent_cat\":\"config\""},
};
static constexpr uint16_t entityCount = sizeof(entities) / sizeof(entities[0]);

// Supports Arduino String and std::string, so tests exercise this exact builder.
template<class Text>
Text makePayload(const Entity& entity, const Text& deviceId, const Text& stateTopic,
                 const Text& availabilityTopic, const Text& deviceJson, const Text& commandTopic) {
    Text json;
    json.reserve(320 + deviceJson.length());
    json += "{\"name\":\"";
    json += entity.name;
    json += "\",\"uniq_id\":\"";
    json += deviceId;
    json += "_";
    json += entity.uniqueSuffix ? entity.uniqueSuffix : entity.objectId;
    json += "\"";
    if (entity.stateKey) {
        json += ",\"stat_t\":\"";
        json += stateTopic;
        json += "\",\"val_tpl\":\"{{ ";
        if (entity.booleanState) json += "'ON' if ";
        json += "value_json.";
        json += entity.stateKey;
        if (entity.booleanState) json += " else 'OFF'";
        json += " }}\"";
        if (entity.booleanState) json += ",\"pl_on\":\"ON\",\"pl_off\":\"OFF\"";
    }
    if (entity.command != Command::None) {
        json += ",\"cmd_t\":\"";
        json += commandTopic;
        json += "\"";
    }
    json += entity.attributes;
    json += ",\"avty_t\":\"";
    json += availabilityTopic;
    json += "\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"dev\":";
    json += deviceJson;
    json += "}";
    return json;
}
} // namespace mqtt_discovery
