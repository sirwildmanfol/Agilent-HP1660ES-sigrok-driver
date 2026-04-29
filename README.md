# libsigrok HP 1660ES Driver

Native sigrok driver for the **HP 1660E/ES** logic analyzer + oscilloscope combo. Exposes all 128 LA channels (if you open the sr file in pulsewidth do not exceed 64, it's a limit of pulsewidth) + 2 analog OSC channels to PulseView/sigrok-cli via raw TCP SCPI.

```
HP 1660ES → TCP/5025 (SCPI + blob) → libsigrok → PulseView / sigrok-cli
```

**Tested on:** HP 1660E, firmware REV 02.02

---

![image](https://raw.githubusercontent.com/sirwildmanfol/Agilent-HP1660ES-sigrok-driver/refs/heads/main/Screenshot%20from%202026-04-29%2023-25-14.png)

## Build & Install

```bash
# Build the driver (inside libsigrok source tree)
git clone git://sigrok.org/libsigrok
cd libsigrok
mkdir -p src/hardware/hp1660es
copy protocol.h protocol.c api.c in src/hardware/hp1660es/
copy configure.ac and Makefile.am in the libsigrok directory
./autogen.sh
./configure --disable-all-drivers --enable-hp1660es
make
then install the compiled (.libs/libsigrok.so.4.0.0) library in your system (/usr/lib ??) 
sudo ldconfig 

**Python prototype** (reference implementation): `hp1660_acq_osc.py`, `hp1660_acq_la.py`

---

## Quick Start

### Scan

```bash
sigrok-cli --driver hp1660es:conn=tcp-raw/192.168.1.110/5025 --scan
```

### Oscilloscope — single channel

```bash
sigrok-cli --driver hp1660es:conn=tcp-raw/192.168.1.110/5025 \
  --config timebase=20us \
  --channel-group OSC1 --config vdiv=5v --config offset=0 --config coupling=DC \
  --channels OSC1 --frames 1 \
  -O srzip -o osc_ch1.sr
```

### Oscilloscope — dual channel

```bash
# Step 1: configure OSC2 (vdiv/offset/coupling)
sigrok-cli --driver hp1660es:conn=tcp-raw/192.168.1.110/5025 \
  --channel-group OSC2 --config vdiv=2v --config offset=0 --config coupling=DC \
  --channels OSC2 --frames 1 -O srzip -o /dev/null

# Step 2: acquire both with OSC1 config
sigrok-cli --driver hp1660es:conn=tcp-raw/192.168.1.110/5025 \
  --config timebase=20us \
  --channel-group OSC1 --config vdiv=5v --config offset=0 --config coupling=DC \
  --channels OSC1,OSC2 --frames 1 \
  -O srzip -o osc_dual.sr
```

> Note: sigrok-cli doesn't support multiple `--channel-group` in one invocation. Use two-pass to set per-channel parameters.

### Oscilloscope — trigger configuration

```bash
sigrok-cli --driver hp1660es:conn=tcp-raw/192.168.1.110/5025 \
  --config timebase=50ns \
  --config trigger_source=CHANnel1 \
  --config trigger_slope=POSitive \
  --config trigger_level=1.5 \
  --config horiz_triggerpos=0.2 \
  --channel-group OSC1 --config vdiv=1v \
  --channels OSC1 --frames 1 \
  -O srzip -o osc_trig.sr
```

| Option | Values |
|--------|--------|
| `trigger_source` | CHANnel1, CHANnel2, EXTernal, LINE |
| `trigger_slope` | POSitive, NEGative |
| `trigger_level` | float (volts) |
| `horiz_triggerpos` | 0.0..1.0 (0.5 = center) |

### Logic Analyzer — TIMING mode

```bash
sigrok-cli --driver hp1660es:conn=tcp-raw/192.168.1.110/5025 \
  --config samplerate=100MHz \
  --channels A1.0,A1.1,A1.2,A1.3 --frames 1 \
  -O srzip -o la_timing.sr
```

| Option | Samplerates |
|--------|-------------|
| `samplerate` | 500MHz, 250MHz, 200MHz, 100MHz, 50MHz, 40MHz, 20MHz, 10MHz, 5MHz, 4MHz, 2MHz, 1MHz, 500kHz, 200kHz, 100kHz |

Multiple pods:
```bash
sigrok-cli --driver hp1660es:conn=tcp-raw/192.168.1.110/5025 \
  --config samplerate=100MHz \
  --channels A1.0,A1.1,A2.0,A2.1 --frames 1 \
  -O srzip -o la_2pod.sr
```

### PulseView

PulseView interface force the channels to be limite to 64.
Right now there is some problem in pulseview: frequency selection menu doesn't work; when you scan for the machine all channels are selected (a bit overwhelming).
Since PulseView can't manage different sample rate in the same acquisition, at the moment, the scope signal is now visualize with the right timing scale.
All of this is not because of some bug in the driver code, but because the appropriate workaround has not yet put in place. 

### Logic Analyzer — STATE mode (external clock)

```bash
sigrok-cli --driver hp1660es:conn=tcp-raw/192.168.1.110/5025 \
  --config external_clock_source="J,RISing" \
  --config capture_ratio=0 \
  --channels A1.0,A1.1,A1.2,A1.3 --frames 1 \
  -O srzip -o la_state.sr
```

| Option | Values |
|--------|--------|
| `external_clock_source` | `<clock_id>,<edge>` — e.g. `J,RISing`, `K,FALLing`, `J,BOTH` |
| `capture_ratio` | 0..9 (setup/hold table, default 0 = 3.5/0.0 ns) |

Clock IDs: J (A1), K (A2), L (A3), M (A4), N (A7), P (A8)

### Mixed Acquisition (LA + OSC simultaneous)

```bash
sigrok-cli --driver hp1660es:conn=tcp-raw/192.168.1.110/5025 \
  --channels A1.0,A1.1,OSC1 \
  --config samplerate=100MHz --frames 1 \
  -O srzip -o mixed.sr
```

The LA acts as master trigger; the OSC is cross-armed via the Intermodule Bus. Both modules capture simultaneously from the same trigger event.

---

## Channel Map

### Logic Analyzer (128 channels)

| Pod | Group | Clock | Channels |
|-----|-------|-------|----------|
| A1 | POD_A1 | J | A1.0 .. A1.15 |
| A2 | POD_A2 | K | A2.0 .. A2.15 |
| A3 | POD_A3 | L | A3.0 .. A3.15 |
| A4 | POD_A4 | M | A4.0 .. A4.15 |
| A5 | POD_A5 | ? | A5.0 .. A5.15 |
| A6 | POD_A6 | ? | A6.0 .. A6.15 |
| A7 | POD_A7 | N | A7.0 .. A7.15 |
| A8 | POD_A8 | P | A8.0 .. A8.15 |

Pods are grouped in pairs: (A1,A2), (A3,A4), (A5,A6), (A7,A8). Unitsize is dynamically compacted — only pods with enabled channels are included in the output.

> Currently limited to 4 pods (64 channels) for PulseView compatibility.

### Oscilloscope (2 channels)

| Channel | Group |
|---------|-------|
| CH1 | OSC1 |
| CH2 | OSC2 |

---

## Known Quirks

- **:STOP blocks acquisition**: never send `:STOP` before the trigger fires naturally — `SYSTEM:DATA?` returns stale data from the previous run.
- **IMB cleanup**: after mixed acquisition, `:INTermodule:DELete ALL` is mandatory. The driver does this automatically.
- **PulseView 64-channel limit**: use `fix_sr.py` to post-process .sr files with > 64 channels.

---

## Debug

```bash
sigrok-cli --driver hp1660es:conn=tcp-raw/192.168.1.110/5025 \
  --loglevel 5 --channels A1.0 --frames 1 -O srzip -o out.sr 2>&1 | tee debug.log
```

---


## Resources

| File | Description |
|------|-------------|
| `src/hardware/hp1660es/api.c` | Driver entry points: scan, config, acquisition start/stop |
| `src/hardware/hp1660es/protocol.c` | SCPI communication, blob parsing, state machines |
| `src/hardware/hp1660es/protocol.h` | Data structures, constants, enums |

---

*Driver status: v21 — tested on HP 1660E REV 02.02. TIMING, STATE, and Mixed acquisition all validated.*
