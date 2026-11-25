var Convert = typeof window !== 'undefined' ? window.Convert : require('../convert');
var W = 72;
var H = 84;
var STRIDE = 9;
var MAX_ROUTINES = 8;
var MAX_STEPS = 15;
var MAX_ART = 12;
var MAX_BLOB = 3072;
var STORE_KEY = 'intervals.builder';

var state = { routines: [] };
var grays = {};

function load() {
  try {
    var raw = localStorage.getItem(STORE_KEY);
    if (raw) state.routines = JSON.parse(raw);
  } catch (e) {
    state.routines = [];
  }
}

function persist() {
  try {
    localStorage.setItem(STORE_KEY, JSON.stringify(state.routines));
  } catch (e) {
    // Storage full: the payload still goes to the watch.
  }
}

function el(tag, props, children) {
  var node = document.createElement(tag);
  Object.keys(props || {}).forEach(function (k) {
    if (k === 'text') node.textContent = props[k];
    else if (k === 'className') node.className = props[k];
    else node[k] = props[k];
  });
  (children || []).forEach(function (c) { node.appendChild(c); });
  return node;
}

function drawPreview(canvas, hex) {
  var ctx = canvas.getContext('2d');
  var packed = Convert.fromHex(hex);
  var img = ctx.createImageData(W, H);
  for (var y = 0; y < H; y++) {
    for (var x = 0; x < W; x++) {
      var on = packed[y * STRIDE + (x >> 3)] & (0x80 >> (x & 7));
      var i = (y * W + x) * 4;
      img.data[i] = img.data[i + 1] = img.data[i + 2] = on ? 255 : 0;
      img.data[i + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
}

function packFromGray(gray, cutoff) {
  return Convert.toHex(Convert.packThreshold(gray, W, H, cutoff));
}

async function grayFromFile(file) {
  var bitmap = await createImageBitmap(file);
  var canvas = document.createElement('canvas');
  canvas.width = W;
  canvas.height = H;
  var ctx = canvas.getContext('2d');
  var fit = Convert.fitRect(bitmap.width, bitmap.height, W, H);
  ctx.drawImage(bitmap, fit.sx, fit.sy, fit.sw, fit.sh, 0, 0, W, H);
  var data = ctx.getImageData(0, 0, W, H).data;
  var gray = new Uint8Array(W * H);
  for (var i = 0; i < gray.length; i++) {
    gray[i] = (data[i * 4] * 299 + data[i * 4 + 1] * 587 + data[i * 4 + 2] * 114) / 1000;
  }
  return gray;
}

function render() {
  var app = document.getElementById('app');
  app.textContent = '';

  state.routines.forEach(function (routine, r) {
    var card = el('section', { className: 'routine' });

    var nameInput = el('input', { type: 'text', value: routine.name, maxLength: 20 });
    nameInput.addEventListener('input', function () {
      routine.name = nameInput.value;
      persist();
    });

    card.appendChild(el('div', { className: 'row' }, [nameInput]));
    card.appendChild(el('button', {
      className: 'small', text: 'Remove routine',
      onclick: function () {
        state.routines.splice(r, 1);
        persist();
        render();
      },
    }));

    routine.steps.forEach(function (step, s) {
      card.appendChild(renderStep(routine, step, r, s));
    });

    card.appendChild(el('button', {
      text: 'Add step',
      onclick: function () {
        if (routine.steps.length >= MAX_STEPS) return;
        routine.steps.push({ name: '', work_s: 30, rest_s: 15, art: null });
        persist();
        render();
      },
    }));

    app.appendChild(card);
  });
}

function renderStep(routine, step, r, s) {
  var box = el('div', { className: 'step' });

  var name = el('input', { type: 'text', value: step.name, maxLength: 20, placeholder: 'Exercise' });
  var work = el('input', { type: 'number', min: 0, max: 65535, value: step.work_s });
  var rest = el('input', { type: 'number', min: 0, max: 65535, value: step.rest_s });

  name.addEventListener('input', function () { step.name = name.value; persist(); });
  work.addEventListener('input', function () { step.work_s = Number(work.value); persist(); });
  rest.addEventListener('input', function () { step.rest_s = Number(rest.value); persist(); });

  box.appendChild(el('div', { className: 'row' }, [
    name,
    el('label', { className: 'hint', text: 'work' }), work,
    el('label', { className: 'hint', text: 'rest' }), rest,
  ]));

  var art = el('div', { className: 'art' });
  var canvas = el('canvas', { width: W, height: H });
  var file = el('input', { type: 'file', accept: 'image/*' });
  var slider = el('input', { type: 'range', min: 32, max: 224, value: 128 });
  var label = el('label', { text: 'Image' });
  slider.hidden = true;

  var gray = grays[r + ':' + s] || null;

  function refresh() {
    if (!step.art) return;
    drawPreview(canvas, step.art);
  }

  file.addEventListener('change', async function () {
    if (!file.files || !file.files[0]) return;
    try {
      gray = await grayFromFile(file.files[0]);
    } catch (e) {
      showMessage('Could not read that image.', false);
      return;
    }
    grays[r + ':' + s] = gray;
    step.art = packFromGray(gray, Number(slider.value));
    slider.hidden = false;
    persist();
    refresh();
  });

  slider.addEventListener('input', function () {
    if (!gray) return;
    step.art = packFromGray(gray, Number(slider.value));
    persist();
    refresh();
  });

  if (step.art) {
    slider.hidden = false;
    refresh();
  }

  art.appendChild(canvas);
  art.appendChild(file);
  art.appendChild(slider);
  box.appendChild(art);

  var controls = el('div', { className: 'row' }, [
    el('button', {
      className: 'small', text: '↑',
      onclick: function () {
        if (s === 0) return;
        var tmp = routine.steps[s - 1];
        routine.steps[s - 1] = routine.steps[s];
        routine.steps[s] = tmp;
        persist();
        render();
      },
    }),
    el('button', {
      className: 'small', text: '↓',
      onclick: function () {
        if (s >= routine.steps.length - 1) return;
        var tmp = routine.steps[s + 1];
        routine.steps[s + 1] = routine.steps[s];
        routine.steps[s] = tmp;
        persist();
        render();
      },
    }),
    el('button', {
      className: 'small', text: 'Remove step',
      onclick: function () {
        routine.steps.splice(s, 1);
        persist();
        render();
      },
    }),
    el('label', { className: 'hint', text: step.art ? 'picture set' : 'no picture' }),
  ]);
  box.appendChild(controls);
  return box;
}

function buildPayload() {
  return {
    routines: state.routines.map(function (routine) {
      return {
        name: routine.name || 'Routine',
        steps: routine.steps.map(function (step) {
          return {
            name: step.name || 'Step',
            work_s: step.work_s,
            rest_s: step.rest_s,
            has_art: !!step.art,
          };
        }),
      };
    }),
    art: (function () {
      var art = [];
      state.routines.forEach(function (routine, r) {
        routine.steps.forEach(function (step, s) {
          if (step.art) {
            // Stored raw for the preview; RLE for the close-URL payload.
            var raw = Convert.fromHex(step.art);
            art.push({ r: r, s: s, data: Convert.toHex(Convert.rleEncode(raw)) });
          }
        });
      });
      return art;
    })(),
  };
}

function problem() {
  if (state.routines.length > MAX_ROUTINES) return 'Too many routines (max ' + MAX_ROUTINES + ').';
  var bad = null;
  state.routines.forEach(function (routine) {
    var name = routine.name || 'a routine';
    if (routine.steps.length > MAX_STEPS) bad = 'Too many steps in ' + name + ' (max ' + MAX_STEPS + ').';
    if (!bad && routine.steps.length === 0) bad = 'Routine "' + name + '" has no steps.';

    var pictures = 0;
    routine.steps.forEach(function (step) {
      if (step.art) pictures++;
      if (!bad && !(step.work_s >= 1)) bad = 'Every step needs a work time of at least 1 second.';
      if (!bad && !(step.rest_s >= 0 && step.rest_s <= 65535)) bad = 'Rest time must be between 0 and 65535 seconds.';
    });
    if (!bad && pictures > MAX_ART) {
      bad = 'Art budget exceeded: ' + pictures + '/' + MAX_ART + ' pictures in ' + name + '.';
    }
  });
  if (bad) return bad;

  var payload = buildPayload();

  var blob;
  try {
    blob = Convert.encodeRoutineSet(payload.routines);
  } catch (e) {
    return e.message;
  }
  if (blob.length > MAX_BLOB) return 'Routine data too large (' + blob.length + '/' + MAX_BLOB + ' bytes).';
  return null;
}

function showMessage(text, ok) {
  var msg = document.getElementById('msg');
  msg.hidden = false;
  msg.className = 'msg ' + (ok ? 'ok' : 'error');
  msg.textContent = text;
}

function returnTo() {
  var match = /[?&]return_to=([^&]+)/.exec(location.search);
  return match ? decodeURIComponent(match[1]) : 'pebblejs://close#';
}

if (typeof document !== 'undefined') {

document.getElementById('add-routine').addEventListener('click', function () {
  if (state.routines.length >= MAX_ROUTINES) {
    showMessage('Too many routines (max ' + MAX_ROUTINES + ').', false);
    return;
  }
  state.routines.push({ name: 'Routine ' + (state.routines.length + 1), steps: [] });
  persist();
  render();
});

document.getElementById('save').addEventListener('click', function () {
  if (state.routines.length === 0 &&
      !confirm('Save an empty routine list? This removes every routine from the watch.')) {
    return;
  }
  var bad = problem();
  if (bad) {
    showMessage(bad, false);
    return;
  }

  var payload = buildPayload();

  persist();
  showMessage('Saved. Closing…', true);
  location = returnTo() + encodeURIComponent(JSON.stringify(payload));
});

load();
render();

}

if (typeof module === 'object' && module.exports) {
  module.exports = { state: state, buildPayload: buildPayload, problem: problem };
}
