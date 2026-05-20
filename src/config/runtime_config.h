#ifndef CONFIG_RUNTIME_CONFIG_H
#define CONFIG_RUNTIME_CONFIG_H

#include "config/common.h"

void editorConfigApplyConfiguredSettings(enum editorConfigBootstrapStatus bootstrap_status,
                                         const char *success_status);
void editorConfigReloadConfiguredSettings(void);

#endif
