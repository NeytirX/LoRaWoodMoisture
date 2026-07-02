# Wiring: Wood Moisture Prototype (T-Beam + ADS1115)

Bench prototype wiring. Since firmware 1.3.0, this wiring **is** the firmware's
default measurement path (`USE_EXTERNAL_ADC` true, shipped default) - see
docs/firmware_architecture.md §2.2 for the sensor-layer behavior and
docs/deployment_guide.md §2.1 for the field-deployment wiring pointer.

- **Probe = two nails in the wood** (two-electrode resistive). Resistance between
  the nails falls as moisture rises.
- **ADS1115** (16-bit I2C ADC) reads the divider node. The ESP32 internal ADC
  (GPIO 35) is unused in this build; the firmware only reads GPIO 35 when
  `USE_EXTERNAL_ADC` is false (legacy internal-ADC path, see
  docs/deployment_guide.md §2.1).
- **DS18B20 not fitted yet.** Its pins are listed under "future" so the headers
  can be pre-soldered now.

## Divider

```
  GPIO 25 ──100kΩ(1%)──┬── ADS1115 A0   (ADC reads this node)
 (drive HIGH only       │
  during a measurement) nail #1
                        (wood)
                        nail #2
                         │
                        GND

  A0 = 3.3V * R_wood / (100k + R_wood)
  dry wood  -> high R -> A0 near 3.3 V
  wet wood  -> low  R -> A0 near 0 V
```

## T-Beam pins to header now (future-proofed)

Solder header pins on these while the iron is out:

| Pin | Use |
|-----|-----|
| 3V3 | ADS VDD (+ future DS18B20 VDD and its pull-up) |
| GND | ADS GND + ADDR, nail #2 (+ future DS18B20 GND) |
| GPIO 21 | I2C SDA |
| GPIO 22 | I2C SCL |
| GPIO 25 | probe drive / 100k |
| GPIO 14 | future DS18B20 data |

Leave **GPIO 35** free. **Never** use **GPIO 32** (radio BUSY).

## Connections

**ADS1115 -> T-Beam**

| ADS1115 | T-Beam |
|---------|--------|
| VDD | 3V3 |
| GND | GND |
| SCL | GPIO 22 |
| SDA | GPIO 21 |
| ADDR | GND  (I2C address 0x48) |

**Divider / probe**

| from | to |
|------|----|
| 100 kΩ leg | GPIO 25 |
| 100 kΩ other leg | ADS1115 A0 |
| nail #1 | ADS1115 A0 (same node as the 100k) |
| nail #2 | GND |

**Future DS18B20 (not wired now)**

| from | to |
|------|----|
| VDD | 3V3 |
| GND | GND |
| data | GPIO 14 |
| 4.7 kΩ | between GPIO 14 and 3V3 |

## Notes

- **ADDR -> GND = 0x48** (firmware default). Do NOT tie ADDR to the neighbouring
  3V3 pin (that would be 0x49).
- 3V3 and GND each appear on **multiple** T-Beam pins. Header more than one, or
  hardwire a branchable lead, to feed several things from one rail.
- Nails are steel and do not solder easily: wrap stripped wire tightly around the
  head, or use an alligator clip / ring terminal. Solid mechanical contact is
  what matters for a resistance reading.
- ADS1115 used **single-ended on A0** (node to GND).
- Power the ADS1115 from 3V3, not 5V/VUSB, to keep A0 within input range.
