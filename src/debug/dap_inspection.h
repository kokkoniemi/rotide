#ifndef ROTIDE_DEBUG_DAP_INSPECTION_H
#define ROTIDE_DEBUG_DAP_INSPECTION_H

#include "debug/dap.h"

void editorDapInspectionClearState(void);
void editorDapInspectionApplyThreadsResponse(const char *message);
void editorDapInspectionApplyStackTraceResponse(const char *message);
void editorDapInspectionApplyScopesResponse(const char *message);
int editorDapInspectionScopeNameLooksLikeRegisters(const char *name);
int editorDapInspectionVariablePreviewChildCount(const struct editorDapVariable *var,
                                                 int max_children);
int editorDapInspectionVariableCanPreview(const struct editorDapVariable *var);
void editorDapInspectionApplyVariablesResponse(const char *message, int scope_index,
                                               int *first_variable_index_out);
void editorDapInspectionApplyVariablePreviewResponse(const char *message, int parent_index);

#endif
