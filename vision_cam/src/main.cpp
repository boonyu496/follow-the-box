#include <Arduino.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_http_server.h>

#ifndef FOLLOWBOX_CAM_WIFI_SSID
#define FOLLOWBOX_CAM_WIFI_SSID "FollowBox"
#endif

#ifndef FOLLOWBOX_CAM_WIFI_PASS
#define FOLLOWBOX_CAM_WIFI_PASS "followbox123"
#endif

#ifndef FOLLOWBOX_CAM_STATIC_IP
#define FOLLOWBOX_CAM_STATIC_IP "192.168.4.10"
#endif

#ifndef FOLLOWBOX_CAM_GATEWAY
#define FOLLOWBOX_CAM_GATEWAY "192.168.4.1"
#endif

#ifndef FOLLOWBOX_CAM_NETMASK
#define FOLLOWBOX_CAM_NETMASK "255.255.255.0"
#endif

#ifndef FOLLOWBOX_CAM_STREAM_PORT
#define FOLLOWBOX_CAM_STREAM_PORT 81
#endif

// Default pin map for common ESP32-S3-WROOM-1-N16R8 CAM boards with OV5640.
// Override any pin from platformio.ini build_flags if your CAM carrier differs.
#ifndef CAM_PIN_PWDN
#define CAM_PIN_PWDN -1
#endif
#ifndef CAM_PIN_RESET
#define CAM_PIN_RESET -1
#endif
#ifndef CAM_PIN_XCLK
#define CAM_PIN_XCLK 15
#endif
#ifndef CAM_PIN_SIOD
#define CAM_PIN_SIOD 4
#endif
#ifndef CAM_PIN_SIOC
#define CAM_PIN_SIOC 5
#endif
#ifndef CAM_PIN_D7
#define CAM_PIN_D7 16
#endif
#ifndef CAM_PIN_D6
#define CAM_PIN_D6 17
#endif
#ifndef CAM_PIN_D5
#define CAM_PIN_D5 18
#endif
#ifndef CAM_PIN_D4
#define CAM_PIN_D4 12
#endif
#ifndef CAM_PIN_D3
#define CAM_PIN_D3 10
#endif
#ifndef CAM_PIN_D2
#define CAM_PIN_D2 8
#endif
#ifndef CAM_PIN_D1
#define CAM_PIN_D1 9
#endif
#ifndef CAM_PIN_D0
#define CAM_PIN_D0 11
#endif
#ifndef CAM_PIN_VSYNC
#define CAM_PIN_VSYNC 6
#endif
#ifndef CAM_PIN_HREF
#define CAM_PIN_HREF 7
#endif
#ifndef CAM_PIN_PCLK
#define CAM_PIN_PCLK 13
#endif
#ifndef CAM_XCLK_HZ
#define CAM_XCLK_HZ 20000000
#endif
#ifndef CAM_FRAME_SIZE
#define CAM_FRAME_SIZE FRAMESIZE_SVGA
#endif
#ifndef CAM_JPEG_QUALITY
#define CAM_JPEG_QUALITY 12
#endif

