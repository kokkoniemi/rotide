#ifndef ROTIDE_SUPPORT_PERF_TRACE_H
#define ROTIDE_SUPPORT_PERF_TRACE_H

/* Per-frame timing trace. Disabled by default; enabled when the environment
 * variable ROTIDE_PERF=1 is set at startup. When disabled, all functions
 * here are no-ops with no formatting / IO overhead.
 *
 * Output is a single CSV line per frame written to stderr:
 *   monotonic_ms,pump_us,refresh_us,bytes_pumped,fds_ready
 *
 * Counters are reset at editorPerfBeginFrame and emitted at editorPerfEndFrame.
 */

void editorPerfInit(void);
int editorPerfEnabled(void);

void editorPerfBeginFrame(void);
void editorPerfRecordPumpBytes(int bytes);
void editorPerfRecordPumpUs(long us);
void editorPerfRecordRefreshUs(long us);
void editorPerfRecordFdsReady(int count);
void editorPerfEndFrame(void);

long editorPerfMonotonicUs(void);

#endif
