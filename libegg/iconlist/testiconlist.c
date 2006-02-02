/* testiconlist.c
 * Copyright (C) 2002  Anders Carlsson <andersca@gnu.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gtk/gtk.h>
#include <sys/types.h>
#include <string.h>
#include <dirent.h>
#include "eggiconlist.h"
#include "prop-editor.h"

#define NUMBER_OF_ITEMS 10

static void
fill_model (GtkTreeModel *model)
{
  GdkPixbuf *pixbuf;
  int i, j;
  char *str, *str2;
  GtkTreeIter iter;
  GtkListStore *store = GTK_LIST_STORE (model);
  
  pixbuf = gdk_pixbuf_new_from_file ("gnome-textfile.png", NULL);

  i = 0;
  
  gtk_list_store_prepend (store, &iter);

  gtk_list_store_set (store, &iter,
		      0, pixbuf,
		      1, "Really really\nreally really loooooooooong item name",
		      2, 0,
		      3, "This is a <b>Test</b> of <i>markup</i>",
		      -1);
  
  while (i < NUMBER_OF_ITEMS - 1)
    {
      str = g_strdup_printf ("Icon %d", i);
      str2 = g_strdup_printf ("Icon <b>%d</b>", i);	
      gtk_list_store_prepend (store, &iter);
      gtk_list_store_set (store, &iter,
			  0, pixbuf,
			  1, str,
			  2, i,
			  3, str2,
			  -1);
      g_free (str);
      g_free (str2);
      i++;
    }

  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store), 2, GTK_SORT_ASCENDING);
}

static GtkTreeModel *
create_model (void)
{
  GtkListStore *store;
  
  store = gtk_list_store_new (4, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING);

  return GTK_TREE_MODEL (store);
}


static void
foreach_selected_remove (GtkWidget *button, EggIconList *icon_list)
{
  GtkTreeIter iter;
  GtkTreeModel *model;

  GList *list, *selected;

  selected = egg_icon_list_get_selected_items (icon_list);
  model = egg_icon_list_get_model (icon_list);
  
  for (list = selected; list; list = list->next)
    {
      GtkTreePath *path = list->data;

      gtk_tree_model_get_iter (model, &iter, path);
      gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
      
      gtk_tree_path_free (path);
    } 
  
  g_list_free (selected);
}


static void
selection_changed (EggIconList *icon_list)
{
  g_print ("Selection changed!\n");
}

static void
item_cb (GtkWidget       *menuitem,
	 GtkTreePath     *path,
	 EggIconList     *icon_list)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  gchar *text;
  model = egg_icon_list_get_model (EGG_ICON_LIST (icon_list));
  
  gtk_tree_model_get_iter (model, &iter, path);

  gtk_tree_model_get (model, &iter,
		      1, &text, -1);
  g_print ("Item activated, text is %s\n", text);
}

static void
do_popup_menu (GtkWidget      *icon_list, 
	       GdkEventButton *event)
{
  GtkWidget *menu;
  GtkWidget *menuitem;
  GtkTreePath *path;
  int button, event_time;

  path = egg_icon_list_get_path_at_pos (EGG_ICON_LIST (icon_list), 
                                        event->x, event->y);
  g_print ("foo: %p\n", path);
  
  if (!path)
    return;

  menu = gtk_menu_new ();

  menuitem = gtk_menu_item_new_with_label ("Activate");
  gtk_widget_show (menuitem);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
  g_signal_connect (menuitem, "activate", G_CALLBACK (item_cb), path);

  if (event)
    {
      button = event->button;
      event_time = event->time;
    }
  else
    {
      button = 0;
      event_time = gtk_get_current_event_time ();
    }

  gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 
                  button, event_time);
  gtk_tree_path_free (path);
}
	

static gboolean
button_press_event_handler (GtkWidget      *widget, 
			    GdkEventButton *event)
{
  /* Ignore double-clicks and triple-clicks */
  if (event->button == 3 && event->type == GDK_BUTTON_PRESS)
    {
      do_popup_menu (widget, event);
      return TRUE;
    }

  return FALSE;
}

static gboolean
popup_menu_handler (GtkWidget *widget)
{
  do_popup_menu (widget, NULL);
  return TRUE;
}
	
gint
main (gint argc, gchar **argv)
{
  GtkWidget *paned;
  GtkWidget *window, *icon_list, *scrolled_window;
  GtkWidget *vbox, *bbox;
  GtkWidget *button;
  GtkWidget *prop_editor;
  GtkTreeModel *model;
  
  gtk_init (&argc, &argv);

  /* to test rtl layout, set RTL=1 in the environment */
  if (g_getenv ("RTL"))
    gtk_widget_set_default_direction (GTK_TEXT_DIR_RTL);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size (GTK_WINDOW (window), 700, 400);

  paned = gtk_hpaned_new ();
  gtk_container_add (GTK_CONTAINER (window), paned);

  icon_list = egg_icon_list_new ();
  egg_icon_list_set_selection_mode (EGG_ICON_LIST (icon_list), GTK_SELECTION_MULTIPLE);

  g_signal_connect_after (icon_list, "button_press_event",
			  G_CALLBACK (button_press_event_handler), NULL);
  g_signal_connect (icon_list, "selection_changed",
		    G_CALLBACK (selection_changed), NULL);
  g_signal_connect (icon_list, "popup_menu",
		    G_CALLBACK (popup_menu_handler), NULL);

  g_signal_connect (icon_list, "item_activated",
		    G_CALLBACK (item_cb), icon_list);
  
  model = create_model ();
  egg_icon_list_set_model (EGG_ICON_LIST (icon_list), model);
  fill_model (model);
  egg_icon_list_set_pixbuf_column (EGG_ICON_LIST (icon_list), 0);
  egg_icon_list_set_text_column (EGG_ICON_LIST (icon_list), 1);
  
  prop_editor = create_prop_editor (G_OBJECT (icon_list), 0);
  gtk_widget_show_all (prop_editor);
  
  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_container_add (GTK_CONTAINER (scrolled_window), icon_list);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
  				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  vbox = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), scrolled_window, TRUE, TRUE, 0);
  bbox = gtk_hbutton_box_new ();
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_START);
  gtk_box_pack_start (GTK_BOX (vbox), bbox, FALSE, FALSE, 0);

  button = gtk_button_new_with_label ("Foreach print");
//  g_signal_connect (button, "clicked", G_CALLBACK (foreach_print), icon_list);
  gtk_box_pack_start_defaults (GTK_BOX (bbox), button);

  button = gtk_button_new_with_label ("Remove selected");
  g_signal_connect (button, "clicked", G_CALLBACK (foreach_selected_remove), icon_list);
  gtk_box_pack_start_defaults (GTK_BOX (bbox), button);
  
  gtk_paned_pack1 (GTK_PANED (paned), vbox, TRUE, FALSE);

  icon_list = egg_icon_list_new ();
  
  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_container_add (GTK_CONTAINER (scrolled_window), icon_list);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_paned_pack2 (GTK_PANED (paned), scrolled_window, TRUE, FALSE);

  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}
