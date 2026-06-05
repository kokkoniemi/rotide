#ifndef ROTIDE_DEBUG_DAP_CONTROL_H
#define ROTIDE_DEBUG_DAP_CONTROL_H

int editorDapControlSendSimple(int to_adapter_fd, int *next_seq, const char *command);
int editorDapControlSendThread(int to_adapter_fd, int *next_seq, const char *command,
                               int stopped_thread_id);

#endif
