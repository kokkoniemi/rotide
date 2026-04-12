#include "workspace/recovery.h"

#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "render/screen.h"
#include "support/size_utils.h"
#include "support/alloc.h"
#include "support/file_io.h"
#include "support/terminal.h"
#include "text/document.h"
#include "text/row.h"
#include "workspace/tabs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ROTIDE_RECOVERY_MAGIC "RTRECOV1"
#define ROTIDE_RECOVERY_MAGIC_LEN 8
#define ROTIDE_RECOVERY_VERSION 2U
#define ROTIDE_RECOVERY_AUTOSAVE_DEBOUNCE_SECONDS 5
#define ROTIDE_RECOVERY_MAX_FILENAME_BYTES 4096

struct editorRecoveryTab {
	int cx;
	int cy;
	int rowoff;
	int coloff;
	char *filename;
	char *text;
	size_t textlen;
};

struct editorRecoverySession {
	int tab_count;
	int active_tab;
	struct editorRecoveryTab *tabs;
};

struct editorRecoveryTabView {
	int cx;
	int cy;
	int rowoff;
	int coloff;
	enum editorTabKind tab_kind;
	struct editorDocument *document;
	const char *filename;
};

enum editorRecoveryLoadStatus {
	EDITOR_RECOVERY_LOAD_OK = 0,
	EDITOR_RECOVERY_LOAD_NOT_FOUND,
	EDITOR_RECOVERY_LOAD_INVALID,
	EDITOR_RECOVERY_LOAD_OOM,
	EDITOR_RECOVERY_LOAD_IO
};

enum editorReadExactStatus {
	EDITOR_READ_EXACT_OK = 1,
	EDITOR_READ_EXACT_EOF = 0,
	EDITOR_READ_EXACT_ERR = -1
};

static int editorRecoveryWriteAll(int fd, const char *buf, size_t len) {
	size_t total = 0;
	while (total < len) {
		ssize_t written = write(fd, buf + total, len - total);
		if (written == -1) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		if (written == 0) {
			errno = EIO;
			return -1;
		}
		total += (size_t)written;
	}
	return 0;
}

static void editorRecoverySessionFree(struct editorRecoverySession *session) {
	if (session == NULL || session->tabs == NULL) {
		return;
	}

	for (int tab_idx = 0; tab_idx < session->tab_count; tab_idx++) {
		struct editorRecoveryTab *tab = &session->tabs[tab_idx];
		free(tab->text);
		tab->text = NULL;
		tab->textlen = 0;
		free(tab->filename);
		tab->filename = NULL;
	}

	free(session->tabs);
	session->tabs = NULL;
	session->tab_count = 0;
	session->active_tab = 0;
}

static uint64_t editorRecoveryHashPath(const char *path) {
	uint64_t hash = UINT64_C(1469598103934665603);
	const unsigned char *p = (const unsigned char *)path;
	while (*p != '\0') {
		hash ^= (uint64_t)*p;
		hash *= UINT64_C(1099511628211);
		p++;
	}
	return hash;
}

static int editorEnsureDirectoryExists(const char *path, mode_t mode) {
	if (mkdir(path, mode) == 0) {
		return 1;
	}
	if (errno != EEXIST) {
		return 0;
	}

	struct stat st;
	if (stat(path, &st) == -1) {
		return 0;
	}
	return S_ISDIR(st.st_mode);
}

static char *editorRecoveryBuildPathForBase(const char *base, uint64_t hash) {
	char name[128];
	int written = snprintf(name, sizeof(name), "rotide-recovery-u%lu-%016llx.swap",
			(unsigned long)getuid(), (unsigned long long)hash);
	if (written <= 0 || (size_t)written >= sizeof(name)) {
		return NULL;
	}
	return editorPathJoin(base, name);
}