namespace {

constexpr char kBoundary[] = "123456789000000000000987654321";
constexpr char kStreamType[] =
    "multipart/x-mixed-replace;boundary=123456789000000000000987654321";
constexpr char kStreamBoundary[] = "\r\n--123456789000000000000987654321\r\n";
constexpr char kStreamPart[] = "Content-Type: image/jpeg\r\n"
                               "Content-Length: %u\r\n\r\n";

httpd_handle_t stream_server = nullptr;
httpd_handle_t status_server = nullptr;
bool camera_ready = false;
bool status_server_ready = false;
bool stream_server_ready = false;
uint16_t camera_sensor_pid = 0;
uint32_t capture_attempts = 0;
uint32_t failed_captures = 0;
uint32_t successful_captures = 0;
uint32_t stream_clients = 0;
uint32_t stream_frames = 0;
uint32_t last_frame_bytes = 0;
uint32_t last_capture_ms = 0;
char last_error[96] = "booting";
char http_error[96] = "not started";
char camera_sensor_name[16] = "unknown";

IPAddress parseIp(const char* text, const IPAddress& fallback) {
  IPAddress ip;
  return ip.fromString(text) ? ip : fallback;
}

String streamUrl() {
  return String("http://") + WiFi.localIP().toString() + "/stream";
}

String legacyStreamUrl() {
  return String("http://") + WiFi.localIP().toString() + ":" +
         String(FOLLOWBOX_CAM_STREAM_PORT) + "/stream";
}

const char* frameSizeName(framesize_t frame_size) {
  switch (frame_size) {
    case FRAMESIZE_QVGA:
      return "QVGA";
    case FRAMESIZE_VGA:
      return "VGA";
    case FRAMESIZE_SVGA:
      return "SVGA";
    case FRAMESIZE_XGA:
      return "XGA";
    case FRAMESIZE_UXGA:
      return "UXGA";
    default:
      return "custom";
  }
}

const char* sensorPidName(uint16_t pid) {
  switch (pid) {
#ifdef OV2640_PID
    case OV2640_PID:
      return "OV2640";
#endif
#ifdef OV3660_PID
    case OV3660_PID:
      return "OV3660";
#endif
#ifdef OV5640_PID
    case OV5640_PID:
      return "OV5640";
#endif
    default:
      return "unknown";
  }
}

void logCameraConfig() {
  Serial.println();
  Serial.println("FollowBox ESP32-S3-CAM");
  Serial.printf("WiFi SSID: %s\n", FOLLOWBOX_CAM_WIFI_SSID);
  Serial.printf("Static IP: %s\n", FOLLOWBOX_CAM_STATIC_IP);
  Serial.printf("Stream: http://%s/stream\n", FOLLOWBOX_CAM_STATIC_IP);
  Serial.printf("Legacy stream: http://%s:%u/stream\n",
                FOLLOWBOX_CAM_STATIC_IP, FOLLOWBOX_CAM_STREAM_PORT);
  Serial.printf("Pins: xclk=%d siod=%d sioc=%d d0=%d d1=%d d2=%d d3=%d "
                "d4=%d d5=%d d6=%d d7=%d vsync=%d href=%d pclk=%d\n",
                CAM_PIN_XCLK, CAM_PIN_SIOD, CAM_PIN_SIOC, CAM_PIN_D0,
                CAM_PIN_D1, CAM_PIN_D2, CAM_PIN_D3, CAM_PIN_D4, CAM_PIN_D5,
                CAM_PIN_D6, CAM_PIN_D7, CAM_PIN_VSYNC, CAM_PIN_HREF,
                CAM_PIN_PCLK);
  Serial.printf("Format: %s JPEG quality=%d xclk=%uHz\n",
                frameSizeName(static_cast<framesize_t>(CAM_FRAME_SIZE)),
                CAM_JPEG_QUALITY, CAM_XCLK_HZ);
}

bool connectWifi() {
  const IPAddress local_ip =
      parseIp(FOLLOWBOX_CAM_STATIC_IP, IPAddress(192, 168, 4, 10));
  const IPAddress gateway =
      parseIp(FOLLOWBOX_CAM_GATEWAY, IPAddress(192, 168, 4, 1));
  const IPAddress subnet =
      parseIp(FOLLOWBOX_CAM_NETMASK, IPAddress(255, 255, 255, 0));

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  if (!WiFi.config(local_ip, gateway, subnet)) {
    Serial.println("WiFi static IP config failed");
  }
  WiFi.begin(FOLLOWBOX_CAM_WIFI_SSID, FOLLOWBOX_CAM_WIFI_PASS);

  Serial.print("Joining FollowBox AP");
  const uint32_t deadline_ms = millis() + 20000;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline_ms) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi join failed; reboot or check FollowBox AP/password");
    return false;
  }

  Serial.printf("WiFi connected: %s RSSI=%d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

bool startCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_PIN_D0;
  config.pin_d1 = CAM_PIN_D1;
  config.pin_d2 = CAM_PIN_D2;
  config.pin_d3 = CAM_PIN_D3;
  config.pin_d4 = CAM_PIN_D4;
  config.pin_d5 = CAM_PIN_D5;
  config.pin_d6 = CAM_PIN_D6;
  config.pin_d7 = CAM_PIN_D7;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_pclk = CAM_PIN_PCLK;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.xclk_freq_hz = CAM_XCLK_HZ;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = static_cast<framesize_t>(CAM_FRAME_SIZE);
  config.jpeg_quality = CAM_JPEG_QUALITY;
  config.fb_count = psramFound() ? 2 : 1;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    snprintf(last_error, sizeof(last_error), "camera init failed: 0x%x", err);
    Serial.println(last_error);
    return false;
  }

  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor != nullptr) {
    camera_sensor_pid = sensor->id.PID;
    std::snprintf(camera_sensor_name, sizeof(camera_sensor_name), "%s",
                  sensorPidName(camera_sensor_pid));
    sensor->set_framesize(sensor, config.frame_size);
    sensor->set_quality(sensor, config.jpeg_quality);
  }

  Serial.printf("Camera ready: sensor=%s pid=0x%04x psram=%s frame=%s\n",
                camera_sensor_name,
                camera_sensor_pid,
                psramFound() ? "yes" : "no",
                frameSizeName(config.frame_size));
  snprintf(last_error, sizeof(last_error), "ok");
  return true;
}

