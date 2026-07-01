#include "web/h5_control_routes.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "web/h5_http_common.h"
#include "web/h5_request_parser.h"

namespace followbox {

void registerH5ControlRoutes(AsyncWebServer& server, H5CommandHandler& handler,
                             portMUX_TYPE& handler_mux,
                             ProfileStore* profile_store,
                             CalibrationStore* calibration_store) {
  H5CommandHandler* const handler_ptr = &handler;
  portMUX_TYPE* const mux_ptr = &handler_mux;

  // POST /api/jog -> low-speed jog (deadman gated, replay protected downstream).
  server.on(
      "/api/jog", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
      [handler_ptr, mux_ptr](AsyncWebServerRequest* request, uint8_t* data,
                             size_t len, size_t index, size_t total) {
        onH5Body(request, data, len, index, total,
                 [handler_ptr, mux_ptr](AsyncWebServerRequest* req,
                                        const char* body, size_t length) {
                   if (!requireLocalApiAuth(req)) {
                     return;
                   }
                   JogRequest jog = parseJogRequest(body, length);
                   if (!jog.valid) {
                     req->send(400, "application/json", kAckRejected);
                     return;
                   }
                   const uint32_t now = millis();
                   portENTER_CRITICAL(mux_ptr);
                   const bool applied = handler_ptr->onJog(
                       jog.seq, jog.forward, jog.turn, jog.deadman, now);
                   portEXIT_CRITICAL(mux_ptr);
                   req->send(200, "application/json",
                             applied ? kAckOk : kAckRejected);
                 });
      });

  // POST /api/mode-request -> mode request (mode_manager still validates).
  server.on(
      "/api/mode-request", HTTP_POST,
      [](AsyncWebServerRequest* request) {}, nullptr,
      [handler_ptr, mux_ptr](AsyncWebServerRequest* request, uint8_t* data,
                             size_t len, size_t index, size_t total) {
        onH5Body(request, data, len, index, total,
                 [handler_ptr, mux_ptr](AsyncWebServerRequest* req,
                                        const char* body, size_t length) {
                   if (!requireLocalApiAuth(req)) {
                     return;
                   }
                   H5ModeRequest mode = parseModeRequest(body, length);
                   const uint32_t now = millis();
                   portENTER_CRITICAL(mux_ptr);
                   handler_ptr->onModeRequest(mode, now);
                   portEXIT_CRITICAL(mux_ptr);
                   req->send(200, "application/json", kAckOk);
                 });
      });

  // POST /api/reset-fault -> software fault-reset request.
  server.on(
      "/api/reset-fault", HTTP_POST,
      [](AsyncWebServerRequest* request) {}, nullptr,
      [handler_ptr, mux_ptr](AsyncWebServerRequest* request, uint8_t* data,
                             size_t len, size_t index, size_t total) {
        onH5Body(request, data, len, index, total,
                 [handler_ptr, mux_ptr](AsyncWebServerRequest* req,
                                        const char* body, size_t length) {
                   if (!requireLocalApiAuth(req)) {
                     return;
                   }
                   const bool confirm = parseResetFaultRequest(body, length);
                   const uint32_t now = millis();
                   portENTER_CRITICAL(mux_ptr);
                   const bool applied =
                       handler_ptr->onResetFault(confirm, now);
                   portEXIT_CRITICAL(mux_ptr);
                   req->send(200, "application/json",
                             applied ? kAckOk : kAckRejected);
                 });
      });

  // POST /api/calibrate -> save throttle calibration to NVS and update control loop.
  server.on(
      "/api/calibrate", HTTP_POST,
      [](AsyncWebServerRequest* request) {}, nullptr,
      [handler_ptr, mux_ptr, calibration_store](
          AsyncWebServerRequest* request, uint8_t* data, size_t len,
          size_t index, size_t total) {
        onH5Body(request, data, len, index, total,
                 [handler_ptr, mux_ptr, calibration_store](
                     AsyncWebServerRequest* req, const char* body,
                     size_t length) {
                   if (!requireLocalApiAuth(req)) {
                     return;
                   }
                   CalibrateRequest cal_req =
                       parseCalibrateRequest(body, length);
                   if (!cal_req.valid || calibration_store == nullptr) {
                     req->send(400, "application/json", kAckRejected);
                     return;
                   }
                   ThrottleCalibration cal;
                   cal.deadband_mv = static_cast<int>(cal_req.deadband_mv);
                   cal.min_active_mv = static_cast<int>(cal_req.min_active_mv);
                   cal.max_mv = static_cast<int>(cal_req.max_mv);
                   cal.module_full_scale_mv =
                       static_cast<int>(cal_req.module_full_scale_mv);
                   cal.rise_mv_per_s = static_cast<int>(cal_req.rise_mv_per_s);
                   cal.fall_mv_per_s = static_cast<int>(cal_req.fall_mv_per_s);

                   const bool saved = calibration_store->save(cal);

                   const uint32_t now = millis();
                   portENTER_CRITICAL(mux_ptr);
                   const bool applied = handler_ptr->onCalibrate(cal, now);
                   portEXIT_CRITICAL(mux_ptr);

                   req->send(200, "application/json",
                             (saved && applied) ? kAckOk : kAckRejected);
                 });
      });

  // POST /api/wizard-complete -> mark install wizard complete in NVS and the control loop.
  server.on(
      "/api/wizard-complete", HTTP_POST,
      [](AsyncWebServerRequest* request) {}, nullptr,
      [handler_ptr, mux_ptr, profile_store](
          AsyncWebServerRequest* request, uint8_t* data, size_t len,
          size_t index, size_t total) {
        onH5Body(request, data, len, index, total,
                 [handler_ptr, mux_ptr, profile_store](
                     AsyncWebServerRequest* req, const char* body,
                     size_t length) {
                   if (!requireLocalApiAuth(req)) {
                     return;
                   }
                   WizardRequest wiz_req = parseWizardRequest(body, length);
                   if (!wiz_req.valid || profile_store == nullptr) {
                     req->send(400, "application/json", kAckRejected);
                     return;
                   }
                   RuntimeProfile prof;
                   prof.install_wizard_complete = wiz_req.complete;

                   const bool saved = profile_store->save(prof);

                   const uint32_t now = millis();
                   portENTER_CRITICAL(mux_ptr);
                   const bool applied =
                       handler_ptr->onWizardComplete(wiz_req.complete, now);
                   portEXIT_CRITICAL(mux_ptr);

                   req->send(200, "application/json",
                             (saved && applied) ? kAckOk : kAckRejected);
                 });
      });
}

}  // namespace followbox