static char *editorResolveRecoveryPath(void) {
	char *cwd = editorPathGetCwd();
	if (cwd == NULL) {
		return NULL;
	}

	uint64_t cwd_hash = editorRecoveryHashPath(cwd);
	free(cwd);

	char *recovery_path = NULL;
	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0') {
		char *dot_rotide = editorPathJoin(home, ".rotide");
		char *recovery_dir = NULL;
		if (dot_rotide != NULL) {
			recovery_dir = editorPathJoin(dot_rotide, "recovery");
		}

		if (dot_rotide != NULL && recovery_dir != NULL &&
				editorEnsureDirectoryExists(dot_rotide, 0700) &&
				editorEnsureDirectoryExists(recovery_dir, 0700)) {
			recovery_path = editorRecoveryBuildPathForBase(recovery_dir, cwd_hash);
		}

		free(dot_rotide);
		free(recovery_dir);
	}

	if (recovery_path == NULL) {
		recovery_path = editorRecoveryBuildPathForBase("/tmp", cwd_hash);
	}

	return recovery_path;
}

int editorRecoveryInitForCurrentDir(void) {
	free(E.recovery_path);
	E.recovery_path = editorResolveRecoveryPath();
	E.recovery_last_autosave_time = 0;
	return E.recovery_path != NULL;
}

void editorRecoveryShutdown(void) {
	free(E.recovery_path);
	E.recovery_path = NULL;
	E.recovery_last_autosave_time = 0;
}

const char *editorRecoveryPath(void) {
	return E.recovery_path;
}

int editorRecoveryHasSnapshot(void) {
	if (E.recovery_path == NULL) {
		return 0;
	}
	struct stat st;
	if (stat(E.recovery_path, &st) == -1) {
		return 0;
	}
	return S_ISREG(st.st_mode);
}

void editorRecoveryCleanupOnCleanExit(void) {
	if (E.recovery_path == NULL) {
		return;
	}
	(void)unlink(E.recovery_path);
	E.recovery_last_autosave_time = 0;
}

static int editorRecoveryReadExact(int fd, void *buf, size_t len) {
	char *dst = (char *)buf;
	size_t total = 0;
	while (total < len) {
		ssize_t nread = read(fd, dst + total, len - total);
		if (nread == 0) {
			return EDITOR_READ_EXACT_EOF;
		}
		if (nread == -1) {
			if (errno == EINTR) {
				continue;
			}
			return EDITOR_READ_EXACT_ERR;
		}
		total += (size_t)nread;
	}
	return EDITOR_READ_EXACT_OK;
}

static int editorRecoveryWriteU32(int fd, uint32_t value) {
	unsigned char bytes[4];
	bytes[0] = (unsigned char)(value & 0xFFU);
	bytes[1] = (unsigned char)((value >> 8) & 0xFFU);
	bytes[2] = (unsigned char)((value >> 16) & 0xFFU);
	bytes[3] = (unsigned char)((value >> 24) & 0xFFU);
	return editorRecoveryWriteAll(fd, (const char *)bytes, sizeof(bytes)) == 0;
}

static int editorRecoveryWriteI32(int fd, int32_t value) {
	return editorRecoveryWriteU32(fd, (uint32_t)value);
}

static int editorRecoveryReadU32(int fd, uint32_t *value_out) {
	unsigned char bytes[4];
	int read_status = editorRecoveryReadExact(fd, bytes, sizeof(bytes));
	if (read_status != EDITOR_READ_EXACT_OK) {
		return read_status;
	}
	*value_out = (uint32_t)bytes[0] |
			((uint32_t)bytes[1] << 8) |
			((uint32_t)bytes[2] << 16) |
			((uint32_t)bytes[3] << 24);
	return EDITOR_READ_EXACT_OK;
}

static int editorRecoveryReadI32(int fd, int32_t *value_out) {
	uint32_t raw = 0;
	int read_status = editorRecoveryReadU32(fd, &raw);
	if (read_status != EDITOR_READ_EXACT_OK) {
		return read_status;
	}
	*value_out = (int32_t)raw;
	return EDITOR_READ_EXACT_OK;
}

