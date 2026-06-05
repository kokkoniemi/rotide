#include "debug/dap_inspection.h"

#include "debug/dap.h"
#include "debug/dap_protocol.h"
#include "rotide.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void editorDapInspectionClearState(void) {
	E.dap_thread_count = 0;
	E.dap_stack_frame_count = 0;
	E.dap_scope_count = 0;
	E.dap_variable_count = 0;
}

static int dapInspectionCollectThread(const char *obj_start, const char *obj_end) {
	if (E.dap_thread_count >= ROTIDE_DAP_MAX_THREADS) {
		return 0;
	}
	int id = 0;
	if (!editorDapJsonObjectIntField(obj_start, obj_end, "\"id\"", &id)) {
		return 1;
	}
	struct editorDapThread *thread = &E.dap_threads[E.dap_thread_count];
	memset(thread, 0, sizeof(*thread));
	thread->id = id;
	(void)editorDapJsonObjectStringField(obj_start, obj_end, "\"name\"", thread->name,
	                                     sizeof(thread->name));
	E.dap_thread_count++;
	return 1;
}

void editorDapInspectionApplyThreadsResponse(const char *message) {
	E.dap_thread_count = 0;
	editorDapJsonForEachBodyArrayElement(message, "\"threads\"", dapInspectionCollectThread);
}

static int dapInspectionCollectStackFrame(const char *obj_start, const char *obj_end) {
	if (E.dap_stack_frame_count >= ROTIDE_DAP_MAX_STACK_FRAMES) {
		return 0;
	}
	int id = 0;
	if (!editorDapJsonObjectIntField(obj_start, obj_end, "\"id\"", &id)) {
		return 1;
	}
	struct editorDapStackFrame *frame = &E.dap_stack_frames[E.dap_stack_frame_count];
	memset(frame, 0, sizeof(*frame));
	frame->id = id;
	(void)editorDapJsonObjectStringField(obj_start, obj_end, "\"name\"", frame->name,
	                                     sizeof(frame->name));
	(void)editorDapJsonObjectIntField(obj_start, obj_end, "\"line\"", &frame->line);
	(void)editorDapJsonObjectIntField(obj_start, obj_end, "\"column\"", &frame->column);
	const char *source_start = NULL;
	const char *source_end = NULL;
	if (editorDapJsonObjectChildObject(obj_start, obj_end, "\"source\"", &source_start,
	                                   &source_end)) {
		(void)editorDapJsonObjectStringField(source_start, source_end, "\"path\"",
		                                     frame->path, sizeof(frame->path));
	}
	E.dap_stack_frame_count++;
	return 1;
}

void editorDapInspectionApplyStackTraceResponse(const char *message) {
	E.dap_stack_frame_count = 0;
	editorDapJsonForEachBodyArrayElement(message, "\"stackFrames\"",
	                                     dapInspectionCollectStackFrame);
}

static int dapInspectionCollectScope(const char *obj_start, const char *obj_end) {
	if (E.dap_scope_count >= ROTIDE_DAP_MAX_SCOPES) {
		return 0;
	}
	struct editorDapScope *scope = &E.dap_scopes[E.dap_scope_count];
	memset(scope, 0, sizeof(*scope));
	(void)editorDapJsonObjectStringField(obj_start, obj_end, "\"name\"", scope->name,
	                                     sizeof(scope->name));
	(void)editorDapJsonObjectIntField(obj_start, obj_end, "\"variablesReference\"",
	                                  &scope->variables_reference);
	E.dap_scope_count++;
	return 1;
}

void editorDapInspectionApplyScopesResponse(const char *message) {
	E.dap_scope_count = 0;
	E.dap_variable_count = 0;
	editorDapJsonForEachBodyArrayElement(message, "\"scopes\"", dapInspectionCollectScope);
}

int editorDapInspectionScopeNameLooksLikeRegisters(const char *name) {
	static const char needle[] = "register";
	size_t needle_len = sizeof(needle) - 1;
	for (const char *p = name; *p != '\0'; p++) {
		size_t i = 0;
		while (i < needle_len && p[i] != '\0' &&
		       (char)tolower((unsigned char)p[i]) == needle[i]) {
			i++;
		}
		if (i == needle_len) {
			return 1;
		}
	}
	return 0;
}

int editorDapInspectionVariablePreviewChildCount(const struct editorDapVariable *var,
                                                 int max_children) {
	int child_count =
	        var->indexed_variables > 0 ? var->indexed_variables : var->named_variables;
	if (child_count <= 0) {
		return 0;
	}
	return child_count < max_children ? child_count : max_children;
}

int editorDapInspectionVariableCanPreview(const struct editorDapVariable *var) {
	if (var == NULL || var->variables_reference <= 0 || var->scope_index < 0 ||
	    var->scope_index >= E.dap_scope_count) {
		return 0;
	}
	return !editorDapInspectionScopeNameLooksLikeRegisters(E.dap_scopes[var->scope_index].name);
}

static int g_dap_inspection_collect_scope_index = 0;

