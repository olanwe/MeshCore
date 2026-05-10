#include "SerialWifiInterface.h"
#include <WiFi.h>

#ifndef WIFI_RECOVERY_SANITY_CHECK_INTERVAL
#define WIFI_RECOVERY_SANITY_CHECK_INTERVAL 60000
#endif
#ifndef WIFI_RECOVERY_SOFT_RECONNECT_DELAY
#define WIFI_RECOVERY_SOFT_RECONNECT_DELAY 15000
#endif
#ifndef WIFI_RECOVERY_HARD_RESET_DELAY
#define WIFI_RECOVERY_HARD_RESET_DELAY 60000
#endif
#ifndef WIFI_RECOVERY_HARD_RESET_INTERVAL
#define WIFI_RECOVERY_HARD_RESET_INTERVAL 60000
#endif
#ifndef WIFI_RECOVERY_OFF_TIME
#define WIFI_RECOVERY_OFF_TIME 100
#endif

SerialWifiInterface* SerialWifiInterface::_instance = NULL;

void SerialWifiInterface::begin(int port) {
  // wifi setup is handled outside of this class, only starts the server
  _port = port;
  startServer();
}

void SerialWifiInterface::begin(int port, const char* ssid, const char* password) {
  _port = port;
  _ssid = ssid;
  _password = password;
  _managed_wifi = true;
  _wifi_ready = false;
  _wifi_disconnected = false;
  _wifi_lost_ip = false;
  _wifi_got_ip = false;
  _last_wifi_check = millis() - WIFI_RECOVERY_SANITY_CHECK_INTERVAL;
  _wifi_issue_since = 0;
  _wifi_reconnect_done = false;
  _wifi_hard_reset_done = false;
  _wifi_reset_in_progress = false;

  _instance = this;
  if (!_wifi_events_registered) {
    WiFi.onEvent(onWiFiEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent(onWiFiEvent, ARDUINO_EVENT_WIFI_STA_LOST_IP);
    WiFi.onEvent(onWiFiEvent, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    _wifi_events_registered = true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(_ssid, _password);
}

void SerialWifiInterface::onWiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  (void)info;
  if (!_instance) return;

  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      _instance->_wifi_disconnected = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      _instance->_wifi_lost_ip = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      _instance->_wifi_got_ip = true;
      break;
    default:
      break;
  }
}

bool SerialWifiInterface::hasValidIP() const {
  IPAddress ip = WiFi.localIP();
  return ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0;
}

bool SerialWifiInterface::isWifiReady() const {
  return WiFi.status() == WL_CONNECTED && hasValidIP();
}

void SerialWifiInterface::stopClient() {
  if (deviceConnected) {
    WIFI_DEBUG_PRINTLN("Disconnected");
  }
  deviceConnected = false;
  client.stop();
  resetReceivedFrameHeader();
  clearBuffers();
}

void SerialWifiInterface::startServer() {
  if (_server_started || _port <= 0) return;
  server.begin(_port);
  _server_started = server;
}

void SerialWifiInterface::stopServer() {
  if (!_server_started) return;
  server.end();
  _server_started = false;
}

void SerialWifiInterface::reconnectWifi() {
  if (!_managed_wifi || !_ssid) return;

  WIFI_DEBUG_PRINTLN("SerialWifiInterface -> reconnecting WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (!WiFi.reconnect()) {
    WiFi.begin(_ssid, _password);
  }
}

void SerialWifiInterface::resetWifi() {
  if (!_managed_wifi || !_ssid || _wifi_reset_in_progress) return;

  WIFI_DEBUG_PRINTLN("SerialWifiInterface -> resetting WiFi");
  _wifi_ready = false;
  stopClient();
  stopServer();
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  _wifi_reset_restart_time = millis() + WIFI_RECOVERY_OFF_TIME;
  _wifi_reset_in_progress = true;
  _last_hard_reset = millis();
}

void SerialWifiInterface::checkWifiStatus() {
  if (!_managed_wifi || !_isEnabled) return;

  unsigned long now = millis();

  if (_wifi_reset_in_progress) {
    if (now < _wifi_reset_restart_time) return;

    WIFI_DEBUG_PRINTLN("SerialWifiInterface -> restarting WiFi");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(_ssid, _password);
    _wifi_reset_in_progress = false;
    return;
  }

  bool saw_event = _wifi_disconnected || _wifi_lost_ip || _wifi_got_ip;
  bool do_sanity_check = now >= _last_wifi_check + WIFI_RECOVERY_SANITY_CHECK_INTERVAL;
  if (!saw_event && _wifi_issue_since == 0 && !do_sanity_check) return;
  if (do_sanity_check) _last_wifi_check = now;

  if (_wifi_got_ip && isWifiReady()) {
    WIFI_DEBUG_PRINTLN("SerialWifiInterface -> WiFi got IP");
    _wifi_ready = true;
    _wifi_got_ip = false;
    _wifi_disconnected = false;
    _wifi_lost_ip = false;
    _wifi_issue_since = 0;
    _wifi_reconnect_done = false;
    _wifi_hard_reset_done = false;
    startServer();
    return;
  }

  if (isWifiReady()) {
    _wifi_ready = true;
    _wifi_disconnected = false;
    _wifi_lost_ip = false;
    _wifi_got_ip = false;
    _wifi_issue_since = 0;
    _wifi_reconnect_done = false;
    _wifi_hard_reset_done = false;
    startServer();
    return;
  }

  if (_wifi_issue_since == 0) {
    _wifi_issue_since = now;
    WIFI_DEBUG_PRINTLN("SerialWifiInterface -> WiFi unavailable");
  }

  _wifi_ready = false;
  stopClient();
  stopServer();

  if (!_wifi_reconnect_done && now >= _wifi_issue_since + WIFI_RECOVERY_SOFT_RECONNECT_DELAY) {
    reconnectWifi();
    _wifi_reconnect_done = true;
  }

  if (now >= _wifi_issue_since + WIFI_RECOVERY_HARD_RESET_DELAY
      && (!_wifi_hard_reset_done || now >= _last_hard_reset + WIFI_RECOVERY_HARD_RESET_INTERVAL)) {
    resetWifi();
    _wifi_hard_reset_done = true;
  }
}

// ---------- public methods
void SerialWifiInterface::enable() { 
  if (_isEnabled) return;

  _isEnabled = true;
  clearBuffers();
}

void SerialWifiInterface::disable() {
  _isEnabled = false;
  stopClient();
}

size_t SerialWifiInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    WIFI_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d\n", len);
    return 0;
  }

  if (deviceConnected && len > 0) {
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      WIFI_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }

    send_queue[send_queue_len].len = len;  // add to send queue
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;

    return len;
  }
  return 0;
}

