# FollowBox 上线修复执行文档 - 2026-06-13

## 目标

把当前 FollowBox 固件从“可构建/可演示”推进到“允许首烧、允许低风险架空验证、具备后续 OTA 升级基础”的状态。实车带动力测试必须在安全门控证据齐全后执行；未通过前只允许 USB 首烧、LittleFS 烧录、串口日志、H5/API、云端链路和架空低速验证。

## 修复边界

- 保持所有运动输出路径不变：`App::tick()` -> `safety_manager.applyFinalGate()` -> `drive_adapter_analog_bldc`。
- H5 只允许请求低速点动、模式请求、配网、标定和安装向导，不允许直接设置 PWM、清除急停、绕过安装向导。
- 首次烧录使用 USB；首烧后才使用 OTA。OTA 期间必须强制电机输出安全。
- UWB 协议未抓包前不新增或编造 parser。
- IMU UART0 只补 HAL 兼容，不默认启用；启用前必须确认 JY61P TX 到 ESP32-S3 GPIO42 已做 3.3V 电平转换。

## P0 必修项

1. **H5 烧录资源单一化**
   - `firmware/web` 作为唯一前端源目录。
   - PlatformIO LittleFS 打包使用 `data_dir = web`。
   - 将当前完整页面同步到 `firmware/web`，避免 `data` 与 `web` 分叉导致烧错页面。

2. **本地危险接口加鉴权**
   - 对 `/api/jog`、`/api/mode-request`、`/api/reset-fault`、`/api/calibrate`、`/api/wizard-complete`、`/api/wifi` 增加本地 API Key 检查。
   - 默认开发构建保持可调试；量产/现场构建必须通过 build flag 打开 `FOLLOWBOX_LOCAL_API_AUTH_REQUIRED=1` 并设置非默认 key。

3. **云端 OTA 基础链路**
   - 云端增加 firmware manifest / binary download / ota-result 端点。
   - 固件增加云端 OTA manager，STA 连云后周期查询 manifest，发现版本变化后下载 bin 并写入 OTA 分区。
   - OTA 执行期间置安全标志，控制任务持续 `drive.stopNow()`。

4. **上线参数一致性**
   - PWM 频率按规格文档回到 1000 Hz。
   - UART HAL 支持 UART0，为后续 IMU 启用做准备，但 `UART_NUM_IMU` 维持 `-1`。

5. **验证**
   - `node --check cloud/server.js`
   - `pio run -e esp32-s3-devkitc-1`
   - `pio run -e esp32-s3-devkitc-1 -t buildfs`
   - 有 USB 设备时再执行首烧；无设备时输出阻塞原因。

## 首烧与 OTA 流程

1. 首烧前确认：
   - 车轮离地或驱动电源断开。
   - 急停按钮和 GPIO21 急停反馈已接好并实际触发过。
   - 5V/3.3V 电源测量正常，JY61P 若接入必须经过电平转换。
   - 现场 API Key、AP 密码、OTA 密码、云端 token 已改为非默认值。

2. 首次 USB 烧录：
   - `pio run -e esp32-s3-devkitc-1 -t upload`
   - `pio run -e esp32-s3-devkitc-1 -t uploadfs`
   - 串口观察启动日志，确认 AP、H5、云端 idle、motor safe。

3. 后续 OTA：
   - 将新 `firmware.bin` 发布到云端固件目录并更新 manifest。
   - 固件通过 STA 向云端查询版本并拉取更新。
   - OTA 成功后设备重启；失败时维持安全停车并上报失败原因。

## 实车测试许可

当前修复完成后默认只批准到 L2/L3：

- L0 静态代码/构建验证：允许。
- L1 低压上电、USB 串口、H5 页面：允许。
- L2 车轮离地低速点动：必须有急停反馈、驱动电源可断开、H5 鉴权已开启。
- L3 地面牵引/低速直线：必须先完成 L2，确认左右方向、刹车、油门标定和遥控优先级。
- L4 自动跟随：必须有 UWB 实测日志、避障有效日志、丢链停车日志、人工急停复测。

没有上述证据时，不进入带人/带载/自动跟随测试。