static int dapInspectionCollectVariable(const char *obj_start, const char *obj_end) {
	if (E.dap_variable_count >= ROTIDE_DAP_MAX_VARIABLES) {
		return 0;
	}
	struct editorDapVariable *var = &E.dap_variables[E.dap_variable_count];
	memset(var, 0, sizeof(*var));
	if (!editorDapJsonObjectStringField(obj_start, obj_end, "\"name\"", var->name,
	                                    sizeof(var->name))) {
		return 1;
	}
	(void)editorDapJsonObjectStringField(obj_start, obj_end, "\"value\"", var->value,
	                                     sizeof(var->value));
	(void)editorDapJsonObjectStringField(obj_start, obj_end, "\"type\"", var->type,
	                                     sizeof(var->type));
	(void)editorDapJsonObjectIntField(obj_start, obj_end, "\"variablesReference\"",
	                                  &var->variables_reference);
	(void)editorDapJsonObjectIntField(obj_start, obj_end, "\"namedVariables\"",
	                                  &var->named_variables);
	(void)editorDapJsonObjectIntField(obj_start, obj_end, "\"indexedVariables\"",
	                                  &var->indexed_variables);
	(void)editorDapJsonObjectStringField(obj_start, obj_end, "\"memoryReference\"",
	                                     var->memory_reference, sizeof(var->memory_reference));
	var->scope_index = g_dap_inspection_collect_scope_index;
	E.dap_variable_count++;
	return 1;
}

void editorDapInspectionApplyVariablesResponse(const char *message, int scope_index,
                                               int *first_variable_index_out) {
	if (first_variable_index_out != NULL) {
		*first_variable_index_out = E.dap_variable_count;
	}
	g_dap_inspection_collect_scope_index = scope_index;
	editorDapJsonForEachBodyArrayElement(message, "\"variables\"",
	                                     dapInspectionCollectVariable);
}

static int g_dap_inspection_preview_parent_index = -1;
static int g_dap_inspection_preview_child_count = 0;

static int dapInspectionPreviewAppendText(struct editorDapVariable *var, const char *text) {
	if (var == NULL || text == NULL || text[0] == '\0') {
		return 1;
	}
	size_t len = strlen(var->preview);
	size_t avail = sizeof(var->preview) - len;
	if (avail <= 1) {
		return 0;
	}
	int written = snprintf(var->preview + len, avail, "%s", text);
	return written >= 0 && (size_t)written < avail;
}

static int dapInspectionCollectVariablePreviewChild(const char *obj_start, const char *obj_end) {
	if (g_dap_inspection_preview_parent_index < 0 ||
	    g_dap_inspection_preview_parent_index >= E.dap_variable_count) {
		return 0;
	}
	struct editorDapVariable *parent = &E.dap_variables[g_dap_inspection_preview_parent_index];
	char name[ROTIDE_DAP_NAME_MAX] = "";
	char value[ROTIDE_DAP_VALUE_MAX] = "";
	(void)editorDapJsonObjectStringField(obj_start, obj_end, "\"name\"", name, sizeof(name));
	(void)editorDapJsonObjectStringField(obj_start, obj_end, "\"value\"", value, sizeof(value));
	if (value[0] == '\0' && name[0] == '\0') {
		return 1;
	}
	int array_like = parent->indexed_variables > 0 && parent->named_variables == 0;
	if (g_dap_inspection_preview_child_count > 0 &&
	    !dapInspectionPreviewAppendText(parent, ",")) {
		return 0;
	}
	if (!array_like && name[0] != '\0') {
		if (!dapInspectionPreviewAppendText(parent, name) ||
		    !dapInspectionPreviewAppendText(parent, "=")) {
			return 0;
		}
	}
	if (!dapInspectionPreviewAppendText(parent, value[0] != '\0' ? value : name)) {
		return 0;
	}
	g_dap_inspection_preview_child_count++;
	return 1;
}

void editorDapInspectionApplyVariablePreviewResponse(const char *message, int parent_index) {
	if (parent_index < 0 || parent_index >= E.dap_variable_count) {
		return;
	}
	struct editorDapVariable *parent = &E.dap_variables[parent_index];
	parent->preview[0] = '\0';
	if (!dapInspectionPreviewAppendText(parent, "{")) {
		return;
	}
	g_dap_inspection_preview_parent_index = parent_index;
	g_dap_inspection_preview_child_count = 0;
	editorDapJsonForEachBodyArrayElement(message, "\"variables\"",
	                                     dapInspectionCollectVariablePreviewChild);
	g_dap_inspection_preview_parent_index = -1;
	if (g_dap_inspection_preview_child_count == 0) {
		parent->preview[0] = '\0';
		return;
	}
	int total_children =
	        parent->indexed_variables > 0 ? parent->indexed_variables : parent->named_variables;
	if (total_children > g_dap_inspection_preview_child_count) {
		(void)dapInspectionPreviewAppendText(parent, ",...");
	}
	(void)dapInspectionPreviewAppendText(parent, "}");
}
