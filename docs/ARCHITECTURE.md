# ASIL B Software Watchdog System - Architecture & Design

## 1. System Overview

This document describes the architecture and design of the ASIL B Software Watchdog system implemented on QNX Neutrino RTOS using AUTOSAR principles. The system implements a **Windowed (PWM-style) Watchdog** with asynchronous heartbeat monitoring using QNX MsgPulse IPC.

### 1.1 Purpose

The software watchdog provides **safety-critical monitoring** of a Producer application using a deadline mechanism. It ensures the Producer delivers periodic heartbeats within a specified time window. If the deadline is missed or violated, the watchdog triggers a safe state transition (hardware reset).

### 1.2 Safety Criticality

- **Safety Level:** ASIL B (Automotive Safety Integrity Level B)
- **Standards:** AUTOSAR AR 4.3, QNX Neutrino 8.0
- **Compliance:** ISO 26262 (Functional Safety - Road Vehicles)
- **E2E Protection:** CRC-32 (16-bit) + 8-bit Sequence Counter per AUTOSAR E2E Profile 01

---

## 2. System Architecture

### 2.1 Component Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                     QNX Neutrino RTOS                            │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────────────┐      ┌──────────────────────────┐    │
│  │   Producer Process   │      │   Watchdog Process       │    │
│  │  (Core 2, PROD-001)  │      │   (Core 0, SYS-002)      │    │
│  │                      │      │                          │    │
│  │  ┌─────────────────┐ │      │  ┌────────────────────┐ │    │
│  │  │ Timer (200ms)   │ │      │  │ MsgReceivePulse()  │ │    │
│  │  │ CLOCK_MONOTONIC │ │      │  │ ChannelCreate()    │ │    │
│  │  └────────┬────────┘ │      │  │ name_attach()      │ │    │
│  │           │          │      │  │ Priority: 99       │ │    │
│  │  ┌────────▼────────┐ │      │  └────────┬───────────┘ │    │
│  │  │ E2E Compute:    │ │      │           │              │    │
│  │  │ - Sequence +1   │ │      │  ┌────────▼───────────┐ │    │
│  │  │ - CRC-32        │ │      │  │ Window Validation: │ │    │
│  │  └────────┬────────┘ │      │  │ - 0-300ms: Accept  │ │    │
│  │           │          │      │  │ - 300-400ms: Reject│ │    │
│  │  ┌────────▼────────┐ │      │  │ (Safety Violation) │ │    │
│  │  │MsgSendPulse()   │ │      │  └────────┬───────────┘ │    │
│  │  │(Asynchronous)   │ │      │           │              │    │
│  │  └────────┬────────┘ │      │  ┌────────▼───────────┐ │    │
│  │           │          │      │  │ E2E Check:         │ │    │
│  │           └──────────┼──────┼─►│ - CRC Validation   │ │    │
│  │                      │      │  │ - Seq Counter Check│ │    │
│  │                      │      │  └────────┬───────────┘ │    │
│  │                      │      │           │              │    │
│  │                      │      │  ┌────────▼───────────┐ │    │
│  │                      │      │  │ Failure Counter    │ │    │
│  │                      │      │  │ (Threshold: 5)     │ │    │
│  │                      │      │  │ → Safe State Trigger│ │    │
│  │                      │      │  └────────────────────┘ │    │
│  │                      │      │                          │    │
│  └──────────────────────┘      └──────────────────────────┘    │
│                                                                  │
│           ┌────────────────────────────────────────┐            │
│           │  QNX MsgPulse IPC (Asynchronous)       │            │
│           │  - Channel Name: /dev/watchdog_channel │            │
│           │  - Priority: 99 (High)                 │            │
│           └────────────────────────────────────────┘            │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

### 2.2 Process Timing Diagram