static int editorRecoveryGetTabView(int idx, struct editorRecoveryTabView *view_out) {
	if (view_out == NULL) {
		errno = EINVAL;
		return 0;
	}

	int tab_count = E.tab_count > 0 ? E.tab_count : 1;
	int active_tab = E.active_tab;
	if (tab_count == 1) {
		active_tab = 0;
	}
	if (idx < 0 || idx >= tab_count) {
		errno = EINVAL;
		return 0;
	}

	if (idx == active_tab) {
		if (editorTabKindSupportsDocument(E.tab_kind) &&
				(!editorDocumentEnsureActiveCurrent() || E.document == NULL)) {
			errno = EIO;
			return 0;
		}
		view_out->cx = E.cx;
		view_out->cy = E.cy;
		view_out->rowoff = E.rowoff;
		view_out->coloff = E.coloff;
		view_out->tab_kind = E.tab_kind;
		view_out->document = E.document;
		view_out->filename = E.filename;
		return 1;
	}

	struct editorTabState *tab = &E.tabs[idx];
	if (editorTabKindSupportsDocument(tab->tab_kind) &&
			(!editorTabDocumentEnsureCurrent(tab) || tab->document == NULL)) {
		errno = EIO;
		return 0;
	}
	view_out->cx = tab->cx;
	view_out->cy = tab->cy;
	view_out->rowoff = tab->rowoff;
	view_out->coloff = tab->coloff;
	view_out->tab_kind = tab->tab_kind;
	view_out->document = tab->document;
	view_out->filename = tab->filename;
	return 1;
}

static char *editorRecoveryDupTabText(const struct editorRecoveryTabView *view, size_t *len_out) {
	if (len_out != NULL) {
		*len_out = 0;
	}
	if (view == NULL) {
		errno = EINVAL;
		return NULL;
	}
	if (view->document == NULL) {
		errno = EIO;
		return NULL;
	}

	size_t text_len = editorDocumentLength(view->document);
	if (text_len > ROTIDE_MAX_TEXT_BYTES) {
		errno = EOVERFLOW;
		return NULL;
	}
	errno = 0;
	return editorDocumentDupRange(view->document, 0, text_len, len_out);
}

static int editorRecoveryWriteSessionToFd(int fd) {
	int tab_count = E.tab_count > 0 ? E.tab_count : 1;
	int active_tab = E.active_tab;
	if (tab_count == 1) {
		active_tab = 0;
	}
	if (tab_count < 1 || tab_count > ROTIDE_MAX_TABS ||
			active_tab < 0 || active_tab >= tab_count) {
		errno = EINVAL;
		return 0;
	}

	if (editorRecoveryWriteAll(fd, ROTIDE_RECOVERY_MAGIC, ROTIDE_RECOVERY_MAGIC_LEN) == -1 ||
			!editorRecoveryWriteU32(fd, ROTIDE_RECOVERY_VERSION) ||
			!editorRecoveryWriteU32(fd, (uint32_t)tab_count) ||
			!editorRecoveryWriteU32(fd, (uint32_t)active_tab)) {
		if (errno == 0) {
			errno = EIO;
		}
		return 0;
	}

	for (int tab_idx = 0; tab_idx < tab_count; tab_idx++) {
		struct editorRecoveryTabView view;
		if (!editorRecoveryGetTabView(tab_idx, &view)) {
			return 0;
		}

		size_t filename_len = 0;
		if (view.filename != NULL) {
			filename_len = strlen(view.filename);
			if (filename_len > ROTIDE_RECOVERY_MAX_FILENAME_BYTES) {
				errno = EOVERFLOW;
				return 0;
			}
		}

		if (!editorRecoveryWriteI32(fd, (int32_t)view.cx) ||
				!editorRecoveryWriteI32(fd, (int32_t)view.cy) ||
				!editorRecoveryWriteI32(fd, (int32_t)view.rowoff) ||
				!editorRecoveryWriteI32(fd, (int32_t)view.coloff) ||
				!editorRecoveryWriteU32(fd, (uint32_t)filename_len)) {
			if (errno == 0) {
				errno = EIO;
			}
			return 0;
		}
		if (filename_len > 0 && editorRecoveryWriteAll(fd, view.filename, filename_len) == -1) {
			return 0;
		}

		size_t text_len = 0;
		char *text = editorRecoveryDupTabText(&view, &text_len);
		if (text == NULL && view.document != NULL && editorDocumentLength(view.document) > 0) {
			return 0;
		}
		if (!editorRecoveryWriteU32(fd, (uint32_t)text_len)) {
			free(text);
			if (errno == 0) {
				errno = EIO;
			}
			return 0;
		}
		if (text_len > 0 && editorRecoveryWriteAll(fd, text, text_len) == -1) {
			free(text);
			return 0;
		}
		free(text);
	}

	return 1;
}

