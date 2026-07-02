// FollowBox embedded H5 runtime state.
// Keep this file free of DOM reads so app.js can own rendering and events.

const LOCAL_KEY_STORAGE = "followbox.localApiKey"; // sessionStorage: cleared on tab close
const CAMERA_URL_STORAGE = "followbox.cameraStreamUrl";
const CLOUD_VIDEO_BASE_URL = "https://www.boonai.cn/fb";
const CLOUD_VIDEO_DEVICE_ID = "followbox-001";
// Empty by default: do not bake cloud operator credentials into device H5.
const CLOUD_VIDEO_OPERATOR_TOKEN = "";
const PRIVATE_CAMERA_HOSTS = new Set(["192.168.4.2", "192.168.4.10"]);
// Camera video is enabled for AP-side diagnosis. It still shares the single
// SoftAP radio, so if WS/API timeouts return during live video tests, disable
// this flag again until the transport is reworked.
const CAMERA_ENABLED = true;
const HTTP_DIAGNOSTIC_TIMEOUT_MS = 2200;
const BROWSER_LOG_LIMIT = 80;
const MAX_RANGE_MM = 3000;
const MAP_MAX_MM = 4000;
const TOF_RATE_WINDOW_MS = 5000;

let ws = null;
let jogSeq = 1;
let jogTimer = null;
let wsRetryTimer = null;
let wsRetryDelay = 1000;
let wsRetryCount = 0;
let wifiSwitching = false;
let joyPointerId = null;
let joyForward = 0;
let joyTurn = 0;
let isFullscreen = false;
let fullscreenOrientationLocked = false;
let activeCameraUrl = "";
let lastTelemetryCameraUrl = "";
let userCameraOverride = false;
let cameraImageOnline = false;
let cloudVideoTimer = null;
let cameraRetryTimer = null;
let cameraRetryDelay = 3000;
let localAuthStatus = null;
let latestState = null;
let lastStateAt = 0;
let logsApiUnavailableLogged = false;
let stateFallbackUnavailableLogged = false;
let lastLoggedMode = "";
let lastLoggedCameraOnline = null;
let lastLoggedCameraUrl = "";
const deviceLogs = [];
const browserLogs = [];
const tofRateWindow = [];
const spatialTrail = [];
let lastSpatialTrailKey = "";

// Canvas redraw state (RAF throttled).
let canvasDirty = false;
let latestUwbData = null;
let latestObstacleData = null;
