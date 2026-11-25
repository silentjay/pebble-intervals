const test = require('node:test');
const assert = require('node:assert');
const { state, buildPayload, problem } = require('../src/pkjs/config/app.js');
const Convert = require('../src/pkjs/convert.js');

function routineWith(pictures, work = 30) {
  const steps = [];
  for (let i = 0; i < pictures; i++) {
    steps.push({ name: 's' + i, work_s: work, rest_s: 10, art: 'aa' });
  }
  return { name: 'R', steps: steps };
}

test('payload carries routines with has_art and art entries', () => {
  state.routines = [{ name: 'Warm', steps: [{ name: 'Arm', work_s: 30, rest_s: 10, art: 'beef' }] }];
  const payload = buildPayload();
  assert.strictEqual(payload.routines[0].steps[0].has_art, true);
  assert.strictEqual(payload.art.length, 1);
  assert.deepStrictEqual([...Convert.rleDecode(Convert.fromHex(payload.art[0].data), 2)], [0xbe, 0xef]);
});

test('a step without a picture reports has_art false', () => {
  state.routines = [{ name: 'W', steps: [{ name: 'A', work_s: 5, rest_s: 0, art: null }] }];
  assert.strictEqual(buildPayload().routines[0].steps[0].has_art, false);
});

test('more than 12 pictures is refused', () => {
  state.routines = [routineWith(13)];
  assert.match(problem(), /Art budget exceeded: 13\/12/);
});

test('zero work time is refused', () => {
  state.routines = [routineWith(1, 0)];
  assert.match(problem(), /work time of at least 1 second/);
});

test('a valid routine has no problem', () => {
  state.routines = [routineWith(2)];
  assert.strictEqual(problem(), null);
});

test('the art budget is per routine, not global', () => {
  state.routines = [routineWith(12), routineWith(12)];
  assert.strictEqual(problem(), null);
});

test('a routine with no steps is refused', () => {
  state.routines = [{ name: 'Empty', steps: [] }];
  assert.match(problem(), /has no steps/);
});

test('a negative rest time is refused', () => {
  state.routines = [{ name: 'R', steps: [{ name: 's', work_s: 5, rest_s: -1, art: null }] }];
  assert.match(problem(), /Rest time must be between/);
});