static int editorRecoveryWriteSnapshotAtomic(void) {
	if (E.recovery_path == NULL) {
		errno = EINVAL;
		return 0;
	}

	char *tmp_path = editorTempPathForTarget(E.recovery_path);
	if (tmp_path == NULL) {
		errno = ENOMEM;
		return 0;
	}

	int fd = -1;
	int tmp_created = 0;
	fd = mkstemp(tmp_path);
	if (fd == -1) {
		goto err;
	}
	tmp_created = 1;
	if (fchmod(fd, 0600) == -1) {
		goto err;
	}
	if (!editorRecoveryWriteSessionToFd(fd)) {
		if (errno == 0) {
			errno = EIO;
		}
		goto err;
	}
	if (fsync(fd) == -1) {
		goto err;
	}
	if (close(fd) == -1) {
		fd = -1;
		goto err;
	}
	fd = -1;

	if (rename(tmp_path, E.recovery_path) == -1) {
		goto err;
	}

	free(tmp_path);
	return 1;

err: {
	int saved_errno = errno;
	if (fd != -1) {
		(void)close(fd);
	}
	if (tmp_created) {
		(void)unlink(tmp_path);
	}
	free(tmp_path);
	errno = saved_errno;
	return 0;
}
}

static int editorRecoveryTabReadText(int fd, struct editorRecoveryTab *tab) {
	uint32_t text_len_u32 = 0;
	int read_status = editorRecoveryReadU32(fd, &text_len_u32);
	if (read_status != EDITOR_READ_EXACT_OK) {
		return read_status;
	}

	size_t text_len = (size_t)text_len_u32;
	size_t alloc = 0;
	if (!editorSizeAdd(text_len, 1, &alloc)) {
		return EDITOR_READ_EXACT_EOF;
	}
	tab->text = editorMalloc(alloc);
	if (tab->text == NULL) {
		errno = ENOMEM;
		return EDITOR_READ_EXACT_ERR;
	}
	if (text_len > 0) {
		read_status = editorRecoveryReadExact(fd, tab->text, text_len);
		if (read_status != EDITOR_READ_EXACT_OK) {
			return read_status;
		}
	}
	tab->text[text_len] = '\0';
	tab->textlen = text_len;
	return EDITOR_READ_EXACT_OK;
}

