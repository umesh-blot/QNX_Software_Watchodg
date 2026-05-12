/*============================================================================*/
/* SW_COMMON.h - ASIL B Software Watchdog - Common Definitions              */
/*============================================================================*/
/* Description: AUTOSAR-compliant common header for MsgPulse-based watchdog  */
/*              with End-to-End (E2E) protection, core affinity, and         */
/*              safety-critical windowed watchdog logic.                     */
/*                                                                            */
/* AUTOSAR Module: Com_IA (AUTOSAR Communication with E2E Protection)        */
/* Safety Level: ASIL B                                                      */
/* Author: ASIL B Safety Team                                                */
/* Date: May 2026                                                            */
/*============================================================================*/

#ifndef SW_COMMON_H
#define SW_COMMON_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/iomgr.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/dispatch.h>
#include <fcntl.h>
#include <time.h>

/*============================================================================*/
/* Section 1: Configuration Parameters (AUTOSAR Com_Config)                 */
/*============================================================================*/

/* IPC Channel Configuration */
#define WATCHDOG_CHANNEL_NAME "watchdog_channel"
#define WATCHDOG_CHANNEL_PRIORITY 99

/* Core Affinity Configuration (AUTOSAR OsTask assignment) */
#define WATCHDOG_CORE 0       /* Core 0 for high-availability monitoring */
#define PRODUCER_CORE 2       /* Core 2 for isolated heartbeat generation */

/* Timing Configuration (AUTOSAR TimingExt) */
#define WATCHDOG_CYCLE_MS 400          /* Total windowed cycle: 400ms */
#define WATCHDOG_OPEN_WINDOW_MS 300    /* Open window: 0-300ms (pulses accepted) */
#define WATCHDOG_CLOSED_WINDOW_MS 100  /* Closed window: 300-400ms (pulses rejected) */
#define PRODUCER_INTERVAL_MS 200       /* Producer heartbeat: 200ms (center of open window) */
#define PRODUCER_INIT_TIMEOUT_S 10     /* Producer wait for handshake: 10 seconds */
#define WATCHDOG_FAILURE_THRESHOLD 5   /* Consecutive failures trigger safe state */

/* E2E Protection Configuration (AUTOSAR E2E Library - Profile 01) */
#define E2E_CRC_POLYNOMIAL 0xEDB88320  /* CRC-32 polynomial */
#define E2E_CRC_INIT 0xFFFFFFFF        /* Initial CRC value */
#define E2E_SEQUENCE_MAX 255           /* 8-bit sequence counter */

/*============================================================================*/
/* Section 2: Message Structure with E2E Protection (AUTOSAR Com_SignalGroup)*/
/*============================================================================*/

/* E2E Protection Header: Sequence Counter + CRC (ASIL B requirement) */
typedef struct {
    uint8_t sequence;    /* Sequence counter (0-255) - detects stale/duplicate messages */
    uint8_t reserved;    /* Reserved for alignment */
    uint16_t crc;        /* 16-bit CRC for E2E protection */
} E2E_ProtectionHeader_t;

/* Watchdog Pulse Payload: Data + Protection Header */
typedef struct {
    uint32_t timestamp_ms;              /* Monotonic timestamp from CLOCK_MONOTONIC */
    uint8_t producer_id;                /* Producer identifier (0x01) */
    E2E_ProtectionHeader_t e2e_header;  /* E2E protection: sequence + CRC */
} WDG_PulsePayload_t;

/* Message Pulse Structure (QNX MsgPulse) */
typedef struct {
    uint16_t code;               /* PULSE_CODE_WATCHDOG */
    uint16_t reserved;
    WDG_PulsePayload_t payload;  /* Embedded watchdog payload */
} WDG_MsgPulse_t;

/* Define MsgPulse codes */
#define PULSE_CODE_WATCHDOG 1
#define PULSE_CODE_SYNC_REQ 2
#define PULSE_CODE_SYNC_ACK 3

/*============================================================================*/
/* Section 3: AUTOSAR-style Watchdog State Machine                          */
/*============================================================================*/

/* Watchdog Status Type (AUTOSAR Wdm_StatusType) */
typedef enum {
    WDG_OFF = 0,           /* Watchdog disabled */
    WDG_IDLE = 1,          /* Watchdog initialized, waiting for pulses */
    WDG_MONITORING = 2,    /* Watchdog actively monitoring heartbeats */
    WDG_EXPIRED = 3,       /* Watchdog trigger threshold reached */
    WDG_ERROR = 4          /* Fatal error state */
} WDG_StatusType;

/* Watchdog Trigger Type (AUTOSAR Wdm_TriggerType) */
typedef enum {
    WDG_TRIGGER_OK = 0,         /* Normal trigger */
    WDG_TRIGGER_WINDOW_FAIL = 1, /* Pulse in closed window (safety violation) */
    WDG_TRIGGER_STALE = 2,      /* Stale/duplicate pulse detected */
    WDG_TRIGGER_CRC_FAIL = 3,   /* CRC validation failed */
    WDG_TRIGGER_SEQ_FAIL = 4,   /* Sequence counter error */
    WDG_TRIGGER_TIMEOUT = 5     /* Timeout - no pulse in open window */
} WDG_TriggerType;

/*============================================================================*/
/* Section 4: E2E Protection Functions (AUTOSAR E2E_Compute & E2E_Check)    */
/*============================================================================*/

/* CRC-32 calculation for E2E protection (polynomial: 0xEDB88320) */
static inline uint32_t SW_CRC32_Calculate(const uint8_t *data, size_t length) {
    uint32_t crc = E2E_CRC_INIT;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ E2E_CRC_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc ^ E2E_CRC_INIT;
}