```
Timeline (milliseconds):

Cycle 1 (0-400ms):
├─ t=0-300ms ──────────────── Open Window (Pulses Accepted)
│  │
│  ├─ t≈200ms: Producer sends Pulse #1 ✓ (VALID)
│  │
│  └─ Within open window
│
├─ t=300-400ms ─────────────── Closed Window (Pulses Rejected)
│  │
│  └─ Any pulse here = SAFETY VIOLATION ✗
│
Cycle 2 (400-800ms):
├─ t=400-700ms ───────────── Open Window
│  │
│  ├─ t≈600ms: Producer sends Pulse #2 ✓ (VALID)
│  │
│  └─ No pulse = TIMEOUT ✗
│
└─ t=700-800ms ────────────── Closed Window

Legend:
✓ Valid pulse (within open window, CRC OK, sequence OK)
✗ Invalid pulse (safety violation)
```

### 2.3 E2E Protection Structure

```
Watchdog Pulse Message (QNX MsgPulse):

┌─────────────────────────────────────────────┐
│  uint16_t code = PULSE_CODE_WATCHDOG (1)    │ 2 bytes
├─────────────────────────────────────────────┤
│  uint16_t reserved                          │ 2 bytes
├─────────────────────────────────────────────┤
│           WDG_PulsePayload_t                │
├─────────────────────────────────────────────┤
│  uint32_t timestamp_ms                      │ 4 bytes (AUTOSAR Counter)
│  uint8_t  producer_id = 0x01                │ 1 byte
│  ────────────────────────────────────────   │
│  E2E Protection Header:                     │
│  ├─ uint8_t sequence (0-255)                │ 1 byte
│  ├─ uint8_t reserved                        │ 1 byte
│  └─ uint16_t crc (16-bit CRC-32)            │ 2 bytes
│                                             │
│  CRC covers: [timestamp_ms || producer_id] │
│  Polynomial: 0xEDB88320 (CRC-32)           │
│  Init Value: 0xFFFFFFFF                     │
└─────────────────────────────────────────────┘

Total: 12 bytes per pulse
```

---

## 3. AUTOSAR Mapping

### 3.1 AUTOSAR Module Classification

| AUTOSAR Module | Implementation | Purpose |
|---|---|---|
| **Wdm** (Watchdog Manager) | SW_Watchdog.cpp | Main watchdog consumer logic |
| **Com_IA** (Communication with E2E) | SW_COMMON.h, SW_Producer.cpp | E2E protection and IPC |
| **Os** (Operating System) | SW_COMMON.h | Task timing, core affinity |
| **SysService** | SW_COMMON.h, both apps | Signal handling, resource management |
| **IpcCom** | SW_Watchdog.cpp, SW_Producer.cpp | MsgPulse channel communication |

### 3.2 AUTOSAR Runnable to Function Mapping

| AUTOSAR Runnable | Implementation | Timing |
|---|---|---|
| `Wdm_MainFunction` | `SW_WatchdogMainLoop()` | Event-triggered (MsgReceivePulse) |
| `Com_SendSignal` | `SW_PrepareWDGPulse()` | 200ms periodic |
| `Com_ReceiveSignal` | `SW_ValidateWDGPulse()` | Event-triggered |
| `E2E_Compute` | `SW_E2E_Compute()` | Per-message (200ms) |
| `E2E_Check` | `SW_E2E_Check()` | Per-reception |

---

## 4. Core Affinity & Multi-Core Isolation

### 4.1 Core Assignment Strategy

```
┌────────────────────────────────────────────┐
│   Multi-Core Processor (4 cores)           │
├────────────────────────────────────────────┤
│                                            │
│  Core 0: Watchdog (High-Priority Monitor) │
│  ├─ Affinity: 0x01 (Core 0 only)          │
│  ├─ ThreadCtl(_NTO_TCTL_RUNMASK, 0x01)    │
│  └─ Dedicated to watchdog monitoring      │
│                                            │
│  Core 1: Unused (Available)                │
│                                            │
│  Core 2: Producer (Heartbeat Generation)   │
│  ├─ Affinity: 0x04 (Core 2 only)          │
│  ├─ ThreadCtl(_NTO_TCTL_RUNMASK, 0x04)    │
│  └─ Isolated from watchdog interference   │
│                                            │
│  Core 3: System / Other tasks             │
│                                            │
└────────────────────────────────────────────┘
```