static enum editorRecoveryLoadStatus editorRecoveryLoadSessionFromPath(const char *path,
		struct editorRecoverySession *session_out) {
	memset(session_out, 0, sizeof(*session_out));

	int fd = open(path, O_RDONLY);
	if (fd == -1) {
		if (errno == ENOENT) {
			return EDITOR_RECOVERY_LOAD_NOT_FOUND;
		}
		return EDITOR_RECOVERY_LOAD_IO;
	}

	enum editorRecoveryLoadStatus status = EDITOR_RECOVERY_LOAD_INVALID;
	char magic[ROTIDE_RECOVERY_MAGIC_LEN];
	int read_status = editorRecoveryReadExact(fd, magic, sizeof(magic));
	if (read_status != EDITOR_READ_EXACT_OK) {
		status = read_status == EDITOR_READ_EXACT_ERR ? EDITOR_RECOVERY_LOAD_IO :
				EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}
	if (memcmp(magic, ROTIDE_RECOVERY_MAGIC, ROTIDE_RECOVERY_MAGIC_LEN) != 0) {
		status = EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}

	uint32_t version = 0;
	uint32_t tab_count_u32 = 0;
	uint32_t active_tab_u32 = 0;
	if (editorRecoveryReadU32(fd, &version) != EDITOR_READ_EXACT_OK ||
			editorRecoveryReadU32(fd, &tab_count_u32) != EDITOR_READ_EXACT_OK ||
			editorRecoveryReadU32(fd, &active_tab_u32) != EDITOR_READ_EXACT_OK) {
		status = EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}
	if (version != ROTIDE_RECOVERY_VERSION ||
			tab_count_u32 < 1 ||
			tab_count_u32 > ROTIDE_MAX_TABS ||
			active_tab_u32 >= tab_count_u32) {
		status = EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}

	session_out->tab_count = (int)tab_count_u32;
	session_out->active_tab = (int)active_tab_u32;
	size_t tabs_bytes = 0;
	if (!editorSizeMul(sizeof(struct editorRecoveryTab), (size_t)session_out->tab_count,
				&tabs_bytes)) {
		status = EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}
	session_out->tabs = editorMalloc(tabs_bytes);
	if (session_out->tabs == NULL) {
		status = EDITOR_RECOVERY_LOAD_OOM;
		goto out;
	}
	memset(session_out->tabs, 0, tabs_bytes);

	for (int tab_idx = 0; tab_idx < session_out->tab_count; tab_idx++) {
		struct editorRecoveryTab *tab = &session_out->tabs[tab_idx];
		int32_t cx = 0;
		int32_t cy = 0;
		int32_t rowoff = 0;
		int32_t coloff = 0;
		if (editorRecoveryReadI32(fd, &cx) != EDITOR_READ_EXACT_OK ||
				editorRecoveryReadI32(fd, &cy) != EDITOR_READ_EXACT_OK ||
				editorRecoveryReadI32(fd, &rowoff) != EDITOR_READ_EXACT_OK ||
				editorRecoveryReadI32(fd, &coloff) != EDITOR_READ_EXACT_OK) {
			status = EDITOR_RECOVERY_LOAD_INVALID;
			goto out;
		}
		tab->cx = (int)cx;
		tab->cy = (int)cy;
		tab->rowoff = (int)rowoff;
		tab->coloff = (int)coloff;

		uint32_t filename_len_u32 = 0;
		if (editorRecoveryReadU32(fd, &filename_len_u32) != EDITOR_READ_EXACT_OK) {
			status = EDITOR_RECOVERY_LOAD_INVALID;
			goto out;
		}
		if (filename_len_u32 > ROTIDE_RECOVERY_MAX_FILENAME_BYTES) {
			status = EDITOR_RECOVERY_LOAD_INVALID;
			goto out;
		}
		if (filename_len_u32 > 0) {
			size_t filename_len = (size_t)filename_len_u32;
			size_t filename_alloc = 0;
			if (!editorSizeAdd(filename_len, 1, &filename_alloc)) {
				status = EDITOR_RECOVERY_LOAD_INVALID;
				goto out;
			}
			tab->filename = editorMalloc(filename_alloc);
			if (tab->filename == NULL) {
				status = EDITOR_RECOVERY_LOAD_OOM;
				goto out;
			}
			read_status = editorRecoveryReadExact(fd, tab->filename, filename_len);
			if (read_status != EDITOR_READ_EXACT_OK) {
				status = read_status == EDITOR_READ_EXACT_ERR ? EDITOR_RECOVERY_LOAD_IO :
						EDITOR_RECOVERY_LOAD_INVALID;
				goto out;
			}
			tab->filename[filename_len] = '\0';
		}

		read_status = editorRecoveryTabReadText(fd, tab);
		if (read_status != EDITOR_READ_EXACT_OK) {
			if (read_status == EDITOR_READ_EXACT_ERR) {
				status = errno == ENOMEM ? EDITOR_RECOVERY_LOAD_OOM :
						EDITOR_RECOVERY_LOAD_IO;
			} else {
				status = EDITOR_RECOVERY_LOAD_INVALID;
			}
			goto out;
		}
	}

	char trailing = '\0';
	ssize_t trailing_read;
	do {
		trailing_read = read(fd, &trailing, 1);
	} while (trailing_read == -1 && errno == EINTR);
	if (trailing_read == 1) {
		status = EDITOR_RECOVERY_LOAD_INVALID;
		goto out;
	}
	if (trailing_read == -1) {
		status = EDITOR_RECOVERY_LOAD_IO;
		goto out;
	}

	status = EDITOR_RECOVERY_LOAD_OK;

out:
	if (close(fd) == -1 && status == EDITOR_RECOVERY_LOAD_OK) {
		status = EDITOR_RECOVERY_LOAD_IO;
	}
	if (status != EDITOR_RECOVERY_LOAD_OK) {
		editorRecoverySessionFree(session_out);
	}
	return status;
}

