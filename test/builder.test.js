const test = require('node:test');
const assert = require('node:assert');
const { state, buildPayload, problem } = require('../src/pkjs/config/app.js');

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
  assert.deepStrictEqual(payload.art, [{ r: 0, s: 0, data: 'beef' }]);
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
