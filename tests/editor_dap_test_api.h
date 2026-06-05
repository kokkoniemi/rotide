#ifndef ROTIDE_TESTS_EDITOR_DAP_TEST_API_H
#define ROTIDE_TESTS_EDITOR_DAP_TEST_API_H

/*
 * Test-only handshake introspection/setup. Values returned by
 * editorDapSessionStateForTest mirror the internal session states:
 *   0 = idle, 1 = awaiting initialize response,
 *   2 = awaiting initialized event, 3 = running.
 * editorDapBeginSessionForTest seeds an in-progress session that has "sent"
 * `initialize` (seq 1) and queued `launch_json` (ownership transferred) to be
 * flushed when the initialize response is processed; `to_adapter_fd` receives
 * any outgoing frames. editorDapEndSessionForTest tears the session down
 * (frees the queued launch); the caller still owns and must close its fds.
 */
int editorDapSessionStateForTest(void);
void editorDapBeginSessionForTest(int to_adapter_fd, char *launch_json);
void editorDapEndSessionForTest(void);

#endif
