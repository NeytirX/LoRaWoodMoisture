---
title: "feat: Create AGENTS.md orchestrator for autonomous build/flash/monitor workflow"
type: feat
status: completed
date: 2026-06-02
---

# Create AGENTS.md Orchestrator for LoRaWoodMoisture

## Summary

Create an AGENTS.md file at `LoRaWoodMoisture/AGENTS.md` that teaches opencode how to autonomously build, flash, monitor, validate, and iterate on the LoRaWAN wood moisture firmware for the TTGO T-Beam v1.1. The file will define project-specific commands, board detection procedures, validation diagnostics, and the iteration workflow so opencode can coordinate the full development cycle without manual PlatformIO invocation.

---

## Problem Frame

The LoRaWoodMoisture firmware is a PlatformIO-based ESP32 project with no automation, no CI/CD, and no agent configuration. Every build, flash, and monitor cycle requires manual command entry. An AGENTS.md file will give opencode the project context and command patterns needed to autonomously execute the development loop: build the firmware, upload it to the connected TTGO T-Beam (COM3), monitor serial output, validate the board is operational, and iterate on changes.

---

## Requirements

- R1. AGENTS.md must teach opencode the correct PlatformIO build, flash, and monitor commands for this project
- R2. AGENTS.md must define board detection and connection procedures (COM3, TTGO T-Beam v1.1)
- R3. AGENTS.md must include validation diagnostics to confirm the board is operational after flash
- R4. AGENTS.md must define the iteration workflow (change -> build -> flash -> validate)
- R5. AGENTS.md must include troubleshooting guidance for common failure modes

---

## Scope Boundaries

- No firmware feature development in this plan
- No CI/CD pipeline setup
- No test framework creation
- No lorawan_keys.h management (gitignored, user-managed)

### Deferred to Follow-Up Work

- CI/CD pipeline: separate future effort
- Hardware-in-the-loop test framework: separate future effort

---

## Context & Research

### Relevant Code and Patterns

- `platformio.ini` — single environment `ttgo-t-beam`, `espressif32` platform, `arduino` framework
- `src/main.cpp` — single-file firmware, 633 lines, sequential boot-to-sleep architecture
- `include/config.h` — all system constants (firmware v2.0.0)
- Serial baud rate: 115200 (set in `platformio.ini` and `config.h`)
- Upload speed: 921600 (set in `platformio.ini`)
- Board: TTGO T-Beam v1.1 with SX1262 LoRa radio

### Institutional Learnings

- No `docs/solutions/` exist yet — this is a fresh project
- The `.gitignore` excludes `.pio/`, `.vscode/`, `lorawan_keys.h`, and `STRATEGY.md`

### External References

- PlatformIO CLI documentation for `pio run`, `pio run --target upload`, `pio device monitor`
- TTGO T-Beam v1.1 pin configuration (CS=5, DIO1=33, RST=27, BUSY=26)

---

## Key Technical Decisions

- **AGENTS.md location**: `LoRaWoodMoisture/AGENTS.md` — at the project root alongside `platformio.ini` and `README.md`
- **PlatformIO as the build system**: No Makefile wrapper needed; PlatformIO CLI is the canonical interface
- **COM3 as default port**: The board is connected to COM3; AGENTS.md will reference this but also teach opencode to detect the port dynamically
- **Firmware version inconsistency**: `config.h` defines v2.0.0 but README says v1.0.1 — AGENTS.md will note the config.h version as authoritative

---

## Open Questions

### Resolved During Planning

- Board connection confirmed on COM3 (USB Serial Device, CH9102F USB-to-serial)
- PlatformIO is the build tool (no Makefile or custom scripts needed)

### Deferred to Implementation

- Exact serial monitor output format for validation (depends on firmware behavior at runtime)

---

## Implementation Units

### U1. Create AGENTS.md with Orchestrator Instructions

**Goal:** Create a single AGENTS.md file that gives opencode all the context and commands needed to autonomously build, flash, monitor, validate, and iterate on the LoRaWoodMoisture firmware.

**Requirements:** R1, R2, R3, R4, R5

**Dependencies:** None

**Files:**
- Create: `LoRaWoodMoisture/AGENTS.md`

**Approach:**

Write a structured AGENTS.md with these sections:

1. **Project Context** — What this project is (LoRaWAN wood moisture sensor, TTGO T-Beam v1.1, ESP32 + SX1262), the architecture (sequential boot-to-sleep), and key files
2. **Build Commands** — `pio run` to build, `pio run --target upload` to flash, `pio device monitor -b 115200` to monitor
3. **Board Detection** — How to detect the connected board (`pio device list`), expected COM port (COM3), and what to do if the board is not detected
4. **Validation Diagnostics** — What to look for in serial output after flash: firmware version print, boot reason, LoRaWAN join status, sensor readings, and successful TX
5. **Iteration Workflow** — The change-build-flash-monitor loop: edit source, build, flash, monitor serial, validate, repeat
6. **Troubleshooting** — Common failure modes: build errors, upload failures, serial monitor connection issues, LoRaWAN join failures, watchdog resets

**Patterns to follow:**
- Follow the existing README.md structure for project context (hardware table, pin configuration)
- Reference `platformio.ini` settings directly
- Use the serial output format already defined in `src/main.cpp` (DEBUG_PRINTLN patterns)

**Test scenarios:**

- Test expectation: none — AGENTS.md is a documentation file, not executable code

**Verification:**
- opencode can run `pio run` in the LoRaWoodMoisture directory and get a successful build
- opencode can detect the connected board via `pio device list`
- opencode can flash the firmware via `pio run --target upload`
- opencode can monitor serial output via `pio device monitor`
- The AGENTS.md file is readable and contains all required sections

---

## System-Wide Impact

- **Interaction graph:** AGENTS.md is read by opencode at session start; no code behavior changes
- **Error propagation:** N/A — documentation only
- **State lifecycle risks:** N/A — documentation only
- **API surface parity:** N/A — documentation only
- **Integration coverage:** N/A — documentation only
- **Unchanged invariants:** The firmware itself is not modified; only the agent's understanding of the project changes

---

## Risks & Dependencies

| Risk | Mitigation |
|------|------------|
| COM port may change if board is reconnected to a different USB port | AGENTS.md teaches dynamic port detection via `pio device list` |
| PlatformIO may not be installed on the target machine | Include PlatformIO installation check in AGENTS.md |
| lorawan_keys.h is gitignored — build will fail without it | Note in AGENTS.md that the user must provide lorawan_keys.h from the template |

---

## Documentation / Operational Notes

- AGENTS.md will be the single source of truth for opencode's interaction with this project
- The file should be committed to git (unlike lorawan_keys.h and STRATEGY.md which are gitignored)
- Future AGENTS.md updates should be made when the project structure or build process changes

---

## Sources & References

- `LoRaWoodMoisture/README.md` — comprehensive project documentation
- `LoRaWoodMoisture/platformio.ini` — build configuration
- `LoRaWoodMoisture/include/config.h` — system constants
- `LoRaWoodMoisture/src/main.cpp` — firmware source
