// 1-bit image and payload helpers shared by the config page and the Node tests.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.Convert = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  // One byte per pixel in, packed rows out. Bit 1 = white, MSB first.
  function packThreshold(gray, w, h, cutoff) {
    const stride = (w + 7) >> 3;
    const out = new Uint8Array(stride * h);
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        if (gray[y * w + x] >= cutoff) out[y * stride + (x >> 3)] |= 0x80 >> (x & 7);
      }
    }
    return out;
  }

  // Largest centered source rect with the target's aspect ratio.
  function fitRect(sw, sh, tw, th) {
    if (sw * th > sh * tw) {
      const width = Math.min(sw, Math.round((sh * tw) / th));
      return { sx: Math.round((sw - width) / 2), sy: 0, sw: width, sh: sh };
    }
    const height = Math.min(sh, Math.round((sw * th) / tw));
    return { sx: 0, sy: Math.round((sh - height) / 2), sw: sw, sh: height };
  }

  function rleEncode(bytes) {
    const out = [];
    let i = 0;
    while (i < bytes.length) {
      const value = bytes[i];
      let run = 1;
      while (i + run < bytes.length && bytes[i + run] === value && run < 255) run++;
      out.push(run, value);
      i += run;
    }
    return Uint8Array.from(out);
  }

  function rleDecode(bytes, expectedLength) {
    const out = new Uint8Array(expectedLength);
    let n = 0;
    for (let i = 0; i + 1 < bytes.length; i += 2) {
      const run = bytes[i];
      const value = bytes[i + 1];
      for (let r = 0; r < run && n < out.length; r++) out[n++] = value;
    }
    return out;
  }

  function toHex(bytes) {
    let s = '';
    for (let i = 0; i < bytes.length; i++) s += bytes[i].toString(16).padStart(2, '0');
    return s;
  }

  function fromHex(str) {
    const out = new Uint8Array(str.length / 2);
    for (let i = 0; i < out.length; i++) out[i] = parseInt(str.substr(i * 2, 2), 16);
    return out;
  }

  return { packThreshold, fitRect, rleEncode, rleDecode, toHex, fromHex };
});
