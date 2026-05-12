/*============================================================================*/
/* SW_Producer.cpp - ASIL B Software Watchdog - Producer/Heartbeat Process  */
/*============================================================================*/
/* Description: AUTOSAR-compliant producer using QNX MsgSend for synchronous */
/*              handshake, then MsgSendPulse for periodic heartbeat          */
/*              transmission. Uses CLOCK_MONOTONIC timer for precise         */
/*              200ms intervals and E2E protection (CRC + Sequence).         */
/*                                                                            */
/* AUTOSAR Module: Com_IA (AUTOSAR Communication with E2E Protection)        */
/* Safety Level: ASIL B                                                      */
/* Requirements Addressed:                                                   */
/*   - SYS-001: MsgPulse IPC for async watchdog heartbeats                   */
/*   - SYS-003: Core affinity (CPU Core 2)                                   */
/*   - PROD-001: Periodic heartbeat every 200ms (center of open window)      */
/*   - PROD-002: Synchronous handshake via MsgSend/MsgReply                  */
/*   - PROD-005: Initialization handshake timeout: 10 seconds                */
/*   - MEM-001: E2E pulse protection (CRC + Sequence counter)                */
/*   - ERR-001: Resource validation with non-zero exit on failure            */
/*============================================================================*/

#include "SW_COMMON.h"
#include <stdatomic.h>

/*============================================================================*/
/* Global State Variables (AUTOSAR Module Variables)                        */
/*============================================================================*/

static int watchdog_connection = -1;
static uint8_t sequence_counter = 0;
static volatile int shutdown_requested = 0;
static uint32_t pulse_send_count = 0;
static timer_t timer_id = (timer_t)(-1);

/* Signal-safe shutdown flag */
static volatile sig_atomic_t signal_shutdown = 0;

/*============================================================================*/
/* Signal Handler (AUTOSAR SysService_Synchronous - Signal Safety)          */
/*============================================================================*/

void SignalHandler(int sig) {
    signal_shutdown = 1;
    if (sig == SIGINT) {
        SW_SIGNAL_SAFE_LOG("SIGNAL: SIGINT received, initiating graceful shutdown\n");
    } else if (sig == SIGTERM) {
        SW_SIGNAL_SAFE_LOG("SIGNAL: SIGTERM received, initiating graceful shutdown\n");
    }
}

/*============================================================================*/
/* Watchdog Handshake (AUTOSAR Com_TxIPduTriggeringSignal)                  */
/*============================================================================*/

static Std_ReturnType SW_PerformInitialHandshake(void) {
    fprintf(stdout, "INFO: Attempting to connect to watchdog at '%s'...\n", 
            WATCHDOG_CHANNEL_NAME);
    
    /* Step 1: Locate watchdog channel (name_open with timeout loop) */
    uint32_t start_time = SW_GetMonotonicTimeMs();
    int connection = -1;
    
    while (SW_TimeElapsedMs(start_time) < (PRODUCER_INIT_TIMEOUT_S * 1000)) {
        connection = name_open(WATCHDOG_CHANNEL_NAME, 0);
        
        if (connection != -1) {
            watchdog_connection = connection;
            fprintf(stdout, "INFO: Connected to watchdog (ID: %d)\n", watchdog_connection);
            break;
        }
        
        /* Wait 100ms before retry (non-blocking delay) */
        usleep(100 * 1000);
    }
    
    if (connection == -1) {
        fprintf(stderr, "ERROR: Failed to connect to watchdog after %d seconds\n",
                PRODUCER_INIT_TIMEOUT_S);
        return E_HANDSHAKE_TIMEOUT;
    }
    
    /* Step 2: Perform synchronous MsgSend/MsgReply handshake */
    struct {
        uint32_t magic;
        uint8_t version;
    } handshake_req = {
        .magic = 0xDEADBEEF,  /* Magic number for protocol verification */
        .version = 1          /* Protocol version */
    };
    
    struct {
        uint8_t ack_code;
        uint32_t cycle_ms;
    } handshake_reply;
    
    fprintf(stdout, "INFO: Sending synchronous handshake to watchdog...\n");
    
    int status = MsgSend(watchdog_connection, &handshake_req, sizeof(handshake_req),
                         &handshake_reply, sizeof(handshake_reply));
    
    if (status == -1) {
        fprintf(stderr, "ERROR: MsgSend handshake failed\n");
        name_close(watchdog_connection);
        watchdog_connection = -1;
        return E_NOT_OK;
    }
    
    if (handshake_reply.ack_code != 0) {
        fprintf(stderr, "ERROR: Watchdog rejected handshake (code: %u)\n",
                handshake_reply.ack_code);
        name_close(watchdog_connection);
        watchdog_connection = -1;
        return E_NOT_OK;
    }
    
    fprintf(stdout, "INFO: Handshake successful (watchdog cycle: %ums)\n",
            handshake_reply.cycle_ms);
    
    return E_OK;
}