/* E2E Compute: Generate CRC for outgoing message (Producer side) */
static inline void SW_E2E_Compute(WDG_PulsePayload_t *payload) {
    /* Compute CRC over timestamp and producer_id (excludes E2E header) */
    uint8_t data_to_crc[5];
    memcpy(&data_to_crc[0], &payload->timestamp_ms, 4);
    data_to_crc[4] = payload->producer_id;
    
    uint32_t crc32 = SW_CRC32_Calculate(data_to_crc, 5);
    payload->e2e_header.crc = (crc32 & 0xFFFF);  /* 16-bit CRC */
}

/* E2E Check: Validate CRC on incoming message (Watchdog side) */
static inline int SW_E2E_Check(const WDG_PulsePayload_t *payload) {
    uint8_t data_to_crc[5];
    memcpy(&data_to_crc[0], &payload->timestamp_ms, 4);
    data_to_crc[4] = payload->producer_id;
    
    uint32_t crc32 = SW_CRC32_Calculate(data_to_crc, 5);
    uint16_t computed_crc = (crc32 & 0xFFFF);
    
    if (computed_crc != payload->e2e_header.crc) {
        fprintf(stderr, "ERROR: CRC mismatch! Computed: 0x%04X, Received: 0x%04X\n",
                computed_crc, payload->e2e_header.crc);
        return -1;  /* CRC validation failed */
    }
    return 0;  /* CRC validation passed */
}

/*============================================================================*/
/* Section 5: Core Affinity Utility Functions (AUTOSAR OsTask binding)      */
/*============================================================================*/

/* Bind current thread to specified CPU core (QNX ThreadCtl API) */
static inline int SW_BindThreadToCore(int core_id) {
    int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    uint32_t runmask;
    
    if (core_id >= num_cpus || core_id < 0) {
        fprintf(stderr, "ERROR: Core %d out of range (available: 0-%d)\n", 
                core_id, num_cpus - 1);
        return -1;
    }
    
    /* QNX-specific: Create runmask for ThreadCtl() */
    runmask = (1U << core_id);
    
    /* Set thread affinity using QNX ThreadCtl() */
    if (ThreadCtl(_NTO_TCTL_RUNMASK, (void *)(uintptr_t)runmask) == -1) {
        perror("ERROR: ThreadCtl(_NTO_TCTL_RUNMASK) failed");
        return -1;
    }
    
    printf("INFO: Thread bound to CPU Core %d (runmask: 0x%08X, total cores: %d)\n", 
           core_id, runmask, num_cpus);
    return 0;
}

/*============================================================================*/
/* Section 6: AUTOSAR-style Timing Functions                                */
/*============================================================================*/

/* Get monotonic time in milliseconds (AUTOSAR Os_Counter) */
static inline uint32_t SW_GetMonotonicTimeMs(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("ERROR: clock_gettime(CLOCK_MONOTONIC) failed");
        return 0;
    }
    return (uint32_t)((ts.tv_sec * 1000UL) + (ts.tv_nsec / 1000000UL));
}

/* Calculate time delta in milliseconds with overflow handling */
static inline uint32_t SW_TimeElapsedMs(uint32_t start_ms) {
    uint32_t current_ms = SW_GetMonotonicTimeMs();
    /* Handle 32-bit overflow case */
    if (current_ms >= start_ms) {
        return current_ms - start_ms;
    } else {
        /* Overflow occurred: (UINT32_MAX - start) + current */
        return (UINT32_MAX - start_ms) + current_ms;
    }
}

/*============================================================================*/
/* Section 7: Safety-Critical Windowed Watchdog Logic                        */
/*============================================================================*/

/* Determine if current time is within open window (AUTOSAR Wdm_Trigger check) */
static inline int SW_IsInOpenWindow(uint32_t cycle_time_ms) {
    /* Cycle: 0-300ms (open), 300-400ms (closed) */
    uint32_t cycle_position = cycle_time_ms % WATCHDOG_CYCLE_MS;
    return (cycle_position < WATCHDOG_OPEN_WINDOW_MS);
}

/* Determine if current time is within closed window (safety violation detection) */
static inline int SW_IsInClosedWindow(uint32_t cycle_time_ms) {
    uint32_t cycle_position = cycle_time_ms % WATCHDOG_CYCLE_MS;
    return (cycle_position >= WATCHDOG_OPEN_WINDOW_MS) && 
           (cycle_position < WATCHDOG_CYCLE_MS);
}

/*============================================================================*/
/* Section 8: Signal-Safe Helper Functions (AUTOSAR SysService_Synchronous) */
/*============================================================================*/

/* Signal-safe write (ASIL B requirement: ERR-002) */
static inline ssize_t SW_SafeWrite(int fd, const void *buf, size_t count) {
    ssize_t result;
    do {
        result = write(fd, buf, count);
    } while (result == -1 && errno == EINTR);
    return result;
}

/* Signal-safe logging to stderr (minimal operations in signal handler) */
#define SW_SIGNAL_SAFE_LOG(msg) \
    do { \
        SW_SafeWrite(STDERR_FILENO, msg, strlen(msg)); \
    } while(0)

/*============================================================================*/
/* Section 9: Error Codes (AUTOSAR Std_ReturnType extension)               */
/*============================================================================*/

typedef uint8_t Std_ReturnType;
#define E_OK 0
#define E_NOT_OK 1
#define E_CHANNEL_NOT_FOUND 2
#define E_CRC_FAIL 3
#define E_SEQUENCE_FAIL 4
#define E_WINDOW_VIOLATION 5
#define E_HANDSHAKE_TIMEOUT 6

#endif /* SW_COMMON_H */

/*============================================================================*/
/* End of File: SW_COMMON.h                                                 */
/*============================================================================*/
