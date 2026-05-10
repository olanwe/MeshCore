#pragma once

#include "../BaseSerialInterface.h"
#include <WiFi.h>

class SerialWifiInterface : public BaseSerialInterface {
  static SerialWifiInterface* _instance;

  bool deviceConnected;
  bool _isEnabled;
  bool _managed_wifi;
  bool _server_started;
  bool _wifi_reconnect_done;
  bool _wifi_hard_reset_done;
  bool _wifi_reset_in_progress;
  bool _wifi_events_registered;
  unsigned long _last_write;
  unsigned long _last_wifi_check;
  unsigned long _wifi_issue_since;
  unsigned long _wifi_reset_restart_time;
  unsigned long _last_hard_reset;
  int _port;
  const char* _ssid;
  const char* _password;
  volatile bool _wifi_disconnected;
  volatile bool _wifi_lost_ip;
  volatile bool _wifi_got_ip;
  volatile unsigned long _last_wifi_event;

  WiFiServer server;
  WiFiClient client;

  struct FrameHeader {
    uint8_t type;
    uint16_t length;
  };

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  FrameHeader received_frame_header;

  #define FRAME_QUEUE_SIZE  4
  int recv_queue_len;
  Frame recv_queue[FRAME_QUEUE_SIZE];
  int send_queue_len;
  Frame send_queue[FRAME_QUEUE_SIZE];

  void clearBuffers() { recv_queue_len = 0; send_queue_len = 0; }
  static void onWiFiEvent(arduino_event_id_t event, arduino_event_info_t info);
  void checkWifiStatus();
  void reconnectWifi();
  void resetWifi();
  bool hasValidIP() const;
  bool isWifiReady() const;
  void stopClient();
  void startServer();
  void stopServer();

protected:

public:
  SerialWifiInterface() : server(WiFiServer()), client(WiFiClient()) {
    deviceConnected = false;
    _isEnabled = false;
    _managed_wifi = false;
    _server_started = false;
    _wifi_reconnect_done = false;
    _wifi_hard_reset_done = false;
    _wifi_reset_in_progress = false;
    _wifi_events_registered = false;
    _last_write = 0;
    _last_wifi_check = 0;
    _wifi_issue_since = 0;
    _wifi_reset_restart_time = 0;
    _last_hard_reset = 0;
    _port = 0;
    _ssid = NULL;
    _password = NULL;
    _wifi_disconnected = false;
    _wifi_lost_ip = false;
    _wifi_got_ip = false;
    _last_wifi_event = 0;
    send_queue_len = recv_queue_len = 0;
    received_frame_header.type = 0;
    received_frame_header.length = 0;
  }

  void begin(int port);
  void begin(int port, const char* ssid, const char* password);

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;
  bool isWriteBusy() const override;

  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;

  bool hasReceivedFrameHeader();
  void resetReceivedFrameHeader();
};

#if WIFI_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define WIFI_DEBUG_PRINT(F, ...) Serial.printf("WiFi: " F, ##__VA_ARGS__)
  #define WIFI_DEBUG_PRINTLN(F, ...) Serial.printf("WiFi: " F "\n", ##__VA_ARGS__)
#else
  #define WIFI_DEBUG_PRINT(...) {}
  #define WIFI_DEBUG_PRINTLN(...) {}
#endif