static void editorRecoveryClampActiveCursorAndScroll(const struct editorRecoveryTab *tab) {
	if (tab->cy < 0) {
		E.cy = 0;
	} else {
		E.cy = tab->cy;
	}
	if (E.numrows == 0) {
		E.cy = 0;
		E.cx = 0;
		E.rowoff = 0;
		E.coloff = tab->coloff < 0 ? 0 : tab->coloff;
		return;
	}
	if (E.cy >= E.numrows) {
		E.cy = E.numrows - 1;
	}

	struct erow *row = &E.rows[E.cy];
	int target_cx = tab->cx;
	if (target_cx < 0) {
		target_cx = 0;
	}
	if (target_cx > row->size) {
		target_cx = row->size;
	}
	E.cx = editorRowClampCxToClusterBoundary(row, target_cx);

	if (tab->rowoff < 0) {
		E.rowoff = 0;
	} else {
		E.rowoff = tab->rowoff;
	}
	if (E.rowoff >= E.numrows) {
		E.rowoff = E.numrows - 1;
	}
	E.coloff = tab->coloff < 0 ? 0 : tab->coloff;
}

static int editorRecoveryPopulateActiveFromTab(const struct editorRecoveryTab *tab) {
	struct editorDocument document;
	int document_inited = 0;
	int ok = 0;
	free(E.filename);
	E.filename = NULL;
	if (tab->filename != NULL) {
		E.filename = strdup(tab->filename);
		if (E.filename == NULL) {
			editorSetAllocFailureStatus();
			return 0;
		}
	}

	editorDocumentInit(&document);
	document_inited = 1;
	if (!editorDocumentResetFromString(&document,
				tab->text != NULL ? tab->text : "", tab->textlen)) {
		editorSetAllocFailureStatus();
		goto cleanup;
	}

	if (!editorRestoreActiveFromDocument(&document, tab->cy, tab->cx, 1, 1)) {
		goto cleanup;
	}

	editorRecoveryClampActiveCursorAndScroll(tab);
	if (!editorBufferPosToOffset(E.cy, E.cx, &E.cursor_offset)) {
		E.cursor_offset = 0;
	}
	ok = 1;

cleanup:
	if (document_inited) {
		editorDocumentFree(&document);
	}
	return ok;
}

static int editorRecoveryApplySession(const struct editorRecoverySession *session) {
	if (session == NULL || session->tab_count < 1) {
		return 0;
	}

	if (!editorTabsInit()) {
		return 0;
	}

	for (int tab_idx = 0; tab_idx < session->tab_count; tab_idx++) {
		if (tab_idx > 0 && !editorTabNewEmpty()) {
			(void)editorTabsInit();
			return 0;
		}

		if (!editorRecoveryPopulateActiveFromTab(&session->tabs[tab_idx])) {
			(void)editorTabsInit();
			return 0;
		}
	}

	if (!editorTabSwitchToIndex(session->active_tab)) {
		(void)editorTabsInit();
		return 0;
	}

	return 1;
}

