#include "items.h"

#include <stdio.h>
#include <stdlib.h>

struct item *item_new(int id, const char *name, double score) {
	struct item *node = calloc(1, sizeof(*node));
	if (node == NULL) {
		return NULL;
	}
	node->id = id;
	node->score = score;
	snprintf(node->name, sizeof(node->name), "%s", name != NULL ? name : "");
	return node;
}

void item_list_free(struct item *head) {
	while (head != NULL) {
		struct item *next = head->next;
		free(head);
		head = next;
	}
}

struct item *build_demo_list(void) {
	struct item *a = item_new(1, "first", 10.5);
	struct item *b = item_new(2, "second", 22.75);
	struct item *c = item_new(3, "third", 35.125);
	if (a == NULL || b == NULL || c == NULL) {
		item_list_free(a);
		item_list_free(b);
		item_list_free(c);
		return NULL;
	}
	a->next = b;
	b->next = c;
	return a;
}

int pointer_walk(const struct item *head, int target_id) {
	const struct item *it = head;
	while (it != NULL) {
		if (it->id == target_id) {
			return (int)it->score;
		}
		it = it->next;
	}
	return -1;
}

int mutate_item(struct item *node, int delta) {
	/* DAP_BP_MUTATE_ITEM */
	if (node == NULL) {
		return 0;
	}
	node->id += delta;
	node->score += (double)delta * 0.25;
	return node->id;
}
