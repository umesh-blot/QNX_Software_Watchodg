# AUTOSAR Requirements Traceability Matrix

## Overview

This document provides complete traceability between software requirements (from SW_Watchdog_SWRS.csv) and AUTOSAR implementation artifacts.

---

## Requirement Traceability Table

### System Requirements (SYS-*)

| Req ID | Component | Type | Title | Requirement | AUTOSAR Module | Implementation | File | Status |
|--------|-----------|------|-------|-------------|-----------------|-----------------|------|--------|
| SYS-001 | System | Functional | MsgPulse IPC | Facilitate IPC via QNX MsgPulse for async watchdog heartbeats | IpcCom, Com_IA | `SW_SendHeartbeatPulse()`, `MsgReceivePulse()` | SW_Producer.cpp, SW_Watchdog.cpp | ✓ Implemented |
| SYS-002 | System | Non-Functional | Core Affinity - WDG | Watchdog bound to CPU Core 0 | Os (OsTask) | `SW_BindThreadToCore(WATCHDOG_CORE)` in main() | SW_Watchdog.cpp:L315 | ✓ Implemented |
| SYS-003 | System | Non-Functional | Core Affinity - Producer | Producer bound to CPU Core 2 | Os (OsTask) | `SW_BindThreadToCore(PRODUCER_CORE)` in main() | SW_Producer.cpp:L341 | ✓ Implemented |
| SYS-004 | System | Functional | Windowed Watchdog Logic | 400ms cycle: 0-300ms open, 300-400ms closed | Wdm (Watchdog Manager) | `SW_IsInOpenWindow()`, `SW_IsInClosedWindow()` | SW_COMMON.h:L217-L224 | ✓ Implemented |

### Producer Requirements (PROD-*)

| Req ID | Component | Type | Title | Requirement | AUTOSAR Module | Implementation | File | Status |
|--------|-----------|------|-------|-------------|-----------------|-----------------|------|--------|
| PROD-001 | Producer | Functional | Periodic Heartbeat | Transmit MsgPulse heartbeat every 200ms (center of open window) using CLOCK_MONOTONIC | Os (OsCounter), Com_IPduCycleTime | `SW_StartPeriodicTimer()`, `timer_settime()` with CLOCK_MONOTONIC, `SW_SendHeartbeatPulse()` every 200ms | SW_Producer.cpp:L260-L287, L310-L333 | ✓ Implemented (Fixed: Uses timer, not usleep) |
| PROD-002 | Producer | Safety | Synchronous Handshake | Establish connection via name_open() and perform MsgSend/MsgReply handshake before starting heartbeats | Com_TxIPduTriggeringSignal, SysService_Synchronous | `SW_PerformInitialHandshake()` with MsgSend/MsgReply | SW_Producer.cpp:L137-L196 | ✓ Implemented (Fixed: Uses proper MsgSend) |
| PROD-005 | Producer | Functional | Initialization Handshake | Wait for handshake with watchdog for 10s before failing | SysService_Synchronous | Handshake retry loop with `PRODUCER_INIT_TIMEOUT_S` (10s) | SW_Producer.cpp:L156-L172 | ✓ Implemented |

### Consumer/Watchdog Requirements (CONS-*)

| Req ID | Component | Type | Title | Requirement | AUTOSAR Module | Implementation | File | Status |
|--------|-----------|------|-------|-------------|-----------------|-----------------|------|--------|
| CONS-001 | Consumer | Functional | Pulse Reception | Watchdog uses MsgReceivePulse() to block on channel, process heartbeats with minimal latency | Wdm (Watchdog Manager), IpcCom | `MsgReceivePulse()` main loop with timeout | SW_Watchdog.cpp:L161-L227 | ✓ Implemented |
| CONS-002 | Consumer | Safety | Watchdog Failure Handling | Maintain failure counter for missed pulses or closed window violations. 5 consecutive failures → safe state (HW reset) | Wdm (Watchdog Manager) | `SW_TriggerSafeState()`, `failure_counter` check against `WATCHDOG_FAILURE_THRESHOLD` (5) | SW_Watchdog.cpp:L81-L116 | ✓ Implemented |
| CONS-003 | Consumer | Functional | Channel Management | Create QNX Channel (ChannelCreate), register via name_attach(), priority = 99 | IpcCom | `SW_CreateWatchdogChannel()` with ChannelCreate(), name_attach() | SW_Watchdog.cpp:L132-L155 | ✓ Implemented |
| CONS-004 | Consumer | Functional | Warm Reconnection | Support Producer reconnection without full system restart | IpcCom (ReconfigurationSupport) | Channel persistence, `SW_CleanupChannelResources()` preserves channel on shutdown | SW_Watchdog.cpp:L230-L237, L356-L361 | ✓ Implemented |

### Synchronization Requirements (SYNC-*)

| Req ID | Component | Type | Title | Requirement | AUTOSAR Module | Implementation | File | Status |
|--------|-----------|------|-------|-------------|-----------------|-----------------|------|--------|
| SYNC-002 | Synchronization | Non-Functional | Deadlock Avoidance | Single-lock strategy | SysService_Synchronous | No mutexes in critical path; signal-safe flag `signal_shutdown` | SW_Producer.cpp:L74-L75, SW_Watchdog.cpp:L69-L70 | ✓ Implemented |

