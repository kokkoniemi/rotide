#include "render/ansi_style.h"

#include <stdio.h>
#include <string.h>

#define VT100_INVERTED_COLORS_4 "\x1b[7m"
#define VT100_NORMAL_COLORS_3 "\x1b[m"
#define VT100_FG_BLACK_5 "\x1b[30m"
#define VT100_FG_RED_5 "\x1b[31m"
#define VT100_FG_GREEN_5 "\x1b[32m"
#define VT100_FG_YELLOW_5 "\x1b[33m"
#define VT100_FG_BLUE_5 "\x1b[34m"
#define VT100_FG_MAGENTA_5 "\x1b[35m"
#define VT100_FG_CYAN_5 "\x1b[36m"
#define VT100_FG_WHITE_5 "\x1b[37m"
#define VT100_FG_GRAY_5 "\x1b[90m"
#define VT100_FG_BRIGHT_RED_5 "\x1b[91m"
#define VT100_FG_BRIGHT_GREEN_5 "\x1b[92m"
#define VT100_FG_BRIGHT_YELLOW_5 "\x1b[93m"
#define VT100_FG_BRIGHT_BLUE_5 "\x1b[94m"
#define VT100_FG_BRIGHT_MAGENTA_5 "\x1b[95m"
#define VT100_FG_BRIGHT_CYAN_5 "\x1b[96m"
#define VT100_FG_BRIGHT_WHITE_5 "\x1b[97m"
#define VT100_CURSOR_COLOR_WHITE "\x1b]12;white\a"

int editorThemeColorEquals(struct editorThemeColor a, struct editorThemeColor b) {
	return a.kind == b.kind && a.value == b.value && a.r == b.r && a.g == b.g && a.b == b.b;
}

int editorThemeColorIsDefault(struct editorThemeColor color) {
	return color.kind == EDITOR_THEME_COLOR_DEFAULT;
}

int editorAppendThemeColor(struct writeBuf *wb, struct editorThemeColor color, int bg) {
	if (color.kind == EDITOR_THEME_COLOR_DEFAULT) {
		return wbAppend(wb, bg ? "\x1b[49m" : "\x1b[39m", 5);
	}
	if (color.kind == EDITOR_THEME_COLOR_ANSI) {
		static const char *fg_sequences[EDITOR_THEME_ANSI_COUNT] = {
			VT100_FG_BLACK_5,
			VT100_FG_RED_5,
			VT100_FG_GREEN_5,
			VT100_FG_YELLOW_5,
			VT100_FG_BLUE_5,
			VT100_FG_MAGENTA_5,
			VT100_FG_CYAN_5,
			VT100_FG_WHITE_5,
			VT100_FG_GRAY_5,
			VT100_FG_BRIGHT_RED_5,
			VT100_FG_BRIGHT_GREEN_5,
			VT100_FG_BRIGHT_YELLOW_5,
			VT100_FG_BRIGHT_BLUE_5,
			VT100_FG_BRIGHT_MAGENTA_5,
			VT100_FG_BRIGHT_CYAN_5,
			VT100_FG_BRIGHT_WHITE_5,
		};
		static const char *bg_sequences[EDITOR_THEME_ANSI_COUNT] = {
			"\x1b[40m", "\x1b[41m", "\x1b[42m", "\x1b[43m",
			"\x1b[44m", "\x1b[45m", "\x1b[46m", "\x1b[47m",
			"\x1b[100m", "\x1b[101m", "\x1b[102m", "\x1b[103m",
			"\x1b[104m", "\x1b[105m", "\x1b[106m", "\x1b[107m",
		};
		if (color.value >= EDITOR_THEME_ANSI_COUNT) {
			return wbAppend(wb, bg ? "\x1b[49m" : "\x1b[39m", 5);
		}
		const char *sequence = bg ? bg_sequences[color.value] : fg_sequences[color.value];
		return wbAppend(wb, sequence, strlen(sequence));
	}

	char sequence[32];
	int len = 0;
	if (color.kind == EDITOR_THEME_COLOR_256) {
		len = snprintf(sequence, sizeof(sequence), "\x1b[%d;5;%um", bg ? 48 : 38,
				(unsigned int)color.value);
	} else {
		len = snprintf(sequence, sizeof(sequence), "\x1b[%d;2;%u;%u;%um", bg ? 48 : 38,
				(unsigned int)color.r, (unsigned int)color.g, (unsigned int)color.b);
	}
	if (len <= 0 || len >= (int)sizeof(sequence)) {
		return 0;
	}
	return wbAppend(wb, sequence, (size_t)len);
}

