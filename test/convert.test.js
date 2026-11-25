const test = require('node:test');
const assert = require('node:assert');
const {
  packThreshold, fitRect, rleEncode, rleDecode, toHex, fromHex,
} = require('../src/pkjs/config/convert.js');

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

test('cover-crops a landscape source to 96x112', () => {
  assert.deepStrictEqual(fitRect(200, 100, 96, 112), { sx: 57, sy: 0, sw: 86, sh: 100 });
});

test('cover-crops a portrait source to 96x112', () => {
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