**Benefit:** Temporal isolation prevents context switching interference on watchdog.

### 4.2 QNX Thread Affinity Configuration

```c
/* QNX-specific API for core affinity */
uint32_t runmask = (1U << core_id);  /* Create mask for core */
ThreadCtl(_NTO_TCTL_RUNMASK, (void *)(uintptr_t)runmask);
```

---

## 5. Safety-Critical Requirements Compliance

### 5.1 SYS-001: MsgPulse IPC

**Requirement:** The system shall facilitate IPC via QNX MsgPulse for asynchronous watchdog heartbeats.

**Implementation:**
- Producer sends pulse asynchronously: `MsgSendPulse(watchdog_connection, -1, PULSE_CODE_WATCHDOG, ...)`
- Watchdog receives: `MsgReceivePulse(channel_id, &pulse, sizeof(pulse), &timeout_ms)`
- Non-blocking, minimal latency
- Supports warm reconnection (CONS-004)

### 5.2 SYS-004: Windowed Watchdog Logic

**Requirement:** Cycle: 400ms. Open: 0-300ms. Closed: 300-400ms.

**Implementation:**
```c
static inline int SW_IsInOpenWindow(uint32_t cycle_time_ms) {
    uint32_t cycle_position = cycle_time_ms % WATCHDOG_CYCLE_MS;
    return (cycle_position < WATCHDOG_OPEN_WINDOW_MS);  /* < 300ms */
}
```

**Validation:** Any pulse received in closed window (300-400ms) triggers failure.

### 5.3 MEM-001: E2E Pulse Protection (ASIL B)

**Requirement:** 8-bit Sequence Counter + 8-bit CRC for data integrity.

**Implementation:**
- Sequence counter: Incremented per pulse, detects duplicates/stale messages
- CRC-32 (16-bit truncated): Polynomial 0xEDB88320, covers data payload
- Failure actions:
  - CRC mismatch: `E_CRC_FAIL` → Failure counter +1
  - Sequence error: `E_SEQUENCE_FAIL` → Failure counter +1
  - 5 consecutive failures: Trigger safe state

### 5.4 CONS-002: Failure Handling

**Requirement:** 5 consecutive failures → safe state transition.

**Implementation:**
```c
if (failure_counter >= WATCHDOG_FAILURE_THRESHOLD) {
    fprintf(stderr, "FATAL: Watchdog failure threshold exceeded!\n");
    wdg_status = WDG_EXPIRED;
    exit(EXIT_FAILURE);  /* Simulated; production: HW reset */
}
```

**Failure Types:**
1. `WDG_TRIGGER_WINDOW_FAIL` - Pulse in closed window
2. `WDG_TRIGGER_STALE` - Stale/duplicate pulse
3. `WDG_TRIGGER_CRC_FAIL` - CRC validation failed
4. `WDG_TRIGGER_SEQ_FAIL` - Sequence counter error
5. `WDG_TRIGGER_TIMEOUT` - No pulse in open window

### 5.5 ERR-002: Signal-Safe Error Handling

**Requirement:** Signal handlers limited to async-signal-safe operations.

**Implementation:**
- Signal handlers set atomic flag: `signal_shutdown = 1`
- Main loop checks flag and performs cleanup
- No malloc/free/printf in signal handler
- Signal-safe logging via `SW_SafeWrite()` (write syscall)

```c
void SignalHandler(int sig) {
    signal_shutdown = 1;
    SW_SIGNAL_SAFE_LOG("SIGNAL: Shutdown requested\n");
}
```