esp_err_t rootHandler(httpd_req_t* req) {
  const String body = String("FollowBox camera online\n") +
                      "sensor=" + String(camera_sensor_name) + "\n" +
                      "sensor_pid=0x" + String(camera_sensor_pid, HEX) + "\n" +
                      "stream=" + streamUrl() + "\n" +
                      "legacy_stream=" + legacyStreamUrl() + "\n" +
                      "status=http://" + WiFi.localIP().toString() + "/status\n" +
                      "capture=http://" + WiFi.localIP().toString() + "/capture\n";
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, body.c_str(), body.length());
}

esp_err_t statusHandler(httpd_req_t* req) {
  const String body = String("{\"ok\":true,\"ip\":\"") +
                      WiFi.localIP().toString() + "\",\"stream_url\":\"" +
                      streamUrl() + "\",\"legacy_stream_url\":\"" +
                      legacyStreamUrl() + "\",\"rssi\":" +
                      String(WiFi.RSSI()) +
                      ",\"sensor\":\"" + String(camera_sensor_name) + "\"" +
                      ",\"sensor_pid\":\"0x" +
                      String(camera_sensor_pid, HEX) + "\"" +
                      ",\"frame_size\":\"" +
                      String(frameSizeName(static_cast<framesize_t>(
                          CAM_FRAME_SIZE))) +
                      "\"" +
                      ",\"psram\":" + (psramFound() ? "true" : "false") +
                      ",\"camera_ready\":" +
                      (camera_ready ? "true" : "false") +
                      ",\"status_server_ready\":" +
                      (status_server_ready ? "true" : "false") +
                      ",\"stream_server_ready\":" +
                      (stream_server_ready ? "true" : "false") +
                      ",\"capture_attempts\":" + String(capture_attempts) +
                      ",\"failed_captures\":" + String(failed_captures) +
                      ",\"successful_captures\":" +
                      String(successful_captures) +
                      ",\"stream_clients\":" + String(stream_clients) +
                      ",\"stream_frames\":" + String(stream_frames) +
                      ",\"last_frame_bytes\":" + String(last_frame_bytes) +
                      ",\"last_capture_ms\":" + String(last_capture_ms) +
                      ",\"last_error\":\"" + String(last_error) + "\"" +
                      ",\"http_error\":\"" + String(http_error) + "\"}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, body.c_str(), body.length());
}

esp_err_t captureHandler(httpd_req_t* req) {
  if (!camera_ready) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, last_error);
  }

  capture_attempts++;
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb == nullptr) {
    failed_captures++;
    snprintf(last_error, sizeof(last_error), "camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  successful_captures++;
  last_frame_bytes = static_cast<uint32_t>(fb->len);
  last_capture_ms = millis();
  snprintf(last_error, sizeof(last_error), "ok");
  const esp_err_t res =
      httpd_resp_send(req, reinterpret_cast<const char*>(fb->buf), fb->len);
  esp_camera_fb_return(fb);
  return res;
}

esp_err_t streamHandler(httpd_req_t* req) {
  if (!camera_ready) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, last_error);
  }

  char part_buf[96];
  esp_err_t res = httpd_resp_set_type(req, kStreamType);
  if (res != ESP_OK) {
    return res;
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "12");

  stream_clients++;
  while (true) {
    capture_attempts++;
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb == nullptr) {
      failed_captures++;
      snprintf(last_error, sizeof(last_error), "camera capture failed");
      Serial.println(last_error);
      return ESP_FAIL;
    }

    res = httpd_resp_send_chunk(req, kStreamBoundary, strlen(kStreamBoundary));
    if (res == ESP_OK) {
      const size_t header_len =
          snprintf(part_buf, sizeof(part_buf), kStreamPart,
                   static_cast<unsigned>(fb->len));
      res = httpd_resp_send_chunk(req, part_buf, header_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(
          req, reinterpret_cast<const char*>(fb->buf), fb->len);
    }

    if (res == ESP_OK) {
      successful_captures++;
      stream_frames++;
      last_frame_bytes = static_cast<uint32_t>(fb->len);
      last_capture_ms = millis();
      snprintf(last_error, sizeof(last_error), "ok");
    }
    esp_camera_fb_return(fb);
    if (res != ESP_OK) {
      break;
    }
    delay(10);
  }

  return res;
}

void recordHttpError(const char* label, esp_err_t err) {
  snprintf(http_error, sizeof(http_error), "%s failed: 0x%x", label, err);
  Serial.println(http_error);
}