int editorAppendThemeForeground(struct writeBuf *wb, struct editorThemeColor color) {
	return editorAppendThemeColor(wb, color, 0);
}

int editorAppendThemeBackground(struct writeBuf *wb, struct editorThemeColor color) {
	return editorAppendThemeColor(wb, color, 1);
}

int editorAppendThemeBaseForeground(struct writeBuf *wb) {
	return editorAppendThemeForeground(wb, E.theme.ui[EDITOR_THEME_UI_FOREGROUND]);
}

int editorAppendThemeBaseStyle(struct writeBuf *wb) {
	if (!editorThemeColorIsDefault(E.theme.ui[EDITOR_THEME_UI_FOREGROUND]) &&
			!editorAppendThemeForeground(wb, E.theme.ui[EDITOR_THEME_UI_FOREGROUND])) {
		return 0;
	}
	if (!editorThemeColorIsDefault(E.theme.ui[EDITOR_THEME_UI_BACKGROUND]) &&
			!editorAppendThemeBackground(wb, E.theme.ui[EDITOR_THEME_UI_BACKGROUND])) {
		return 0;
	}
	return 1;
}

int editorAppendThemeReset(struct writeBuf *wb) {
	return wbAppend(wb, VT100_NORMAL_COLORS_3, 3) && editorAppendThemeBaseStyle(wb);
}

int editorAppendThemeStyle(struct writeBuf *wb, enum editorThemeStyleRole role) {
	if (role < 0 || role >= EDITOR_THEME_STYLE_ROLE_COUNT) {
		return 1;
	}
	struct editorThemeStyle style = E.theme.styles[role];
	if (style.reverse) {
		return wbAppend(wb, VT100_INVERTED_COLORS_4, 4);
	}
	if (!editorAppendThemeForeground(wb, style.fg)) {
		return 0;
	}
	return editorAppendThemeBackground(wb, style.bg);
}

int editorAppendThemeForegroundRole(struct writeBuf *wb, enum editorThemeUiRole role) {
	if (role < 0 || role >= EDITOR_THEME_UI_ROLE_COUNT) {
		return 1;
	}
	return editorAppendThemeForeground(wb, E.theme.ui[role]);
}

int editorAppendThemeBackgroundRole(struct writeBuf *wb, enum editorThemeUiRole role) {
	if (role < 0 || role >= EDITOR_THEME_UI_ROLE_COUNT) {
		return 1;
	}
	return editorAppendThemeBackground(wb, E.theme.ui[role]);
}

int editorAppendThemeCursorColor(struct writeBuf *wb) {
	struct editorThemeColor color = E.theme.ui[EDITOR_THEME_UI_CURSOR];
	if (color.kind == EDITOR_THEME_COLOR_DEFAULT) {
		return wbAppend(wb, VT100_CURSOR_COLOR_WHITE, strlen(VT100_CURSOR_COLOR_WHITE));
	}
	if (color.kind == EDITOR_THEME_COLOR_ANSI) {
		static const char *names[EDITOR_THEME_ANSI_COUNT] = {
			"black", "red", "green", "yellow", "blue", "magenta", "cyan", "white",
			"brightblack", "brightred", "brightgreen", "brightyellow", "brightblue",
			"brightmagenta", "brightcyan", "brightwhite",
		};
		if (color.value == EDITOR_THEME_ANSI_WHITE || color.value >= EDITOR_THEME_ANSI_COUNT) {
			return wbAppend(wb, VT100_CURSOR_COLOR_WHITE, strlen(VT100_CURSOR_COLOR_WHITE));
		}
		char sequence[40];
		int len = snprintf(sequence, sizeof(sequence), "\x1b]12;%s\a", names[color.value]);
		if (len <= 0 || len >= (int)sizeof(sequence)) {
			return 0;
		}
		return wbAppend(wb, sequence, (size_t)len);
	}
	if (color.kind == EDITOR_THEME_COLOR_256) {
		return wbAppend(wb, VT100_CURSOR_COLOR_WHITE, strlen(VT100_CURSOR_COLOR_WHITE));
	}
	char sequence[40];
	int len = snprintf(sequence, sizeof(sequence), "\x1b]12;rgb:%02x/%02x/%02x\a",
			(unsigned int)color.r, (unsigned int)color.g, (unsigned int)color.b);
	if (len <= 0 || len >= (int)sizeof(sequence)) {
		return 0;
	}
	return wbAppend(wb, sequence, (size_t)len);
}
