// Cross-module seam: what the builder emits must be what the phone decodes.
const test = require('node:test');
const assert = require('node:assert');
const Convert = require('../src/pkjs/convert.js');
const { createService } = require('../src/pkjs/index.js');
const builder = require('../src/pkjs/config/app.js');
const { makeFakePebble, makeStorage } = require('./fake-pebble.js');

test('a builder payload reaches the watch byte for byte', () => {
  const gray = new Uint8Array(72 * 84);
  for (let y = 0; y < 84; y++) {
    for (let x = 0; x < 72; x++) {
      gray[y * 72 + x] = (x > 20 && x < 50 && y > 10 && y < 70) ? 0 : 255;
    }
  }
  const packed = Convert.packThreshold(gray, 72, 84, 128);

  builder.state.routines = [{
    name: 'Warm',
    steps: [{ name: 'Arm', work_s: 5, rest_s: 0, art: Convert.toHex(packed) }],
  }];

  const pebble = makeFakePebble();
  const service = createService(pebble, makeStorage());
  assert.strictEqual(
    service.handleWebviewClosed(encodeURIComponent(JSON.stringify(builder.buildPayload()))),
    true,
  );

  pebble.sent.length = 0;
  service.handleMessage({ Cmd: 3, RoutineId: 0, StepIndex: 0 });

  const served = [];
  for (const message of pebble.sent) served.push(...message.Chunk);
  assert.deepStrictEqual(Uint8Array.from(served), Uint8Array.from(packed));
});
