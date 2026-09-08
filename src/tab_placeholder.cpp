#include "tab_placeholder.h"

void build_placeholder_tab(lv_obj_t *tab, const char *text) {
  lv_obj_t *label = lv_label_create(tab);
  lv_label_set_text(label, text);
  lv_obj_center(label);
}
