#ifndef DAP_SAMPLE_ITEMS_H
#define DAP_SAMPLE_ITEMS_H

struct item {
	int id;
	char name[32];
	double score;
	struct item *next;
};

struct item *item_new(int id, const char *name, double score);
void item_list_free(struct item *head);
struct item *build_demo_list(void);
int pointer_walk(const struct item *head, int target_id);
int mutate_item(struct item *node, int delta);

#endif