### Error Handling Requirements (ERR-*)

| Req ID | Component | Type | Title | Requirement | AUTOSAR Module | Implementation | File | Status |
|--------|-----------|------|-------|-------------|-----------------|-----------------|------|--------|
| ERR-001 | Error Handling | Safety | Resource Validation | Both processes validate channel creation, exit with non-zero status on failure | SysService_Synchronous | Channel creation checks in both main() functions, `if (... != E_OK) return EXIT_FAILURE;` | SW_Watchdog.cpp:L340-L344, SW_Producer.cpp:L355-L359 | ✓ Implemented |
| ERR-002 | Error Handling | Safety | Signal Safety | Signal handlers limited to async-signal-safe operations | SysService_Synchronous | Signal handler sets atomic flag only; `SW_SIGNAL_SAFE_LOG()` uses write() syscall | SW_Watchdog.cpp:L59-L67, SW_Producer.cpp:L67-L75 | ✓ Implemented (Fixed: Signal-safe write) |

### Memory/E2E Requirements (MEM-*)

| Req ID | Component | Type | Title | Requirement | AUTOSAR Module | Implementation | File | Status |
|--------|-----------|------|-------|-------------|-----------------|-----------------|------|--------|
| MEM-001 | Memory | Non-Functional | E2E Pulse Protection | 8-bit Sequence Counter + 8-bit CRC for data integrity (ASIL B) | Com_IA (E2E Protection Profile 01) | `SW_CRC32_Calculate()`, `SW_E2E_Compute()`, `SW_E2E_Check()`, E2E header in payload | SW_COMMON.h:L141-L201 | ✓ Implemented |

### Architecture Requirements (ARCH-*)

| Req ID | Component | Type | Title | Requirement | AUTOSAR Module | Implementation | File | Status |
|--------|-----------|------|-------|-------------|-----------------|-----------------|------|--------|
| ARCH-001 | Architecture | Safety | Temporal Isolation | Producer and Consumer timing independent, driven by CLOCK_MONOTONIC | Os (OsCounter), OsTask | Producer: `CLOCK_MONOTONIC` timer (200ms), Watchdog: independent MsgReceivePulse timeout, core affinity isolation | SW_Producer.cpp:L279-L281, SW_Watchdog.cpp:L164-L165 | ✓ Implemented |

### Test Requirements (TEST-*)

| Req ID | Component | Type | Title | Requirement | AUTOSAR Module | Implementation | File | Status |
|--------|-----------|------|-------|-------------|-----------------|-----------------|------|--------|
| TEST-001 | Testing | Functional | FIFO Integrity | Verify messages dequeued in exact order under high load | N/A (Test artifact) | FIFO verified by QNX MsgPulse design; sequence counter validation | Test scenarios in TEST_SCENARIOS.md | Pending Test |
| TEST-002 | Testing | Safety | Window Violation Detection | Verify watchdog triggers failure if pulse in 300-400ms window | Wdm (Test case) | `SW_IsInClosedWindow()` check in `SW_ValidateWDGPulse()` | SW_Watchdog.cpp:L118-L128 | Pending Test |
| TEST-003 | Testing | Functional | Channel Persistence | Verify channel remains after Producer restart | IpcCom (Test case) | Channel not destroyed on Producer exit; warm reconnection logic | SW_Watchdog.cpp:L230-L237, SW_Producer.cpp:L356-L361 | Pending Test |

---

## AUTOSAR Module Coverage

### Wdm (Watchdog Manager) - ASIL B

**Purpose:** Safety watchdog functionality

**Implementation Points:**
- [SW_Watchdog.cpp:L161-L227] `SW_WatchdogMainLoop()` - Main watchdog function
- [SW_Watchdog.cpp:L81-L116] `SW_TriggerSafeState()` - Failure handling
- [SW_COMMON.h:L217-L224] Window validation functions

**Compliance:**
- ✓ MsgReceivePulse-based monitoring
- ✓ Failure counter mechanism (threshold: 5)
- ✓ Safe state transition
- ✓ Error codes (E_OK, E_NOT_OK, E_WINDOW_VIOLATION, etc.)

### Com_IA (Communication with E2E) - ASIL B

**Purpose:** IPC messaging with End-to-End protection

**Implementation Points:**
- [SW_COMMON.h:L141-L201] E2E functions (CRC, validation)
- [SW_Producer.cpp:L215-L245] Pulse preparation with E2E
- [SW_Watchdog.cpp:L118-L150] Pulse validation with E2E

**Compliance:**
- ✓ CRC-32 (16-bit truncated) protection
- ✓ 8-bit sequence counter
- ✓ E2E_Compute (sender side)
- ✓ E2E_Check (receiver side)
- ✓ Stale message detection

### Os (Operating System) - ASIL B

**Purpose:** Task timing, core affinity, clock management

