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
#define FOLLOWBOX_CAM_STATIC_IP "192.168.4.2"
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
uint32_t failed_captures = 0;
char last_error[96] = "booting";

IPAddress parseIp(const char* text, const IPAddress& fallback) {
  IPAddress ip;
  return ip.fromString(text) ? ip : fallback;
}

String streamUrl() {
  return String("http://") + WiFi.localIP().toString() + ":" +
         String(FOLLOWBOX_CAM_STREAM_PORT) + "/stream";
}

void logCameraConfig() {
  Serial.println();
  Serial.println("FollowBox ESP32-S3-CAM");
  Serial.printf("WiFi SSID: %s\n", FOLLOWBOX_CAM_WIFI_SSID);
  Serial.printf("Static IP: %s\n", FOLLOWBOX_CAM_STATIC_IP);
  Serial.printf("Stream: http://%s:%u/stream\n", FOLLOWBOX_CAM_STATIC_IP,
                FOLLOWBOX_CAM_STREAM_PORT);
  Serial.printf("Pins: xclk=%d siod=%d sioc=%d d0=%d d1=%d d2=%d d3=%d "
                "d4=%d d5=%d d6=%d d7=%d vsync=%d href=%d pclk=%d\n",
                CAM_PIN_XCLK, CAM_PIN_SIOD, CAM_PIN_SIOC, CAM_PIN_D0,
                CAM_PIN_D1, CAM_PIN_D2, CAM_PIN_D3, CAM_PIN_D4, CAM_PIN_D5,
                CAM_PIN_D6, CAM_PIN_D7, CAM_PIN_VSYNC, CAM_PIN_HREF,
                CAM_PIN_PCLK);
}

bool connectWifi() {
  const IPAddress local_ip =
      parseIp(FOLLOWBOX_CAM_STATIC_IP, IPAddress(192, 168, 4, 2));
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
  config.frame_size = psramFound() ? FRAMESIZE_SVGA : FRAMESIZE_VGA;
  config.jpeg_quality = 12;
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
    sensor->set_framesize(sensor, config.frame_size);
    sensor->set_quality(sensor, config.jpeg_quality);
  }

  Serial.printf("Camera ready: psram=%s frame=%s\n",
                psramFound() ? "yes" : "no",
                psramFound() ? "SVGA" : "VGA");
  snprintf(last_error, sizeof(last_error), "ok");
  return true;
}

esp_err_t rootHandler(httpd_req_t* req) {
  const String body = String("FollowBox camera online\n") +
                      "stream=" + streamUrl() + "\n" +
                      "status=http://" + WiFi.localIP().toString() + "/status\n" +
                      "capture=http://" + WiFi.localIP().toString() + "/capture\n";
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, body.c_str(), body.length());
}

esp_err_t statusHandler(httpd_req_t* req) {
  const String body = String("{\"ok\":true,\"ip\":\"") +
                      WiFi.localIP().toString() + "\",\"stream_url\":\"" +
                      streamUrl() + "\",\"rssi\":" + String(WiFi.RSSI()) +
                      ",\"psram\":" + (psramFound() ? "true" : "false") +
                      ",\"camera_ready\":" +
                      (camera_ready ? "true" : "false") +
                      ",\"failed_captures\":" + String(failed_captures) +
                      ",\"last_error\":\"" + String(last_error) + "\"}";
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

  camera_fb_t* fb = esp_camera_fb_get();
  if (fb == nullptr) {
    failed_captures++;
    snprintf(last_error, sizeof(last_error), "camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
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

  while (true) {
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

    esp_camera_fb_return(fb);
    if (res != ESP_OK) {
      break;
    }
    delay(10);
  }

  return res;
}

void startHttpServers() {
  httpd_config_t status_config = HTTPD_DEFAULT_CONFIG();
  status_config.server_port = 80;
  status_config.ctrl_port = 32768;
  status_config.max_uri_handlers = 4;

  if (httpd_start(&status_server, &status_config) == ESP_OK) {
    httpd_uri_t root_uri = {};
    root_uri.uri = "/";
    root_uri.method = HTTP_GET;
    root_uri.handler = rootHandler;
    httpd_register_uri_handler(status_server, &root_uri);

    httpd_uri_t status_uri = {};
    status_uri.uri = "/status";
    status_uri.method = HTTP_GET;
    status_uri.handler = statusHandler;
    httpd_register_uri_handler(status_server, &status_uri);

    httpd_uri_t capture_uri = {};
    capture_uri.uri = "/capture";
    capture_uri.method = HTTP_GET;
    capture_uri.handler = captureHandler;
    httpd_register_uri_handler(status_server, &capture_uri);
  }

  httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
  stream_config.server_port = FOLLOWBOX_CAM_STREAM_PORT;
  stream_config.ctrl_port = 32769;
  stream_config.max_uri_handlers = 1;

  if (httpd_start(&stream_server, &stream_config) == ESP_OK) {
    httpd_uri_t stream_uri = {};
    stream_uri.uri = "/stream";
    stream_uri.method = HTTP_GET;
    stream_uri.handler = streamHandler;
    httpd_register_uri_handler(stream_server, &stream_uri);
  }

  Serial.printf("HTTP status: http://%s/\n", WiFi.localIP().toString().c_str());
  Serial.printf("MJPEG stream: %s\n", streamUrl().c_str());
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
  camera_ready = startCamera();
  if (!camera_ready) {
    Serial.println("HTTP status server will stay online for diagnostics");
  }
  startHttpServers();
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
    Serial.printf("CAM online ip=%s rssi=%d camera=%s stream=%s error=%s\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                  camera_ready ? "ready" : "not-ready", streamUrl().c_str(),
                  last_error);
  }
  delay(100);
}