bool SerialWifiInterface::isWriteBusy() const {
  return false;
}

bool SerialWifiInterface::hasReceivedFrameHeader() {
  return received_frame_header.type != 0 && received_frame_header.length != 0;
}

void SerialWifiInterface::resetReceivedFrameHeader() {
  received_frame_header.type = 0;
  received_frame_header.length = 0;
}

size_t SerialWifiInterface::checkRecvFrame(uint8_t dest[]) {
  checkWifiStatus();
  if (_managed_wifi && !_wifi_ready) {
    return 0;
  }

  // check if new client connected
  auto newClient = server.available();
  if (newClient) {

    // disconnect existing client
    deviceConnected = false;
    client.stop();

    // switch active connection to new client
    client = newClient;

    // forget received frame header
    resetReceivedFrameHeader();
    
  }

  if (client.connected()) {
    if (!deviceConnected) {
      WIFI_DEBUG_PRINTLN("Got connection");
      deviceConnected = true;
    }
  } else {
    if (deviceConnected) {
      deviceConnected = false;
      WIFI_DEBUG_PRINTLN("Disconnected");
    }
  }

  if (deviceConnected) {
    if (send_queue_len > 0) {   // first, check send queue
      
      _last_write = millis();
      int len = send_queue[0].len;

      uint8_t pkt[3+len]; // use same header as serial interface so client can delimit frames
      pkt[0] = '>';
      pkt[1] = (len & 0xFF);  // LSB
      pkt[2] = (len >> 8);    // MSB
      memcpy(&pkt[3], send_queue[0].buf, send_queue[0].len);
      client.write(pkt, 3 + len);
      send_queue_len--;
      for (int i = 0; i < send_queue_len; i++) {   // delete top item from queue
        send_queue[i] = send_queue[i + 1];
      }
    } else {

      // check if we are waiting for a frame header
      if(!hasReceivedFrameHeader()){

        // make sure we have received enough bytes for a frame header
        // 3 bytes frame header = (1 byte frame type) + (2 bytes frame length as unsigned 16-bit little endian)
        int frame_header_length = 3;
        if(client.available() >= frame_header_length){

          // read frame header
          client.readBytes(&received_frame_header.type, 1);
          client.readBytes((uint8_t*)&received_frame_header.length, 2);

        }

      }

      // check if we have received a frame header
      if(hasReceivedFrameHeader()){

        // make sure we have received enough bytes for the required frame length
        int available = client.available();
        int frame_type = received_frame_header.type;
        int frame_length = received_frame_header.length;
        if(frame_length > available){
          WIFI_DEBUG_PRINTLN("Waiting for %d more bytes", frame_length - available);
          return 0;
        }

        // skip frames that are larger than MAX_FRAME_SIZE
        if(frame_length > MAX_FRAME_SIZE){
          WIFI_DEBUG_PRINTLN("Skipping frame: length=%d is larger than MAX_FRAME_SIZE=%d", frame_length, MAX_FRAME_SIZE);
          while(frame_length > 0){
            uint8_t skip[1];
            int skipped = client.read(skip, 1);
            frame_length -= skipped;
          }
          resetReceivedFrameHeader();
          return 0;
        }

        // skip frames that are not expected type
        // '<' is 0x3c which indicates a frame sent from app to radio
        if(frame_type != '<'){
          WIFI_DEBUG_PRINTLN("Skipping frame: type=0x%x is unexpected", frame_type);
          while(frame_length > 0){
            uint8_t skip[1];
            int skipped = client.read(skip, 1);
            frame_length -= skipped;
          }
          resetReceivedFrameHeader();
          return 0;
        }

        // read frame data to provided buffer
        client.readBytes(dest, frame_length);

        // ready for next frame
        resetReceivedFrameHeader();
        return frame_length;

      }
      
    }
  }

  return 0;
}

bool SerialWifiInterface::isConnected() const {
  return deviceConnected;  //pServer != NULL && pServer->getConnectedCount() > 0;
}