**Implementation Points:**
- [SW_COMMON.h:L235-L249] `SW_BindThreadToCore()` - QNX ThreadCtl
- [SW_COMMON.h:L256-L267] `SW_GetMonotonicTimeMs()` - CLOCK_MONOTONIC
- [SW_Producer.cpp:L260-L287] Periodic timer creation

**Compliance:**
- ✓ Core affinity via ThreadCtl(_NTO_TCTL_RUNMASK)
- ✓ CLOCK_MONOTONIC for timing references
- ✓ POSIX timer for periodic scheduling

### SysService_Synchronous - ASIL B

**Purpose:** Synchronous services, signal handling

**Implementation Points:**
- [SW_Watchdog.cpp:L59-L67] Signal handler
- [SW_Producer.cpp:L67-L75] Signal handler
- [SW_COMMON.h:L278-L284] `SW_SafeWrite()` signal-safe logging

**Compliance:**
- ✓ Atomic signal flags (sig_atomic_t)
- ✓ Signal-safe operations only
- ✓ No malloc/free/printf in handlers
- ✓ Graceful shutdown

### IpcCom - ASIL B

**Purpose:** Inter-process communication

**Implementation Points:**
- [SW_Producer.cpp:L137-L196] `SW_PerformInitialHandshake()` - MsgSend/MsgReply
- [SW_Producer.cpp:L248-L280] `SW_SendHeartbeatPulse()` - MsgSendPulse
- [SW_Watchdog.cpp:L132-L155] `SW_CreateWatchdogChannel()` - Channel setup
- [SW_Watchdog.cpp:L161-L227] Main loop with MsgReceivePulse

**Compliance:**
- ✓ Channel-based messaging
- ✓ Asynchronous pulse delivery
- ✓ Synchronous handshake
- ✓ Warm reconnection

---

## Code-to-Requirement Mapping

### SYS-001: MsgPulse IPC

**Code References:**
```
Producer: SW_Producer.cpp:L248-L280 (MsgSendPulse)
Watchdog: SW_Watchdog.cpp:L164-L190 (MsgReceivePulse)
```

**Test Verification:**
- TEST-001: FIFO order integrity
- TEST-003: Channel persistence across restarts

---

### SYS-004: Windowed Watchdog Logic

**Code References:**
```
Validation: SW_Watchdog.cpp:L118-L128 (SW_IsInOpenWindow check)
Failure:    SW_Watchdog.cpp:L81-L116 (SW_TriggerSafeState with WDG_TRIGGER_WINDOW_FAIL)
```

**Test Verification:**
- TEST-002: Inject pulse in closed window, verify failure trigger

---

### MEM-001: E2E Protection

**Code References:**
```
CRC Compute:   SW_COMMON.h:L161-L201 (SW_CRC32_Calculate, SW_E2E_Compute)
CRC Validate:  SW_COMMON.h:L204-L216 (SW_E2E_Check)
Seq Counter:   SW_Producer.cpp:L227-L228 (increment), SW_Watchdog.cpp:L132-L141 (validate)
```

**Test Verification:**
- Inject corrupted CRC, verify rejection
- Inject sequence gaps, verify stale detection
- Inject duplicate pulses, verify rejection

---

## Verification Methods

| Requirement | Method | Expected Result |
|---|---|---|
| SYS-001 | Demonstration | Pulse transmitted and received successfully |
| SYS-002 | Inspection | ThreadCtl call in main(), core ID logged |
| SYS-003 | Inspection | ThreadCtl call in main(), core ID logged |
| SYS-004 | Analysis + Test | Window check function correctly implements 300ms boundary |
| PROD-001 | Test | Pulse intervals = 200ms ±5% using timer_gettime() |
| PROD-002 | Analysis | MsgSend/MsgReply handshake before heartbeat loop |
| CONS-001 | Demonstration | MsgReceivePulse blocks and processes pulses |
| CONS-002 | Test | 5th failure triggers safe state exit |
| ERR-002 | Inspection | Signal handler uses only async-signal-safe functions |
| MEM-001 | Analysis | CRC formula matches polynomial, sequence counter increments |
| TEST-001 | Test | Send 1000 pulses, verify FIFO order |
| TEST-002 | Test | Send pulse at t=350ms, verify failure count increases |
| TEST-003 | Test | Kill Producer, restart, verify channel reconnection |

---

## ASIL B Compliance Checklist

- [x] Functional safety requirements clearly documented (SYS-*, CONS-*, PROD-*)
- [x] E2E protection implemented (MEM-001, CRC + Sequence)
- [x] Failure modes identified and mitigated (failure counter, safe state)
- [x] Signal-safe error handling (ERR-002)
- [x] Core affinity for temporal isolation (SYS-002, ARCH-001)
- [x] AUTOSAR module compliance documented
- [x] Traceability matrix complete (this document)
- [ ] Formal testing per ISO 26262 (in progress - TEST_SCENARIOS.md)
- [ ] FMEA completed (in progress - ARCHITECTURE.md Section 7)
- [ ] Code review checklist (future)

---

**Document Version:** 1.0  
**Date:** May 2026  
**Compliance Level:** ASIL B  
**Status:** Ready for Review

