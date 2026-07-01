# 激光雷达资料 (LiDAR Reference)

> 用于自动跟随车盒子项目
> 目录: D:\car\Follow the box\zhiliao

---

## 已收录资料

### 1. LD06 开发手册 (中文)
- **文件**: `LDROBOT_LD06_Development_manual.pdf` (约1MB)
- **来源**: 乐动机器人 (LDROBOT) 官方
- **内容**: DTOF激光雷达LD06的通信协议(UART@230400)、数据包格式、ROS SDK使用说明
- **关键参数**: 测距12m, 360°扫描, 每秒4500次测距

### 2. LD19 数据手册 (英文)
- **文件**: `LDROBOT_LD19_Datasheet_EN_v2.6.pdf` (约744KB)
- **来源**: ldrobot.com 官方
- **内容**: 电气参数(5V/180mA)、UART @ 230400、PWM调速(20-50KHz)、机械尺寸
- **关键参数**: 测距12m, DTOF技术, 360°扫描

### 3. LD19 开发手册 (中文)
- **文件**: `LD19_Development_Manual_V2.3.pdf` (约1.4MB)
- **来源**: Elecrow
- **内容**: 通信协议详解、ROS1/ROS2 SDK、STM32示例、点云数据处理

---

## 待补充 (百度网盘中的文件)

- **网盘链接**: https://pan.baidu.com/s/1jFjO9A8WChfBcY_gJuX5aw
- **提取码**: 8017
- **大小**: 55.8MB 压缩包
- **状态**: 需登录百度账号下载（需要安全验证）
- **解决方式**: 在 Windows 上用百度网盘客户端登录下载后放到此目录

---

## 在线参考资源 (自动跟随相关)

### LDROBOT官方资料
- LD06/LD19 ROS驱动: https://github.com/ldrobotSensorTeam/ldlidar_stl_ros
- LD06/LD19 ROS2驱动: https://github.com/ldrobotSensorTeam/ldlidar_stl_ros2
- 乐动机器人官网: https://www.ldrobot.com

### ESP32 + LiDAR 跟随方案
- ESP32 port for RPLIDAR: https://github.com/GuchiEg/rplidar_sdk_arduino
- Arduino LiDAR 库 (YDLIDAR/LDROBOT): https://github.com/kaiaai/LDS
- Elektor ESP32 + LiDAR 自主车: https://github.com/ClemensAtElektor/Lidar-controlled-autonomous-vehicle
- 人体跟随 Robot (ROS + LiDAR): https://macstepien.github.io/projects/human-following-robot

### 思岚 RPLIDAR
- ROS 使用 RPLIDAR 教程: https://www.ncnynl.com/archives/201611/1100.html
- RPLIDAR 全系列兼容 ROS: https://www.slamtec.com/cn/news/detail/469

### UWB + 激光雷达融合
- 基于UWB与激光雷达的机器人跟随和避障方法 (专利): https://patents.google.com/patent/CN114625122A/zh

---

*最后更新: 2026-05-30*
