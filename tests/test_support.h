#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include "rotide.h"

#include "config/editor_config.h"
#include "config/keymap.h"
#include "config/lsp_config.h"
#include "config/theme_config.h"
#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "editing/selection.h"
#include "editor_test_api.h"
#include "input/dispatch.h"
#include "language/lsp.h"
#include "language/syntax.h"
#include "render/screen.h"
#include "support/file_io.h"
#include "support/terminal.h"
#include "text/document.h"
#include "text/row.h"
#include "text/utf8.h"
#include "workspace/drawer.h"
#include "workspace/recovery.h"
#include "workspace/task.h"
#include "workspace/tabs.h"
#include "alloc_test_hooks.h"
#include "save_syscalls_test_hooks.h"
#include "test_helpers.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern struct editorConfig E;

struct envVarBackup {
	const char *name;
	char *value;
	int was_set;
};


struct recoveryTestEnv {
	struct envVarBackup home_backup;
	char *original_cwd;
	char root_dir[512];
	char home_dir[512];
	char project_dir[512];
};


int count_tmp_save_artifacts(const char *target_path);
int remove_tmp_save_artifacts(const char *target_path);
int write_fixture_to_temp_path(char path_buf[], int suffix_len,
		const char *fixture_relative_path);
int write_temp_file_with_suffix(char path_buf[], size_t path_buf_size, const char *prefix,
		const char *suffix, const char *content);
int copy_fixture_to_temp_file_with_suffix(char path_buf[], size_t path_buf_size,
		const char *prefix, const char *suffix, const char *fixture_relative_path);
int copy_fixture_to_path(const char *dest_path, const char *fixture_relative_path);
int write_temp_go_file(char path_buf[], size_t path_buf_size, const char *content);
int write_temp_c_file(char path_buf[], size_t path_buf_size, const char *content);
int write_temp_html_file(char path_buf[], size_t path_buf_size, const char *content);
int write_temp_text_file(char path_buf[], size_t path_buf_size, const char *content);
char *dup_active_source_text(size_t *len_out);
int assert_active_source_matches_rows(void);
int set_active_search_match(int row, int col, int len);
int assert_active_search_match(int row, int col, int len);
int set_selection_anchor(int row, int col);
int assert_selection_anchor(int row, int col);
int editor_process_keypress_with_input_silent(const char *input, size_t len);
int wait_for_task_completion_with_timeout(int timeout_ms);
int editor_process_single_key(int key);
int format_sgr_mouse_event(char *buf, size_t bufsz, int cb, int x, int y, char suffix);
int wait_for_child_exit_with_timeout(pid_t pid, int timeout_ms, int *status_out);
int backup_env_var(struct envVarBackup *backup, const char *name);
int restore_env_var(struct envVarBackup *backup);
int write_text_file(const char *path, const char *text);
char *build_repeated_text(const char *line, int repeats, size_t *len_out);
int path_join(char *buf, size_t bufsz, const char *dir, const char *name);
int make_dir(const char *path);
int find_drawer_entry(const char *name, int *idx_out, struct editorDrawerEntryView *view_out);
void remove_files_in_dir(const char *dir_path);
void cleanup_recovery_test_env(struct recoveryTestEnv *env);
int setup_recovery_test_env(struct recoveryTestEnv *env);

#endif
