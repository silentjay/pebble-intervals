// Runs inside the Pebble app. Owns the routine/art data and serves the watch.
var Convert = require('./convert');

var CONFIG_URL = 'https://silentjay.github.io/pebble-intervals/src/pkjs/config/index.html';
var ROUTINES_KEY = 'intervals.routines';
var ART_KEY = 'intervals.art';
var ROUTINE_CHUNK = 128;
var ART_CHUNK = 378;
var ART_BYTES = 9 * 84;

function createService(pebble, storage) {
  function read(key, fallback) {
    try {
      var raw = storage.getItem(key);
      return raw == null ? fallback : JSON.parse(raw);
    } catch (e) {
      return fallback;
    }
  }

  // Sends bytes as appmessages; each message carries a slice plus the shared header.
  function sendChunks(base, bytes, chunkSize, payloadLen) {
    var count = Math.ceil(bytes.length / chunkSize);
    var i = 0;
    function step() {
      if (i >= count) return;
      var index = i++;
      var start = index * chunkSize;
      var dict = {
        Cmd: base.Cmd,
        ChunkIndex: index,
        ChunkCount: count,
        PayloadLen: payloadLen,
        Chunk: Array.prototype.slice.call(bytes, start, Math.min(start + chunkSize, bytes.length)),
      };
      if (base.RoutineId !== undefined) {
        dict.RoutineId = base.RoutineId;
        dict.StepIndex = base.StepIndex;
      }
      pebble.sendAppMessage(dict, step, step);
    }
    step();
  }

  function sendRoutines() {
    var blob;
    try {
      blob = Convert.encodeRoutineSet(read(ROUTINES_KEY, []));
    } catch (e) {
      return;
    }
    sendChunks({ Cmd: 2 }, blob, ROUTINE_CHUNK, blob.length);
  }

  function handleWebviewClosed(response) {
    var payload;
    try {
      payload = JSON.parse(decodeURIComponent(response));
    } catch (e) {
      return false;
    }
    if (!payload || !Array.isArray(payload.routines)) return false;

    var blob;
    try {
      blob = Convert.encodeRoutineSet(payload.routines);
    } catch (e) {
      return false;
    }

    var art = {};
    var entries = Array.isArray(payload.art) ? payload.art : [];
    for (var n = 0; n < entries.length; n++) {
      var entry = entries[n];
      try {
        var raw = Convert.fromHex(entry.data);
        var total = 0;
        for (var j = 0; j + 1 < raw.length; j += 2) total += raw[j];
        if (total !== ART_BYTES) throw new Error('bad run length');
        art[entry.r + ':' + entry.s] = Array.from(Convert.rleDecode(raw, ART_BYTES));
      } catch (e) {
        // A broken picture is dropped; the routines still save.
      }
    }

    storage.setItem(ROUTINES_KEY, JSON.stringify(payload.routines));
    storage.setItem(ART_KEY, JSON.stringify(art));
    sendChunks({ Cmd: 2 }, blob, ROUTINE_CHUNK, blob.length);
    return true;
  }

  function handleMessage(dict) {
    if (!dict) return;
    if (dict.Cmd === 1) {
      sendRoutines();
      return;
    }
    if (dict.Cmd === 3) {
      var art = read(ART_KEY, {});
      var bytes = art[dict.RoutineId + ':' + dict.StepIndex];
      if (!Array.isArray(bytes) || bytes.length !== ART_BYTES) return;
      sendChunks(
        { Cmd: 4, RoutineId: dict.RoutineId, StepIndex: dict.StepIndex },
        bytes, ART_CHUNK, ART_BYTES
      );
    }
  }

  return { handleWebviewClosed: handleWebviewClosed, handleMessage: handleMessage, sendRoutines: sendRoutines };
}

if (typeof Pebble !== 'undefined' && Pebble.addEventListener) {
  var service = createService(Pebble, localStorage);
  Pebble.addEventListener('showConfiguration', function () {
    Pebble.openURL(CONFIG_URL + '?return_to=' + encodeURIComponent('pebblejs://close#'));
  });
  Pebble.addEventListener('webviewclosed', function (e) {
    service.handleWebviewClosed(e.response);
  });
  Pebble.addEventListener('appmessage', function (e) {
    service.handleMessage(e.payload);
  });
}

if (typeof module === 'object' && module.exports) {
  module.exports = { createService: createService, CONFIG_URL: CONFIG_URL };
}
