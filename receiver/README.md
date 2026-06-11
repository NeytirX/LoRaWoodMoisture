# Gateway-less LoRa receiver (Heltec V3)

A one-way LoRa receiver for the wood moisture sensor, for use when no EU433
LoRaWAN gateway is available (bench validation, interim data collection, RF
range checks). It is **not** LoRaWAN and **not** a substitute for a real
gateway: a single SX1262 hears one channel at one spreading factor. The
deployment data path stays LoRaWAN.

- **Board:** Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262), 433 MHz variant.
- **What it does:** receives the framed Cayenne LPP the T-Beam sends in P2P
  mode, decodes it, prints JSON over serial, and (optionally) publishes to a
  local MQTT broker.

## Why a separate project

Different board, different MCU, different libraries, opposite job. It shares
**only** the wire format in `include/p2p_frame.h`. Keeping it out of the sensor
firmware is deliberate: the LoRaWAN thesis firmware stays untouched.

## Wire format

The transmitter sends `[0x57][0x4D][version][nodeId][lppLen][...Cayenne LPP...]`.
The channel map and radio parameters live in `include/p2p_frame.h` and **must
match** the T-Beam P2P build. See the header for the current values
(433.5 MHz, SF9, BW125, CR4/5, sync word 0x12).

## First build

```bash
cp include/secrets_template.h include/secrets.h   # then edit secrets.h
pio run                                            # build
pio run --target upload                            # flash (Heltec on USB)
pio device monitor -b 115200                       # watch decoded JSON
```

`secrets.h` is gitignored. **Leave `WIFI_SSID` empty to run serial-only** (still
prints JSON over USB, just skips WiFi + MQTT). Fill it in to publish to MQTT.

## Reading the data over MQTT

Run a broker on your laptop (one-time):

```bash
sudo apt install mosquitto mosquitto-clients   # or: docker run -p 1883:1883 eclipse-mosquitto
```

Point `MQTT_BROKER_HOST` in `secrets.h` at that machine's LAN IP, reflash, then:

```bash
mosquitto_sub -h localhost -t wood/sensor -v
```

Each reading arrives as one JSON line, e.g.:

```json
{"node":1,"rssi":-47,"snr":9.5,"wood_mc":12.5,"wood_temp_c":21.5,"battery_v":3.95,"temp_fallback":0}
```

## Proving it works before refactoring the firmware (smoke test)

`tools/p2p_tx_smoke/` is a throwaway transmitter that sends canned frames from
the T-Beam, so you can validate this receiver end to end without touching the
sensor firmware:

```bash
cd ../tools/p2p_tx_smoke
pio run --target upload          # flash the T-Beam with the smoke transmitter
pio device monitor -b 115200     # shows "tx #N ... -> 0"
```

With the Heltec receiver also powered, you should see a decoded JSON line on the
receiver's serial (and on `mosquitto_sub`) every ~5 s. Once that works, the
real sensor firmware can be wired to the same wire format via the transport
seam, and the smoke transmitter discarded.

## Known assumptions to verify on hardware

- Heltec V3 SX1262 pins (NSS 8, SCK 9, MOSI 10, MISO 11, RST 12, BUSY 13,
  DIO1 14) and TCXO 1.8 V. These are the standard V3 values; confirm on first
  flash (init failure code points at a mismatch).
- Both ends must use identical radio params from `p2p_frame.h`.