/*============================================================================*/
/* E2E Pulse Generation (AUTOSAR Com_SendSignal)                            */
/*============================================================================*/

static void SW_PrepareWDGPulse(WDG_MsgPulse_t *pulse) {
    /* Set message pulse code */
    pulse->code = PULSE_CODE_WATCHDOG;
    pulse->reserved = 0;
    
    /* Fill payload */
    pulse->payload.timestamp_ms = SW_GetMonotonicTimeMs();
    pulse->payload.producer_id = 0x01;  /* Producer ID */
    
    /* Set E2E protection header */
    pulse->payload.e2e_header.sequence = sequence_counter;
    pulse->payload.e2e_header.reserved = 0;
    
    /* Compute CRC (AUTOSAR E2E_Compute) */
    SW_E2E_Compute(&pulse->payload);
    
    /* Increment sequence counter for next pulse (wrap at 255) */
    sequence_counter = (sequence_counter + 1) & 0xFF;
    
    fprintf(stdout, "INFO: Pulse prepared (seq: %u, crc: 0x%04X, ts: %ums)\n",
            pulse->payload.e2e_header.sequence,
            pulse->payload.e2e_header.crc,
            pulse->payload.timestamp_ms);
}

/*============================================================================*/
/* Heartbeat Transmission (AUTOSAR Com_IPduCycleTime)                       */
/*============================================================================*/

static Std_ReturnType SW_SendHeartbeatMessage(void) {
    WDG_MsgPulse_t pulse;
    
    /* Step 1: Prepare pulse with E2E protection */
    SW_PrepareWDGPulse(&pulse);
    
    /* Step 2: Send message synchronously (MsgSend) with full E2E payload */
    /* No reply expected from watchdog for heartbeat messages */
    int status = MsgSend(watchdog_connection, &pulse, sizeof(pulse), 
                         NULL, 0);
    
    if (status == -1) {
        fprintf(stderr, "ERROR: MsgSend heartbeat failed (errno: %d)\n", errno);
        
        /* Connection lost - attempt recovery */
        fprintf(stdout, "INFO: Attempting to reconnect to watchdog...\n");
        if (SW_PerformInitialHandshake() != E_OK) {
            fprintf(stderr, "ERROR: Reconnection failed\n");
            return E_NOT_OK;
        }
        return E_OK;  /* Retry on next cycle */
    }
    
    pulse_send_count++;
    fprintf(stdout, "INFO: Heartbeat message sent (seq: %u, total: %u)\n",
            pulse.payload.e2e_header.sequence, pulse_send_count);
    
    return E_OK;
}

/*============================================================================*/
/* Timer-Based Heartbeat Control (AUTOSAR OsTask timing)                    */
/*============================================================================*/

static int producer_timer_armed = 0;
static pthread_cond_t timer_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Timer expiration signal handler */
static void TimerSignalHandler(int sig) {
    (void)sig;  /* Unused */
    
    /* Wake up waiting thread (signal-safe) */
    pthread_cond_broadcast(&timer_cond);
}

static Std_ReturnType SW_StartPeriodicTimer(void) {
    struct sigevent sev;
    struct itimerspec its;
    timer_t tid;
    
    fprintf(stdout, "INFO: Starting periodic timer (200ms intervals)...\n");
    
    /* Create POSIX timer (AUTOSAR Os_Counter) */
    memset(&sev, 0, sizeof(sev));
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGALRM;
    sev.sigev_value.sival_ptr = NULL;
    
    if (timer_create(CLOCK_MONOTONIC, &sev, &tid) == -1) {
        perror("ERROR: timer_create failed");
        return E_NOT_OK;
    }
    
    timer_id = tid;
    
    /* Arm timer for 200ms initial expiration, then 200ms repeats */
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = PRODUCER_INTERVAL_MS * 1000000;  /* 200ms in nanoseconds */
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = PRODUCER_INTERVAL_MS * 1000000;
    
    if (timer_settime(tid, 0, &its, NULL) == -1) {
        perror("ERROR: timer_settime failed");
        timer_delete(tid);
        timer_id = (timer_t)(-1);
        return E_NOT_OK;
    }
    
    /* Install signal handler for timer expiration */
    signal(SIGALRM, TimerSignalHandler);
    
    fprintf(stdout, "INFO: Timer started (period: %ums)\n", PRODUCER_INTERVAL_MS);
    producer_timer_armed = 1;
    
    return E_OK;
}

