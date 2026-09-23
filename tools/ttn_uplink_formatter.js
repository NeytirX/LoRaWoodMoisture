// ttn_uplink_formatter.js - TTN v3 custom JavaScript uplink formatter.
//
// Paste into the TTN console: Application > Payload formatters > Uplink >
// Custom Javascript formatter. Do NOT use TTN's built-in "Cayenne LPP"
// formatter: it has no Generic Sensor type (0x64), which channel 4
// (resistance) uses, so it rejects the whole frame and every uplink arrives
// without decoded fields.
//
// Decodes only the LPP types the sensor emits, with the same JSON keys as the
// P2P receiver (receiver/src/main.cpp), so both data paths yield identical
// fields. Channel numbers mirror LPP_CHANNEL_* in include/config.h.

var CHANNEL_KEYS = {
  1: "wood_mc",
  2: "wood_temp_c",
  3: "indicated_mc",
  4: "resistance_kohm",
  5: "battery_v",
  6: "esp_temp_c",
  7: "temp_fallback",
  8: "adc_nonlinear"
};

function readInt16(b, i) {
  var v = (b[i] << 8) | b[i + 1];
  return v > 0x7fff ? v - 0x10000 : v;
}

function decodeUplink(input) {
  var b = input.bytes;
  var data = {};
  var i = 0;
  while (i + 2 <= b.length) {
    var channel = b[i++];
    var type = b[i++];
    var key = CHANNEL_KEYS[channel] || "ch" + channel;
    var need = { 0x00: 1, 0x02: 2, 0x67: 2, 0x64: 4 }[type];
    if (need === undefined) {
      return { data: data, errors: ["unknown LPP type 0x" + type.toString(16) + " on ch " + channel] };
    }
    if (i + need > b.length) {
      return { data: data, errors: ["truncated LPP entry on ch " + channel] };
    }
    switch (type) {
      case 0x00: data[key] = b[i]; break;                            // digital input
      case 0x02: data[key] = readInt16(b, i) / 100; break;           // analog input, x100
      case 0x67: data[key] = readInt16(b, i) / 10; break;            // temperature, x10
      case 0x64: data[key] = ((b[i] << 24) >>> 0) + (b[i + 1] << 16) // generic sensor,
                           + (b[i + 2] << 8) + b[i + 3]; break;      // uint32, integer kOhm
    }
    i += need;
  }
  return { data: data };
}
