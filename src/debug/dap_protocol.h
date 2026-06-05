#ifndef ROTIDE_DEBUG_DAP_PROTOCOL_H
#define ROTIDE_DEBUG_DAP_PROTOCOL_H

#include "debug/dap.h"

char *editorDapBuildSetBreakpointsRequestJson(int seq, const char *path,
                                              const struct editorDapBreakpoint *breakpoints,
                                              int breakpoint_count);
char *editorDapBuildIntArgRequestJson(int seq, const char *command, const char *arg_key,
                                      int arg_value);
char *editorDapBuildVariablesRequestJson(int seq, int variables_reference, int start, int count);

int editorDapJsonStringField(const char *json, const char *field, char *buf, size_t bufsize);
int editorDapJsonResponseSucceeded(const char *json);
int editorDapJsonObjectIntField(const char *start, const char *end, const char *quoted_key,
                                int *out);
int editorDapJsonObjectStringField(const char *start, const char *end, const char *quoted_key,
                                   char *buf, size_t bufsize);
int editorDapJsonObjectChildObject(const char *start, const char *end, const char *quoted_key,
                                   const char **child_start, const char **child_end);
int editorDapJsonBodyChildObject(const char *message, const char *quoted_key,
                                 const char **child_start, const char **child_end);
int editorDapJsonBodyIntField(const char *message, const char *quoted_key, int *out);
int editorDapJsonTopLevelIntField(const char *message, const char *quoted_key, int *out);
int editorDapJsonBodyStringField(const char *message, const char *quoted_key, char *buf,
                                 size_t bufsize);

typedef int (*editorDapJsonArrayElementFn)(const char *obj_start, const char *obj_end);

void editorDapJsonForEachBodyArrayElement(const char *message, const char *quoted_key,
                                          editorDapJsonArrayElementFn on_element);

#endif