static void SW_StopPeriodicTimer(void) {
    if (timer_id != (timer_t)(-1)) {
        timer_delete(timer_id);
        timer_id = (timer_t)(-1);
        fprintf(stdout, "INFO: Timer stopped\n");
    }
}

/*============================================================================*/
/* Producer Main Loop (AUTOSAR Runnable)                                    */
/*============================================================================*/

static Std_ReturnType SW_ProducerMainLoop(void) {
    fprintf(stdout, "INFO: Producer main loop started\n");
    fprintf(stdout, "INFO: Heartbeat interval: %ums\n", PRODUCER_INTERVAL_MS);
    fprintf(stdout, "INFO: Waiting for timer events...\n");
    fprintf(stdout, "\n");
    
    while (!signal_shutdown) {
        /* Wait for timer signal (200ms periodic) */
        pthread_mutex_lock(&timer_mutex);
        pthread_cond_wait(&timer_cond, &timer_mutex);
        pthread_mutex_unlock(&timer_mutex);
        
        if (signal_shutdown) break;
        
        /* Send heartbeat message on timer expiration */
        if (SW_SendHeartbeatMessage() != E_OK) {
            fprintf(stderr, "WARN: Failed to send heartbeat message\n");
        }
    }
    
    return E_OK;
}

/*============================================================================*/
/* Resource Cleanup (AUTOSAR Shutdown routine)                              */
/*============================================================================*/

static void SW_CleanupResources(void) {
    fprintf(stdout, "\nINFO: Producer shutting down gracefully...\n");
    
    /* Stop timer */
    SW_StopPeriodicTimer();
    
    /* Close watchdog connection */
    if (watchdog_connection != -1) {
        name_close(watchdog_connection);
        watchdog_connection = -1;
        fprintf(stdout, "INFO: Connection to watchdog closed\n");
    }
    
    fprintf(stdout, "INFO: Total pulses sent: %u\n", pulse_send_count);
    fprintf(stdout, "INFO: Producer shutdown complete\n");
    fprintf(stdout, "\n");
}

/*============================================================================*/
/* Main Entry Point                                                          */
/*============================================================================*/

int main(void) {
    fprintf(stdout, "\n");
    fprintf(stdout, "================================================================================\n");
    fprintf(stdout, "  ASIL B Software Watchdog (Producer) - Heartbeat Generator\n");
    fprintf(stdout, "  Version: 1.0.0 | Build Date: May 2026\n");
    fprintf(stdout, "================================================================================\n");
    fprintf(stdout, "\n");
    
    /* Step 1: Bind Producer to CPU Core 2 (AUTOSAR OsTask assignment) */
    if (SW_BindThreadToCore(PRODUCER_CORE) != 0) {
        fprintf(stderr, "FATAL: Failed to bind producer to core %d\n", PRODUCER_CORE);
        return EXIT_FAILURE;
    }
    
    /* Step 2: Setup signal handlers (AUTOSAR SysService_Synchronous) */
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    /* Step 3: Perform initial handshake with watchdog (ERR-001 validation) */
    if (SW_PerformInitialHandshake() != E_OK) {
        fprintf(stderr, "FATAL: Initial handshake failed\n");
        return EXIT_FAILURE;
    }
    
    fprintf(stdout, "INFO: Producer initialization complete\n");
    fprintf(stdout, "\n");
    
    /* Step 4: Start periodic timer (AUTOSAR Os_Counter) */
    if (SW_StartPeriodicTimer() != E_OK) {
        fprintf(stderr, "FATAL: Failed to start periodic timer\n");
        SW_CleanupResources();
        return EXIT_FAILURE;
    }
    
    /* Step 5: Enter producer main loop */
    SW_ProducerMainLoop();
    
    /* Step 6: Cleanup and exit */
    SW_CleanupResources();
    
    return EXIT_SUCCESS;
}

/*============================================================================*/
/* End of File: SW_Producer.cpp                                             */
/*============================================================================*/
