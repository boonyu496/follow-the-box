#pragma once

namespace followbox::camera_config {

// ESP32-S3-CAM runs as a separate WiFi video module. The recommended first
// version is: camera joins the FollowBox softAP as STA with static IP
// 192.168.4.10 and serves MJPEG on the normal HTTP port. Keep this outside the
// first DHCP leases used by phones/laptops on the FollowBox softAP.
#ifndef FOLLOWBOX_CAMERA_STREAM_URL
#define FOLLOWBOX_CAMERA_STREAM_URL "http://192.168.4.10/stream"
#endif

constexpr char STREAM_URL[] = FOLLOWBOX_CAMERA_STREAM_URL;

}  // namespace followbox::camera_config
