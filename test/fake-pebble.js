function makeFakePebble() {
  const sent = [];
  return {
    sent,
    sendAppMessage(dict, ok) {
      sent.push(dict);
      if (ok) ok();
    },
    addEventListener() {},
    openURL() {},
  };
}

function makeStorage() {
  const map = new Map();
  return {
    getItem(key) {
      return map.has(key) ? map.get(key) : null;
    },
    setItem(key, value) {
      map.set(key, String(value));
    },
  };
}

module.exports = { makeFakePebble, makeStorage };
