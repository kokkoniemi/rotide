#ifndef TEST_CASE_H
#define TEST_CASE_H

struct editorTestCase {
	const char *name;
	int (*run)(void);
};

#endif
