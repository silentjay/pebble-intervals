const test = require('node:test');
const assert = require('node:assert');
const {
  packThreshold, fitRect, rleEncode, rleDecode, toHex, fromHex,
} = require('../src/pkjs/convert.js');

test('packs a row with white as 1', () => {
  const gray = [0, 255, 0, 255, 0, 255, 0, 255];
  assert.deepStrictEqual([...packThreshold(gray, 8, 1, 128)], [0x55]);
});

test('inverted row packs to the inverse', () => {
  const gray = [255, 0, 255, 0, 255, 0, 255, 0];
  assert.deepStrictEqual([...packThreshold(gray, 8, 1, 128)], [0xaa]);
});

test('pads rows to whole bytes', () => {
  const out = packThreshold([255, 255, 255], 3, 1, 128);
  assert.strictEqual(out.length, 1);
  assert.strictEqual(out[0], 0xe0);
});

test('cover-crops a landscape source to the target aspect', () => {
  assert.deepStrictEqual(fitRect(200, 100, 96, 112), { sx: 57, sy: 0, sw: 86, sh: 100 });
});

test('cover-crops a portrait source to the target aspect', () => {
  assert.deepStrictEqual(fitRect(100, 200, 96, 112), { sx: 0, sy: 42, sw: 100, sh: 117 });
});

test('rle round-trips', () => {
  const data = [1, 1, 1, 2, 9];
  assert.deepStrictEqual([...rleDecode(rleEncode(data), data.length)], data);
});

test('rle splits runs longer than 255', () => {
  const data = new Uint8Array(600).fill(7);
  const encoded = rleEncode(data);
  assert.deepStrictEqual([...encoded], [255, 7, 255, 7, 90, 7]);
  assert.strictEqual(rleDecode(encoded, 600).length, 600);
});

test('hex round-trips', () => {
  const data = [0x00, 0xab, 0xff];
  assert.strictEqual(toHex(data), '00abff');
  assert.deepStrictEqual([...fromHex('00abff')], data);
});

const { encodeRoutineSet } = require('../src/pkjs/convert.js');

test('encodes the shared C fixture byte for byte', () => {
  const blob = encodeRoutineSet([{
    name: 'Warm',
    steps: [
      { name: 'Arm', work_s: 30, rest_s: 10, has_art: false },
      { name: 'Leg', work_s: 45, rest_s: 0, has_art: true },
    ],
  }]);
  assert.strictEqual(toHex(blob), '0102045761726d0341726d1e000a0000034c65672d00000001');
});

test('truncates an over-long name to 20 bytes at a code point boundary', () => {
  const blob = encodeRoutineSet([{
    name: '€'.repeat(12),
    steps: [{ name: 'x', work_s: 1, rest_s: 0, has_art: false }],
  }]);
  assert.strictEqual(blob[2], 18, 'backs off to the last whole code point');
  const nameBytes = blob.slice(3, 21);
  assert.strictEqual(new TextDecoder().decode(nameBytes), '€'.repeat(6));
});

test('refuses more than 8 routines or 15 steps', () => {
  const many = Array.from({ length: 9 }, () => ({ name: 'r', steps: [] }));
  assert.throws(() => encodeRoutineSet(many));
  const long = [{ name: 'r', steps: Array.from({ length: 16 }, () => ({ name: 's', work_s: 1, rest_s: 0 })) }];
  assert.throws(() => encodeRoutineSet(long));
});
