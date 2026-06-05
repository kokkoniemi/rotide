#include "debug/dap_control.h"

#include "debug/dap.h"
#include "debug/dap_client.h"
#include "debug/dap_protocol.h"
#include "editing/edit.h"
#include "rotide.h"

int editorDapControlSendSimple(int to_adapter_fd, int *next_seq, const char *command) {
	if (!E.dap_running || to_adapter_fd == -1) {
		editorSetStatusMsg("No DAP session running");
		return 0;
	}
	if (!editorDapClientSendRequest(to_adapter_fd, editorDapBuildSimpleCommandRequestJson(
	                                                       (*next_seq)++, command))) {
		editorSetStatusMsg("DAP command failed");
		return 0;
	}
	return 1;
}

static int dapControlCurrentThreadId(int stopped_thread_id) {
	if (stopped_thread_id > 0) {
		return stopped_thread_id;
	}
	if (E.dap_thread_count > 0 && E.dap_threads[0].id > 0) {
		return E.dap_threads[0].id;
	}
	return 1;
}

int editorDapControlSendThread(int to_adapter_fd, int *next_seq, const char *command,
                               int stopped_thread_id) {
	if (!E.dap_running || to_adapter_fd == -1) {
		editorSetStatusMsg("No DAP session running");
		return 0;
	}
	if (!editorDapClientSendRequest(to_adapter_fd,
	                                editorDapBuildIntArgRequestJson(
	                                        (*next_seq)++, command, "threadId",
	                                        dapControlCurrentThreadId(stopped_thread_id)))) {
		editorSetStatusMsg("DAP command failed");
		return 0;
	}
	return 1;
}
