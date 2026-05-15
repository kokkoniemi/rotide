#ifndef ROTIDE_DEBUG_DAP_CLIENT_H
#define ROTIDE_DEBUG_DAP_CLIENT_H

char *editorDapClientReadFrame(int from_adapter_fd);
int editorDapClientSendRequest(int to_adapter_fd, char *json);

#endif