---

## 6. Build System Architecture

### 6.1 Makefile Structure

```
Makefile
├─ QNX SDK Configuration
│  ├─ QNX_HOST (windows/linux)
│  ├─ QNX_TARGET (qnx)
│  ├─ ARCH (aarch64le, x86_64)
│  └─ Toolchain paths
│
├─ Safety Flags
│  ├─ -D_ASIL_B
│  ├─ -D_SAFETY_CRITICAL
│  ├─ -fstack-protector-strong
│  └─ -fcf-protection=full
│
├─ Build Targets
│  ├─ SW_WDG (Watchdog executable)
│  └─ SW_Producer (Producer executable)
│
└─ Architecture Output
   └─ build/{ARCH}/bin/ (deliverables)
      ├─ SW_WDG
      └─ SW_Producer
```

### 6.2 Cross-Compilation Flow (Windows to QNX ARM64)

```
Windows Host (MSYS2/Cygwin)
        │
        ├─ build-qnx.bat
        │  ├─ Source QNX SDK env: qnxsdp-env.bat
        │  ├─ Set QNX_HOST=windows, QNX_TARGET=qnx
        │  ├─ ARCH=aarch64le
        │  └─ Call: make ARCH=aarch64le
        │
        └─ Makefile
           ├─ Compile: $(CXX) [aarch64le-g++]
           ├─ Link: SW_WDG, SW_Producer
           └─ Output: build/aarch64le/bin/
              └─ QNX Neutrino ARM64 ELF binaries

Deployment:
        └─ SCP to Target Device (IP: 169.254.149.71)
           └─ /data/home/root/{SW_WDG, SW_Producer}
```

---

## 7. Failure Mode & Effects Analysis (FMEA)

### 7.1 Critical Failure Modes

| Failure Mode | Severity | Detection | Mitigation |
|---|---|---|---|
| Producer stops sending pulses | **Critical** | Timeout (300ms+) | Safe state on threshold |
| Pulse in closed window | **Critical** | Window check | Immediate failure count +1 |
| CRC corruption | **High** | CRC validation | Failure count +1 |
| Sequence discontinuity | **High** | Seq counter check | Stale pulse detection |
| Core affinity loss | **High** | Periodic check (future) | Rebind on watchdog startup |
| Signal handler blocks | **Critical** | Code inspection | Signal-safe ops only |

### 7.2 Safe State Transition

```
Failure Detection
       │
       ├─ failure_counter++
       │
       ├─ failure_counter < 5?
       │  └─ Continue monitoring
       │
       └─ failure_counter >= 5?
          ├─ Set wdg_status = WDG_EXPIRED
          ├─ Log: "FATAL: Watchdog failure threshold exceeded"
          ├─ TODO: Trigger HW watchdog reset
          ├─ TODO: Cut power/apply brake (vehicle context)
          └─ exit(EXIT_FAILURE)
```

---

## 8. Future Enhancements

1. **Hardware Integration:**
   - Trigger external watchdog timer on safe state
   - GPIO control for safety relays

2. **Diagnostic Logging:**
   - Circular buffer for failure history
   - Performance metrics (min/max latency)

3. **AUTOSAR Compliance:**
   - Formal test matrix per ISO 26262
   - State machine documentation
   - FMEA attestation

4. **Adaptive Timing:**
   - Runtime window adjustment
   - Configurable cycle times via NV storage (AUTOSAR NvM)

---

## 9. References

1. ISO 26262:2018 - Functional Safety - Road Vehicles
2. AUTOSAR Standard Release 4.3
3. QNX Neutrino RTOS 8.0 System Architecture Guide
4. MsgPulse IPC Documentation (QNX Developer Manual)
5. AUTOSAR E2E Protection Profile 01

---

**Document Version:** 1.0  
**Date:** May 2026  
**Compliance:** ASIL B, ISO 26262, AUTOSAR AR 4.3

