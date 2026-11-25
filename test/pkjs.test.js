const test = require('node:test');
const assert = require('node:assert');
const { createService } = require('../src/pkjs/index.js');
const { makeFakePebble, makeStorage } = require('./fake-pebble.js');
const Convert = require('../src/pkjs/convert.js');

const ROUTINES = [{
  name: 'Warm',
  steps: [
    { name: 'Arm', work_s: 30, rest_s: 10, has_art: true },
    { name: 'Leg', work_s: 45, rest_s: 0, has_art: false },
  ],
}];

const IMAGE = new Uint8Array(1344).fill(0xaa);

function artEntry(r, s, bytes) {
  return { r, s, data: Convert.toHex(Convert.rleEncode(bytes)) };
}

function closePayload(routines, art) {
  return encodeURIComponent(JSON.stringify({ routines, art }));
}

function concatenate(messages, key) {
  const bytes = [];
  for (const m of messages) bytes.push(...m.Chunk);
  return Uint8Array.from(bytes);
}

test('a close payload stores routines and art and pushes the routine set', () => {
  const pebble = makeFakePebble();
  const storage = makeStorage();
  const service = createService(pebble, storage);

  assert.strictEqual(service.handleWebviewClosed(closePayload(ROUTINES, [artEntry(0, 0, IMAGE)])), true);

  const set = pebble.sent.filter((m) => m.Cmd === 2);
  assert.ok(set.length > 0);
  assert.strictEqual(set[0].PayloadLen, Convert.encodeRoutineSet(ROUTINES).length);
  assert.strictEqual(set[0].ChunkCount, set.length);
  assert.deepStrictEqual(
    [...concatenate(set)],
    [...Convert.encodeRoutineSet(ROUTINES)],
  );
});

test('a second save replaces the first set', () => {
  const pebble = makeFakePebble();
  const service = createService(pebble, makeStorage());
  service.handleWebviewClosed(closePayload(ROUTINES, []));
  service.handleWebviewClosed(closePayload([{ name: 'Only', steps: [] }], []));
  service.handleMessage({ Cmd: 1 });
  const last = pebble.sent.filter((m) => m.Cmd === 2).slice(-1)[0];
  assert.strictEqual(last.PayloadLen, Convert.encodeRoutineSet([{ name: 'Only', steps: [] }]).length);
});

test('the watch can ask for routines again', () => {
  const pebble = makeFakePebble();
  const service = createService(pebble, makeStorage());
  service.handleWebviewClosed(closePayload(ROUTINES, []));
  const before = pebble.sent.length;
  service.handleMessage({ Cmd: 1 });
  assert.ok(pebble.sent.length > before);
});

test('art is served in chunks that reassemble to the image', () => {
  const pebble = makeFakePebble();
  const service = createService(pebble, makeStorage());
  service.handleWebviewClosed(closePayload(ROUTINES, [artEntry(0, 0, IMAGE)]));
  pebble.sent.length = 0;

  service.handleMessage({ Cmd: 3, RoutineId: 0, StepIndex: 0 });

  assert.strictEqual(pebble.sent.length, 2);
  assert.deepStrictEqual([...concatenate(pebble.sent)], [...IMAGE]);
  assert.strictEqual(pebble.sent[0].PayloadLen, 1344);
});

test('a missing picture sends nothing', () => {
  const pebble = makeFakePebble();
  const service = createService(pebble, makeStorage());
  service.handleWebviewClosed(closePayload(ROUTINES, []));
  pebble.sent.length = 0;
  service.handleMessage({ Cmd: 3, RoutineId: 5, StepIndex: 0 });
  assert.strictEqual(pebble.sent.length, 0);
});

test('a malformed payload leaves stored data unchanged', () => {
  const pebble = makeFakePebble();
  const storage = makeStorage();
  const service = createService(pebble, storage);
  service.handleWebviewClosed(closePayload(ROUTINES, [artEntry(0, 0, IMAGE)]));
  const stored = storage.getItem('intervals.routines');

  assert.strictEqual(service.handleWebviewClosed('not-json'), false);
  assert.strictEqual(storage.getItem('intervals.routines'), stored);
  service.handleMessage({ Cmd: 3, RoutineId: 0, StepIndex: 0 });
  assert.deepStrictEqual([...concatenate(pebble.sent.filter((m) => m.Cmd === 4))], [...IMAGE]);
});
