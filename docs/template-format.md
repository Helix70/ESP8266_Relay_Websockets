# Relay Template JSON Format

Templates are stored in `data/templates/` as `.json` files and uploaded to LittleFS.

## Top-level fields

| Field | Type | Description |
|-------|------|-------------|
| `t` | string | Display name shown in the UI template picker |
| `n` | number | Number of relays this template targets. Allowed: `8` or `16` |
| `l` | array | One entry per relay, in relay order (relay 1 first) |

## Label entry fields

Each entry in `l` is a single JSON object on one line:

| Field | Type | Description |
|-------|------|-------------|
| `o` | string | Label shown when the relay is ON (max 32 chars) |
| `f` | string | Label shown when the relay is OFF (max 32 chars). If the same as `o`, a single label is displayed |
| `m` | string | `L` (latched), `I` (interlocked), or `P` (pulsed) |
| `g` | number | Interlock group number. Use `0` when mode is not `I` |
| `p` | number | Pulse duration in seconds when mode is `P`. Use `0` otherwise. Valid range: `1`–`30` |

## Example

Files must use compact JSON (no whitespace). ArduinoJson parses whitespace as tokens that consume pool memory and increase file size with no benefit.

```json
{"t":"My Template","n":8,"l":[{"o":"Antenna A","f":"Antenna A","m":"I","g":1,"p":0},{"o":"Antenna B","f":"Antenna B","m":"I","g":1,"p":0},{"o":"Reset","f":"Reset","m":"P","g":0,"p":5},{"o":"Power","f":"Power","m":"L","g":0,"p":0},{"o":"Relay 5 On","f":"Relay 5 Off","m":"L","g":0,"p":0},{"o":"Relay 6 On","f":"Relay 6 Off","m":"L","g":0,"p":0},{"o":"Relay 7 On","f":"Relay 7 Off","m":"L","g":0,"p":0},{"o":"Relay 8 On","f":"Relay 8 Off","m":"L","g":0,"p":0}]}
```

## Notes

- The firmware parses these files into a `DynamicJsonDocument(2560)` on the ESP8266. Worst-case pool usage for a 16-relay template with 32-char labels is under 2,560 bytes — do not add extra fields or the pool will overflow.
- Do not add extra fields (e.g. `_help` or comments) — they are parsed into memory and waste heap space on the device.
- Field names were compacted over several revisions to reduce LittleFS file size and ArduinoJson pool usage:
  - `title` → `t`, `labels` → `l`, `mode` → `m`, `on` → `o`, `off` → `f`
  - `relayCount` → `n`, `group` → `g`, `pulse` → `p`
  - mode values: `latched` → `L`, `interlocked` → `I`, `pulsed` → `P`
- The firmware only accepts the current short-form field names. Old templates must be re-uploaded.