bool registerGetHandler(httpd_handle_t server, const char* uri,
                        esp_err_t (*handler)(httpd_req_t*),
                        const char* label) {
  httpd_uri_t handler_config = {};
  handler_config.uri = uri;
  handler_config.method = HTTP_GET;
  handler_config.handler = handler;
  const esp_err_t err = httpd_register_uri_handler(server, &handler_config);
  if (err != ESP_OK) {
    recordHttpError(label, err);
    return false;
  }
  return true;
}

bool startStatusServer() {
  if (status_server != nullptr) {
    status_server_ready = true;
    return true;
  }

  status_server_ready = false;
  httpd_config_t status_config = HTTPD_DEFAULT_CONFIG();
  status_config.server_port = 80;
  status_config.ctrl_port = 32768;
  status_config.max_uri_handlers = 4;

  esp_err_t err = httpd_start(&status_server, &status_config);
  if (err != ESP_OK) {
    recordHttpError("status http start", err);
    status_server = nullptr;
    return false;
  }

  if (!registerGetHandler(status_server, "/", rootHandler, "root handler") ||
      !registerGetHandler(status_server, "/status", statusHandler,
                          "status handler") ||
      !registerGetHandler(status_server, "/capture", captureHandler,
                          "capture handler") ||
      !registerGetHandler(status_server, "/stream", streamHandler,
                          "stream handler")) {
    httpd_stop(status_server);
    status_server = nullptr;
    status_server_ready = false;
    return false;
  }

  status_server_ready = true;
  return true;
}

bool startStreamServer() {
  if (stream_server != nullptr) {
    stream_server_ready = true;
    return true;
  }

  stream_server_ready = false;
  httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
  stream_config.server_port = FOLLOWBOX_CAM_STREAM_PORT;
  stream_config.ctrl_port = 32769;
  stream_config.max_uri_handlers = 1;

  esp_err_t err = httpd_start(&stream_server, &stream_config);
  if (err != ESP_OK) {
    recordHttpError("stream http start", err);
    stream_server = nullptr;
    return false;
  }

  if (!registerGetHandler(stream_server, "/stream", streamHandler,
                          "stream handler")) {
    httpd_stop(stream_server);
    stream_server = nullptr;
    stream_server_ready = false;
    return false;
  }

  stream_server_ready = true;
  return true;
}

void startHttpServers() {
  const bool status_ok = startStatusServer();
  const bool stream_ok = startStreamServer();
  if (status_ok && stream_ok) {
    snprintf(http_error, sizeof(http_error), "ok");
  }

  Serial.printf("HTTP status: %s http://%s/\n", status_ok ? "ready" : "down",
                WiFi.localIP().toString().c_str());
  Serial.printf("MJPEG stream: %s %s\n", status_ok ? "ready" : "down",
                streamUrl().c_str());
  Serial.printf("Legacy MJPEG stream: %s %s\n",
                stream_ok ? "ready" : "down", legacyStreamUrl().c_str());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  const uint32_t serial_deadline_ms = millis() + 3000;
  while (!Serial && millis() < serial_deadline_ms) {
    delay(10);
  }
  delay(300);
  logCameraConfig();

  if (!connectWifi()) {
    Serial.println("WiFi setup stopped");
    return;
  }
  startHttpServers();
  camera_ready = startCamera();
  if (!camera_ready) {
    Serial.println("HTTP status server will stay online for diagnostics");
  }
}

void loop() {
  static uint32_t last_log_ms = 0;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected; reconnecting");
    WiFi.reconnect();
    delay(1000);
    return;
  }

  const uint32_t now_ms = millis();
  if (now_ms - last_log_ms >= 10000) {
    last_log_ms = now_ms;
    if (!status_server_ready || !stream_server_ready) {
      startHttpServers();
    }
    Serial.printf("CAM online ip=%s rssi=%d camera=%s status_http=%s "
                  "stream_http=%s stream=%s error=%s http=%s\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                  camera_ready ? "ready" : "not-ready",
                  status_server_ready ? "ready" : "down",
                  stream_server_ready ? "ready" : "down", streamUrl().c_str(),
                  last_error, http_error);
    Serial.printf("CAM diag sensor=%s pid=0x%04x attempts=%lu ok=%lu "
                  "fail=%lu stream_clients=%lu stream_frames=%lu "
                  "last_frame=%luB last_capture_ms=%lu legacy=%s\n",
                  camera_sensor_name, camera_sensor_pid,
                  static_cast<unsigned long>(capture_attempts),
                  static_cast<unsigned long>(successful_captures),
                  static_cast<unsigned long>(failed_captures),
                  static_cast<unsigned long>(stream_clients),
                  static_cast<unsigned long>(stream_frames),
                  static_cast<unsigned long>(last_frame_bytes),
                  static_cast<unsigned long>(last_capture_ms),
                  legacyStreamUrl().c_str());
  }
  delay(100);
}
