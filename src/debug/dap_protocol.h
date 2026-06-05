#ifndef ROTIDE_DEBUG_DAP_PROTOCOL_H
#define ROTIDE_DEBUG_DAP_PROTOCOL_H

#include "debug/dap.h"

char *editorDapBuildSetBreakpointsRequestJson(int seq, const char *path,
                                              const struct editorDapBreakpoint *breakpoints,
                                              int breakpoint_count);
char *editorDapBuildIntArgRequestJson(int seq, const char *command, const char *arg_key,
                                      int arg_value);
char *editorDapBuildVariablesRequestJson(int seq, int variables_reference, int start, int count);

#endif
