#include <gtk/gtkliststore.h>
#include <gtk/gtktreestore.h>

void	egg_list_store_reorder (GtkListStore *list_store,
				gint         *new_order);
void	egg_tree_store_reorder (GtkTreeStore *tree_store,
				GtkTreeIter  *parent_iter,
				gint         *new_order);
