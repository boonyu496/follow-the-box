const assert = require("assert");

const { _test } = require("./server");

assert(_test, "server test helpers must be exported");

const device = {
  id: "followbox-test",
  lastIngestAt: Date.now(),
  state: null,
  logs: [],
  command: { seq: 1, deadman: false, forward: 0, turn: 0, safe_idle: false },
  commandAt: 0,
  ota: {},
  video: { lastFrameAt: 0, frameSeq: 0 },
};

_test.appendDeviceLogs(device, ["A", "B", "C"]);
_test.appendDeviceLogs(device, ["B", "C", "D"]);
assert.deepStrictEqual(device.logs, ["A", "B", "C", "D"]);

for (let i = 0; i < 240; i += 1) {
  _test.appendDeviceLogs(device, [`line-${i}`]);
}
assert(device.logs.length <= _test.MAX_DEVICE_LOG_LINES);

_test.appendDeviceLogs(device, ["X".repeat(_test.MAX_LOG_LINE_CHARS + 20)]);
assert(device.logs.at(-1).length === _test.MAX_LOG_LINE_CHARS);

const payload = _test.buildBroadcastPayload(device, Date.now());
assert(payload.logs.length <= _test.MAX_BROADCAST_LOG_LINES);
assert(JSON.stringify(payload).length < 50000);

console.log("cloud log de-dup test PASS");
