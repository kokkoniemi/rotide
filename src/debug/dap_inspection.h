#ifndef ROTIDE_DEBUG_DAP_INSPECTION_H
#define ROTIDE_DEBUG_DAP_INSPECTION_H

#include "debug/dap.h"

int editorDapInspectionThreadCount(void);
const struct editorDapThread *editorDapInspectionThreadAt(int idx);
int editorDapInspectionStackFrameCount(void);
const struct editorDapStackFrame *editorDapInspectionStackFrameAt(int idx);
int editorDapInspectionScopeCount(void);
const struct editorDapScope *editorDapInspectionScopeAt(int idx);
int editorDapInspectionVariableCount(void);
const struct editorDapVariable *editorDapInspectionVariableAt(int idx);
const struct editorDapVariablePreviewChild *
editorDapInspectionVariablePreviewChildAt(const struct editorDapVariable *var, int idx);
int editorDapInspectionScopeVariableCount(int scope_idx);
int editorDapInspectionScopeVariableIndex(int scope_idx, int nth);
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