int editorRecoveryRestoreSnapshot(void) {
	if (E.recovery_path == NULL) {
		return 0;
	}

	struct editorRecoverySession session;
	enum editorRecoveryLoadStatus load_status =
			editorRecoveryLoadSessionFromPath(E.recovery_path, &session);
	if (load_status == EDITOR_RECOVERY_LOAD_NOT_FOUND) {
		return 0;
	}
	if (load_status == EDITOR_RECOVERY_LOAD_OOM) {
		editorSetAllocFailureStatus();
		return 0;
	}
	if (load_status == EDITOR_RECOVERY_LOAD_IO) {
		editorSetStatusMsg("Recovery load failed (%s)", strerror(errno));
		return 0;
	}
	if (load_status == EDITOR_RECOVERY_LOAD_INVALID) {
		(void)unlink(E.recovery_path);
		editorSetStatusMsg("Recovery data was invalid and was discarded");
		return 0;
	}

	int applied = editorRecoveryApplySession(&session);
	editorRecoverySessionFree(&session);
	if (!applied) {
		return 0;
	}

	editorSetStatusMsg("Recovered previous session");
	return 1;
}

static int editorRecoveryPromptRestoreChoice(void) {
	while (1) {
		editorSetStatusMsg("Recovery data found. Restore previous session? (y/N)");
		editorRefreshScreen();

		int key = editorReadKey();
		if (key == RESIZE_EVENT) {
			(void)editorRefreshWindowSize();
			continue;
		}
		if (key == MOUSE_EVENT) {
			continue;
		}
		if (key == INPUT_EOF_EVENT) {
			return 0;
		}
		if (key == 'y' || key == 'Y') {
			return 1;
		}
		if (key == 'n' || key == 'N' || key == '\x1b' || key == '\r') {
			return 0;
		}
	}
}

int editorRecoveryPromptAndMaybeRestore(void) {
	if (!editorRecoveryHasSnapshot()) {
		return 0;
	}

	if (editorRecoveryPromptRestoreChoice()) {
		return editorRecoveryRestoreSnapshot();
	}

	editorRecoveryCleanupOnCleanExit();
	editorSetStatusMsg("Discarded recovery data");
	return 0;
}

void editorRecoveryMaybeAutosaveOnActivity(void) {
	if (E.recovery_path == NULL) {
		return;
	}

	if (!editorTabAnyDirty()) {
		editorRecoveryCleanupOnCleanExit();
		return;
	}

	time_t now = time(NULL);
	if (now != (time_t)-1 &&
			E.recovery_last_autosave_time != 0 &&
			now - E.recovery_last_autosave_time < ROTIDE_RECOVERY_AUTOSAVE_DEBOUNCE_SECONDS) {
		return;
	}

	if (!editorRecoveryWriteSnapshotAtomic()) {
		int saved_errno = errno;
		if (saved_errno != 0) {
			editorSetStatusMsg("Recovery autosave failed (%s)", strerror(saved_errno));
		} else {
			editorSetStatusMsg("Recovery autosave failed");
		}
		if (now != (time_t)-1) {
			E.recovery_last_autosave_time = now;
		}
		return;
	}

	if (now != (time_t)-1) {
		E.recovery_last_autosave_time = now;
	}
}

int editorStartupLoadRecoveryOrOpenArgs(int argc, char *argv[]) {
	int restored = editorRecoveryPromptAndMaybeRestore();
	if (!restored && argc >= 2) {
		editorOpen(argv[1]);
		for (int i = 2; i < argc; i++) {
			if (!editorTabOpenFileAsNew(argv[i])) {
				break;
			}
		}
		(void)editorTabSwitchToIndex(0);
	}
	return restored;
}
