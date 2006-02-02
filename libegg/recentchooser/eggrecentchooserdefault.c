/* GTK - The GIMP Toolkit
 * eggrecentchooserdefault.c
 * Copyright (C) 2005-2006, Emmanuele Bassi
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <glib/gi18n.h>

#include <gtk/gtkstock.h>
#include <gtk/gtkicontheme.h>
#include <gtk/gtkiconfactory.h>
#include <gtk/gtksettings.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkcelllayout.h>
#include <gtk/gtkcellrendererpixbuf.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkcheckmenuitem.h>
#include <gtk/gtkclipboard.h>
#include <gtk/gtkcombobox.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkeventbox.h>
#include <gtk/gtkexpander.h>
#include <gtk/gtkframe.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkhpaned.h>
#include <gtk/gtkiconfactory.h>
#include <gtk/gtkicontheme.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkimagemenuitem.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkseparatormenuitem.h>
#include <gtk/gtksizegroup.h>
#include <gtk/gtkstock.h>
#include <gtk/gtktable.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtktreemodelsort.h>
#include <gtk/gtktreemodelfilter.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtktreestore.h>
#include <gtk/gtktooltips.h>
#include <gtk/gtktypebuiltins.h>
#include <gtk/gtkvbox.h>

#include "eggrecentmanager.h"
#include "eggrecentfilter.h"
#include "eggrecentchooser.h"
#include "eggrecentchooserprivate.h"
#include "eggrecentchooserutils.h"
#include "eggrecentchooserdefault.h"
#include "eggrecenttypebuiltins.h"



struct _EggRecentChooserDefault
{
  GtkVBox parent_instance;
  
  EggRecentManager *manager;
  gulong manager_changed_id;
  
  gint icon_size;

  /* RecentChooser properties */
  gint limit;  
  EggRecentSortType sort_type;
  guint show_private : 1;
  guint show_not_found : 1;
  guint select_multiple : 1;
  guint show_numbers : 1;
  guint show_tips : 1;
  guint show_icons : 1;
  guint local_only : 1;
  
  GSList *filters;
  EggRecentFilter *current_filter;
  GtkWidget *filter_combo_hbox;
  GtkWidget *filter_combo;
  
  EggRecentSortFunc sort_func;
  gpointer sort_data;
  GDestroyNotify sort_data_destroy;

  GtkTooltips *tooltips;

  GtkIconTheme *icon_theme;
  
  GtkWidget *recent_view;
  GtkListStore *recent_store;
  GtkTreeModel *recent_store_filter;
  GtkTreeViewColumn *icon_column;
  GtkTreeViewColumn *meta_column;
  GtkCellRenderer *meta_renderer;
  GtkTreeSelection *selection;
  
  GtkWidget *recent_popup_menu;
  GtkWidget *recent_popup_menu_copy_item;
  GtkWidget *recent_popup_menu_remove_item;
  GtkWidget *recent_popup_menu_clear_item;
  GtkWidget *recent_popup_menu_show_private_item;
 
  guint load_id;
  GList *recent_items;
  gint n_recent_items;
  gint loaded_items;
  guint load_state;
};

typedef struct _EggRecentChooserDefaultClass
{
  GtkVBoxClass parent_class;
} EggRecentChooserDefaultClass;

enum {
  RECENT_URI_COLUMN,
  RECENT_DISPLAY_NAME_COLUMN,
  RECENT_INFO_COLUMN,
    
  N_RECENT_COLUMNS
};

enum {
  LOAD_EMPTY,    /* initial state: the model is empty */
  LOAD_PRELOAD,  /* the model is loading and not inserted in the tree yet */
  LOAD_LOADING,  /* the model is fully loaded but not inserted */
  LOAD_FINISHED  /* the model is fully loaded and inserted */
};

enum {
  TEXT_URI_LIST
};

/* Target types for DnD from the file list */
static const GtkTargetEntry recent_list_source_targets[] = {
  { "text/uri-list", 0, TEXT_URI_LIST }
};

static const int num_recent_list_source_targets = (sizeof (recent_list_source_targets)
                                                   / sizeof (recent_list_source_targets[0]));

/* Icon size for if we can't get it from the theme */
#define FALLBACK_ICON_SIZE  48
#define FALLBACK_ITEM_LIMIT 20

#define NUM_CHARS 40
#define NUM_LINES 9



/* GObject */
static void     egg_recent_chooser_default_class_init   (EggRecentChooserDefaultClass *klass);
static void     egg_recent_chooser_default_init         (EggRecentChooserDefault      *impl);
static GObject *egg_recent_chooser_default_constructor  (GType                         type,
						         guint                         n_construct_prop,
						         GObjectConstructParam        *construct_params);
static void     egg_recent_chooser_default_finalize     (GObject                      *object);
static void     egg_recent_chooser_default_set_property (GObject                      *object,
						         guint                         prop_id,
						         const GValue                 *value,
						         GParamSpec                   *pspec);
static void     egg_recent_chooser_default_get_property (GObject                      *object,
						         guint                         prop_id,
						         GValue                       *value,
						         GParamSpec                   *pspec);

/* EggRecentChooserIface */
static void              egg_recent_chooser_iface_init                 (EggRecentChooserIface  *iface);
static gboolean          egg_recent_chooser_default_set_current_uri    (EggRecentChooser       *chooser,
								        const gchar            *uri,
								        GError                **error);
static gchar *           egg_recent_chooser_default_get_current_uri    (EggRecentChooser       *chooser);
static gboolean          egg_recent_chooser_default_select_uri         (EggRecentChooser       *chooser,
								        const gchar            *uri,
								        GError                **error);
static void              egg_recent_chooser_default_unselect_uri       (EggRecentChooser       *chooser,
								        const gchar            *uri);
static void              egg_recent_chooser_default_select_all         (EggRecentChooser       *chooser);
static void              egg_recent_chooser_default_unselect_all       (EggRecentChooser       *chooser);
static GList *           egg_recent_chooser_default_get_items          (EggRecentChooser       *chooser);
static EggRecentManager *egg_recent_chooser_default_get_recent_manager (EggRecentChooser       *chooser);
static void              egg_recent_chooser_default_set_sort_func      (EggRecentChooser       *chooser,
									EggRecentSortFunc       sort_func,
									gpointer                sort_data,
									GDestroyNotify          data_destroy);
static void              egg_recent_chooser_default_add_filter         (EggRecentChooser       *chooser,
								        EggRecentFilter        *filter);
static void              egg_recent_chooser_default_remove_filter      (EggRecentChooser       *chooser,
								        EggRecentFilter        *filter);
static GSList *          egg_recent_chooser_default_list_filters       (EggRecentChooser       *chooser);


static void egg_recent_chooser_default_map      (GtkWidget *widget);
static void egg_recent_chooser_default_show_all (GtkWidget *widget);

static void set_current_filter        (EggRecentChooserDefault *impl,
				       EggRecentFilter         *filter);

static GtkIconTheme *get_icon_theme_for_widget (GtkWidget *widget);
static gint          get_icon_size_for_widget  (GtkWidget *widget);

static void reload_recent_items (EggRecentChooserDefault *impl);
static void chooser_set_model   (EggRecentChooserDefault *impl);

static void set_recent_manager (EggRecentChooserDefault *impl,
				EggRecentManager        *manager);

static void chooser_set_sort_type (EggRecentChooserDefault *impl,
				   EggRecentSortType        sort_type);

static gboolean recent_store_filter_func (GtkTreeModel *model,
					  GtkTreeIter  *iter,
					  gpointer      user_data);

static void recent_manager_changed_cb (EggRecentManager  *manager,
			               gpointer           user_data);
static void recent_icon_data_func     (GtkTreeViewColumn *tree_column,
				       GtkCellRenderer   *cell,
				       GtkTreeModel      *model,
				       GtkTreeIter       *iter,
				       gpointer           user_data);
static void recent_meta_data_func     (GtkTreeViewColumn *tree_column,
				       GtkCellRenderer   *cell,
				       GtkTreeModel      *model,
				       GtkTreeIter       *iter,
				       gpointer           user_data);

static void selection_changed_cb      (GtkTreeSelection  *z,
				       gpointer           user_data);
static void row_activated_cb          (GtkTreeView       *tree_view,
				       GtkTreePath       *tree_path,
				       GtkTreeViewColumn *tree_column,
				       gpointer           user_data);
static void filter_combo_changed_cb   (GtkComboBox       *combo_box,
				       gpointer           user_data);

static void remove_all_activated_cb   (GtkMenuItem       *menu_item,
				       gpointer           user_data);
static void remove_item_activated_cb  (GtkMenuItem       *menu_item,
				       gpointer           user_data);
static void show_private_toggled_cb   (GtkCheckMenuItem  *menu_item,
				       gpointer           user_data);

static gboolean recent_view_popup_menu_cb   (GtkWidget      *widget,
					     gpointer        user_data);
static gboolean recent_view_button_press_cb (GtkWidget      *widget,
					     GdkEventButton *event,
					     gpointer        user_data);

static void     recent_view_drag_data_get_cb      (GtkWidget        *widget,
						   GdkDragContext   *context,
						   GtkSelectionData *selection_data,
						   guint             info,
						   guint32           time_,
						   gpointer          data);

G_DEFINE_TYPE_WITH_CODE (EggRecentChooserDefault,
			 egg_recent_chooser_default,
			 GTK_TYPE_VBOX,
			 G_IMPLEMENT_INTERFACE (EGG_TYPE_RECENT_CHOOSER,
				 		egg_recent_chooser_iface_init));




static void
egg_recent_chooser_iface_init (EggRecentChooserIface *iface)
{
  iface->set_current_uri = egg_recent_chooser_default_set_current_uri;
  iface->get_current_uri = egg_recent_chooser_default_get_current_uri;
  iface->select_uri = egg_recent_chooser_default_select_uri;
  iface->unselect_uri = egg_recent_chooser_default_unselect_uri;
  iface->select_all = egg_recent_chooser_default_select_all;
  iface->unselect_all = egg_recent_chooser_default_unselect_all;
  iface->get_items = egg_recent_chooser_default_get_items;
  iface->get_recent_manager = egg_recent_chooser_default_get_recent_manager;
  iface->set_sort_func = egg_recent_chooser_default_set_sort_func;
  iface->add_filter = egg_recent_chooser_default_add_filter;
  iface->remove_filter = egg_recent_chooser_default_remove_filter;
  iface->list_filters = egg_recent_chooser_default_list_filters;
}

static void
egg_recent_chooser_default_class_init (EggRecentChooserDefaultClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gobject_class->constructor = egg_recent_chooser_default_constructor;  
  gobject_class->finalize = egg_recent_chooser_default_finalize;
  gobject_class->set_property = egg_recent_chooser_default_set_property;
  gobject_class->get_property = egg_recent_chooser_default_get_property;
  
  widget_class->map = egg_recent_chooser_default_map;
  widget_class->show_all = egg_recent_chooser_default_show_all;
  
  _egg_recent_chooser_install_properties (gobject_class);
}

static void
egg_recent_chooser_default_init (EggRecentChooserDefault *impl)
{
  gtk_box_set_spacing (GTK_BOX (impl), 12);
  
  impl->limit = FALLBACK_ITEM_LIMIT;
  impl->sort_type = EGG_RECENT_SORT_NONE;
  impl->show_private = FALSE;
  impl->show_not_found = FALSE;
  impl->select_multiple = FALSE;
  impl->local_only = TRUE;
  
  impl->icon_size = FALLBACK_ICON_SIZE;
  impl->icon_theme = NULL;
  
  impl->current_filter = NULL;

  impl->tooltips = gtk_tooltips_new ();
  g_object_ref (impl->tooltips);
  gtk_object_sink (GTK_OBJECT (impl->tooltips));
  
  impl->recent_items = NULL;
  impl->n_recent_items = 0;
  impl->loaded_items = 0;
  
  impl->load_state = LOAD_EMPTY;
}

static GObject *
egg_recent_chooser_default_constructor (GType                  type,
				        guint                  n_construct_prop,
				        GObjectConstructParam *construct_params)
{
  EggRecentChooserDefault *impl;
  GObject *object;
  
  GtkWidget *scrollw;
  GtkCellRenderer *renderer;
  
  object = G_OBJECT_CLASS (egg_recent_chooser_default_parent_class)->constructor (type, n_construct_prop, construct_params);

  impl = EGG_RECENT_CHOOSER_DEFAULT (object);
  
  g_assert (impl->manager);
  
  gtk_widget_push_composite_child ();
  
  scrollw = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrollw),
  				       GTK_SHADOW_IN);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrollw),
  				  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start (GTK_BOX (impl), scrollw, TRUE, TRUE, 0);
  gtk_widget_show (scrollw);
  
  impl->recent_view = gtk_tree_view_new ();
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (impl->recent_view), FALSE);
  g_signal_connect (impl->recent_view, "row-activated",
                    G_CALLBACK (row_activated_cb), impl);
  g_signal_connect (impl->recent_view, "popup-menu",
  		    G_CALLBACK (recent_view_popup_menu_cb), impl);
  g_signal_connect (impl->recent_view, "button-press-event",
  		    G_CALLBACK (recent_view_button_press_cb), impl);
  g_object_set_data (G_OBJECT (impl->recent_view), "EggRecentChooserDefault", impl);
  
  gtk_container_add (GTK_CONTAINER (scrollw), impl->recent_view);
  gtk_widget_show (impl->recent_view);
  
  impl->icon_column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_expand (impl->icon_column, FALSE);
  gtk_tree_view_column_set_resizable (impl->icon_column, FALSE);
  
  renderer = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (impl->icon_column, renderer, FALSE);
  gtk_tree_view_column_set_cell_data_func (impl->icon_column,
  					   renderer,
  					   recent_icon_data_func,
  					   impl,
  					   NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (impl->recent_view),
                               impl->icon_column);
  
  impl->meta_column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_expand (impl->meta_column, TRUE);
  gtk_tree_view_column_set_resizable (impl->meta_column, FALSE);
  
  impl->meta_renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (impl->meta_renderer),
                "ellipsize", PANGO_ELLIPSIZE_END,
                NULL);
  gtk_tree_view_column_pack_start (impl->meta_column, impl->meta_renderer, TRUE);
  gtk_tree_view_column_set_cell_data_func (impl->meta_column,
  					   impl->meta_renderer,
  					   recent_meta_data_func,
  					   impl,
  					   NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (impl->recent_view),
                               impl->meta_column);
  
  impl->selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->recent_view));
  gtk_tree_selection_set_mode (impl->selection, GTK_SELECTION_SINGLE);
  g_signal_connect (impl->selection, "changed", G_CALLBACK (selection_changed_cb), impl);

  /* drag and drop */
  gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (impl->recent_view),
		  			  GDK_BUTTON1_MASK,
					  recent_list_source_targets,
					  num_recent_list_source_targets,
					  GDK_ACTION_COPY);

  g_signal_connect (impl->recent_view, "drag-data-get",
		    G_CALLBACK (recent_view_drag_data_get_cb), impl);
  
  impl->filter_combo_hbox = gtk_hbox_new (FALSE, 12);
  
  impl->filter_combo = gtk_combo_box_new_text ();
  gtk_combo_box_set_focus_on_click (GTK_COMBO_BOX (impl->filter_combo), FALSE);
  g_signal_connect (impl->filter_combo, "changed",
                    G_CALLBACK (filter_combo_changed_cb), impl);
  
  gtk_box_pack_end (GTK_BOX (impl->filter_combo_hbox),
                    impl->filter_combo,
                    FALSE, FALSE, 0);
  gtk_widget_show (impl->filter_combo);
  
  gtk_box_pack_end (GTK_BOX (impl), impl->filter_combo_hbox, FALSE, FALSE, 0);
  
  gtk_widget_pop_composite_child ();
  
  impl->recent_store = gtk_list_store_new (N_RECENT_COLUMNS,
  					   G_TYPE_STRING,       /* uri */
  					   G_TYPE_STRING,       /* display_name */
  					   EGG_TYPE_RECENT_INFO /* info */);
  
  return object;
}

static void
egg_recent_chooser_default_set_property (GObject      *object,
				         guint         prop_id,
					 const GValue *value,
					 GParamSpec   *pspec)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (object);
  
  switch (prop_id)
    {
    case EGG_RECENT_CHOOSER_PROP_RECENT_MANAGER:
      set_recent_manager (impl, g_value_get_object (value));
      break;
    case EGG_RECENT_CHOOSER_PROP_SHOW_PRIVATE:
      impl->show_private = g_value_get_boolean (value);
      
      if (impl->recent_store && impl->recent_store_filter)
        gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (impl->recent_store_filter));

      if (impl->recent_popup_menu_show_private_item)
	{
          GtkCheckMenuItem *item = GTK_CHECK_MENU_ITEM (impl->recent_popup_menu_show_private_item);
	  g_signal_handlers_block_by_func (item, G_CALLBACK (show_private_toggled_cb), impl);
          gtk_check_menu_item_set_active (item, impl->show_private);
	  g_signal_handlers_unblock_by_func (item, G_CALLBACK (show_private_toggled_cb), impl);
        }
      break;
    case EGG_RECENT_CHOOSER_PROP_SHOW_NOT_FOUND:
      impl->show_not_found = g_value_get_boolean (value);
      
      if (impl->recent_store && impl->recent_store_filter)
        gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (impl->recent_store_filter));
      break;
    case EGG_RECENT_CHOOSER_PROP_SHOW_TIPS:
      g_warning ("%s: Recent Choosers of type `%s' do not support "
                 "showing tooltips",
                 G_STRFUNC,
                 G_OBJECT_TYPE_NAME (object));
      break;
    case EGG_RECENT_CHOOSER_PROP_SHOW_NUMBERS:
      g_warning ("%s: Recent Choosers of type `%s' do not support "
                 "showing numbers",
                 G_STRFUNC,
                 G_OBJECT_TYPE_NAME (object));
      break;
    case EGG_RECENT_CHOOSER_PROP_SHOW_ICONS:
      impl->show_icons = g_value_get_boolean (value);
      gtk_tree_view_column_set_visible (impl->icon_column, impl->show_icons);
      break;
    case EGG_RECENT_CHOOSER_PROP_SELECT_MULTIPLE:
      impl->select_multiple = g_value_get_boolean (value);
      
      if (impl->select_multiple)
        gtk_tree_selection_set_mode (impl->selection, GTK_SELECTION_MULTIPLE);
      else
        gtk_tree_selection_set_mode (impl->selection, GTK_SELECTION_SINGLE);
      break;
    case EGG_RECENT_CHOOSER_PROP_LOCAL_ONLY:
      impl->local_only = g_value_get_boolean (value);
      break;
    case EGG_RECENT_CHOOSER_PROP_LIMIT:
      impl->limit = g_value_get_int (value);
      break;
    case EGG_RECENT_CHOOSER_PROP_SORT_TYPE:
      chooser_set_sort_type (impl, g_value_get_enum (value));
      break;
    case EGG_RECENT_CHOOSER_PROP_FILTER:
      set_current_filter (impl, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
egg_recent_chooser_default_get_property (GObject    *object,
					 guint       prop_id,
					 GValue     *value,
					 GParamSpec *pspec)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (object);
  
  switch (prop_id)
    {
    case EGG_RECENT_CHOOSER_PROP_LIMIT:
      g_value_set_int (value, impl->limit);
      break;
    case EGG_RECENT_CHOOSER_PROP_SORT_TYPE:
      g_value_set_enum (value, impl->sort_type);
      break;
    case EGG_RECENT_CHOOSER_PROP_SHOW_PRIVATE:
      g_value_set_boolean (value, impl->show_private);
      break;
    case EGG_RECENT_CHOOSER_PROP_SHOW_ICONS:
      g_value_set_boolean (value, impl->show_icons);
      break;
    case EGG_RECENT_CHOOSER_PROP_SHOW_NOT_FOUND:
      g_value_set_boolean (value, impl->show_not_found);
      break;
    case EGG_RECENT_CHOOSER_PROP_LOCAL_ONLY:
      g_value_set_boolean (value, impl->local_only);
      break;
    case EGG_RECENT_CHOOSER_PROP_SELECT_MULTIPLE:
      g_value_set_boolean (value, impl->select_multiple);
      break;
    case EGG_RECENT_CHOOSER_PROP_FILTER:
      g_value_set_object (value, impl->current_filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
egg_recent_chooser_default_finalize (GObject *object)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (object);

  if (impl->recent_items)
    {
      g_list_foreach (impl->recent_items,
		      (GFunc) egg_recent_info_unref,
		      NULL);
      g_list_free (impl->recent_items);
      impl->recent_items = NULL;
    }

  if (impl->manager_changed_id)
    {
      g_signal_handler_disconnect (impl->manager, impl->manager_changed_id);
      impl->manager_changed_id = 0;
    }

  if (impl->manager)
    g_object_unref (impl->manager);
  
  if (impl->sort_data_destroy)
    {
      impl->sort_data_destroy (impl->sort_data);
      
      impl->sort_data_destroy = NULL;
      impl->sort_data = NULL;
      impl->sort_func = NULL;
    }
    
  if (impl->filters)
    {
      g_slist_foreach (impl->filters,
      		       (GFunc) g_object_unref,
      		       NULL);
      g_slist_free (impl->filters);    
    }
  
  if (impl->current_filter)
    g_object_unref (impl->current_filter);

  if (impl->recent_store_filter)
    g_object_unref (impl->recent_store_filter);

  if (impl->recent_store)
    g_object_unref (impl->recent_store);

  if (impl->tooltips)
    g_object_unref (impl->tooltips);
  
  G_OBJECT_CLASS (egg_recent_chooser_default_parent_class)->finalize (object);
}

/* override GtkWidget::show_all since we have internal widgets we wish to keep
 * hidden unless we decide otherwise, like the filter combo box.
 */
static void
egg_recent_chooser_default_show_all (GtkWidget *widget)
{
  gtk_widget_show (widget);
}



/* Shows an error dialog set as transient for the specified window */
static void
error_message_with_parent (GtkWindow   *parent,
			   const gchar *msg,
			   const gchar *detail)
{
  GtkWidget *dialog;

  dialog = gtk_message_dialog_new (parent,
				   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_ERROR,
				   GTK_BUTTONS_OK,
				   "%s",
				   msg);
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
					    "%s", detail);

  if (parent->group)
    gtk_window_group_add_window (parent->group, GTK_WINDOW (dialog));

  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

/* Returns a toplevel GtkWindow, or NULL if none */
static GtkWindow *
get_toplevel (GtkWidget *widget)
{
  GtkWidget *toplevel;

  toplevel = gtk_widget_get_toplevel (widget);
  if (!GTK_WIDGET_TOPLEVEL (toplevel))
    return NULL;
  else
    return GTK_WINDOW (toplevel);
}

/* Shows an error dialog for the file chooser */
static void
error_message (EggRecentChooserDefault *impl,
	       const gchar             *msg,
	       const gchar             *detail)
{
  error_message_with_parent (get_toplevel (GTK_WIDGET (impl)), msg, detail);
}

static void
set_busy_cursor (EggRecentChooserDefault *impl,
		 gboolean                 show_busy_cursor)
{
  GtkWindow *toplevel;
  GdkDisplay *display;
  GdkCursor *cursor;

  toplevel = get_toplevel (GTK_WIDGET (impl));
  if (!toplevel || !GTK_WIDGET_REALIZED (toplevel))
    return;
  
  display = gtk_widget_get_display (GTK_WIDGET (toplevel));
  
  cursor = NULL;
  if (show_busy_cursor)
    cursor = gdk_cursor_new_for_display (display, GDK_WATCH);

  gdk_window_set_cursor (GTK_WIDGET (toplevel)->window, cursor);
  gdk_display_flush (display);

  if (cursor)
    gdk_cursor_unref (cursor);
}

static void
chooser_set_model (EggRecentChooserDefault *impl)
{
  g_assert (impl->recent_store != NULL);
  g_assert (impl->recent_store_filter == NULL);
  g_assert (impl->load_state == LOAD_LOADING);
  
  impl->recent_store_filter = gtk_tree_model_filter_new (GTK_TREE_MODEL (impl->recent_store), NULL);
  gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (impl->recent_store_filter),
		  			  recent_store_filter_func,
					  impl,
					  NULL);
  
  gtk_tree_view_set_model (GTK_TREE_VIEW (impl->recent_view),
      			   impl->recent_store_filter);
  gtk_tree_view_columns_autosize (GTK_TREE_VIEW (impl->recent_view));
  gtk_tree_view_set_enable_search (GTK_TREE_VIEW (impl->recent_view), TRUE);
  gtk_tree_view_set_search_column (GTK_TREE_VIEW (impl->recent_view),
  				   RECENT_DISPLAY_NAME_COLUMN);

  impl->load_state = LOAD_FINISHED;
}

static gboolean
load_recent_items (gpointer user_data)
{
  EggRecentChooserDefault *impl;
  EggRecentInfo *info;
  GtkTreeIter iter;
  const gchar *uri, *name;
  gboolean retval;
  
  GDK_THREADS_ENTER ();
  
  impl = EGG_RECENT_CHOOSER_DEFAULT (user_data);
  
  g_assert ((impl->load_state == LOAD_EMPTY) ||
            (impl->load_state == LOAD_PRELOAD));
  
  /* store the items for multiple runs */
  if (!impl->recent_items)
    {
      impl->recent_items = egg_recent_chooser_get_items (EGG_RECENT_CHOOSER (impl));
      if (!impl->recent_items)
        {
          GDK_THREADS_LEAVE ();
          
          return TRUE;
        }
        
      impl->n_recent_items = g_list_length (impl->recent_items);
      impl->loaded_items = 0;
      impl->load_state = LOAD_PRELOAD;
    }
  
  info = (EggRecentInfo *) g_list_nth_data (impl->recent_items,
                                            impl->loaded_items);
  g_assert (info);

  uri = egg_recent_info_get_uri (info);
  name = egg_recent_info_get_display_name (info);
  
  /* at this point, everything goes inside the model; operations on the
   * visualization of items inside the model are done in the cell data
   * funcs (remember that there are two of those: one for the icon and
   * one for the text), while the filtering is done only when a filter
   * is actually loaded. */
  gtk_list_store_append (impl->recent_store, &iter);
  gtk_list_store_set (impl->recent_store, &iter,
  		      RECENT_URI_COLUMN, uri,           /* uri  */
  		      RECENT_DISPLAY_NAME_COLUMN, name, /* display_name */
  		      RECENT_INFO_COLUMN, info,         /* info */
  		      -1);
  
  impl->loaded_items += 1;

  if (impl->loaded_items == impl->n_recent_items)
    {
      /* we have finished loading, so we remove the items cache */
      impl->load_state = LOAD_LOADING;
      
      g_list_foreach (impl->recent_items,
      		      (GFunc) egg_recent_info_unref,
      		      NULL);
      g_list_free (impl->recent_items);
      
      impl->recent_items = NULL;
      impl->n_recent_items = 0;
      impl->loaded_items = 0;
      
      if (impl->recent_store_filter)
        {
          g_object_unref (impl->recent_store_filter);
	  impl->recent_store_filter = NULL;
	}
      
      /* load the filled up model */
      chooser_set_model (impl);

      retval = FALSE;
    }
  else
    {
      /* we did not finish, so continue loading */
      retval = TRUE;
    }
  
  GDK_THREADS_LEAVE ();
  
  return retval;
}

static void
cleanup_after_load (gpointer user_data)
{
  EggRecentChooserDefault *impl;
  
  GDK_THREADS_ENTER ();
  
  impl = EGG_RECENT_CHOOSER_DEFAULT (user_data);

  if (impl->load_id != 0)
    {
      g_assert ((impl->load_state == LOAD_PRELOAD) ||
		(impl->load_state == LOAD_LOADING) ||
		(impl->load_state == LOAD_FINISHED));
      
      /* we have officialy finished loading all the items,
       * so we can reset the state machine
       */
      g_source_remove (impl->load_id);
      impl->load_id = 0;
      impl->load_state = LOAD_EMPTY;
    }
  else
    g_assert ((impl->load_state == LOAD_EMPTY) ||
	      (impl->load_state == LOAD_LOADING) ||
	      (impl->load_state == LOAD_FINISHED));

  set_busy_cursor (impl, FALSE);
  
  GDK_THREADS_LEAVE ();
}

/* clears the current model and reloads the recently used resources */
static void
reload_recent_items (EggRecentChooserDefault *impl)
{
  gtk_tree_view_set_model (GTK_TREE_VIEW (impl->recent_view), NULL);
  gtk_list_store_clear (impl->recent_store);
  
  if (!impl->icon_theme)
    impl->icon_theme = get_icon_theme_for_widget (GTK_WIDGET (impl));

  impl->icon_size = get_icon_size_for_widget (GTK_WIDGET (impl));

  set_busy_cursor (impl, TRUE);

  impl->load_state = LOAD_EMPTY;
  impl->load_id = g_idle_add_full (G_PRIORITY_HIGH_IDLE + 30,
		      		   load_recent_items,
				   impl,
				   cleanup_after_load);
}

/* taken form gtkfilechooserdialog.c */
static void
set_default_size (EggRecentChooserDefault *impl)
{
  GtkWidget *widget;
  gint width, height;
  gint font_size;
  GdkScreen *screen;
  gint monitor_num;
  GtkRequisition req;
  GdkRectangle monitor;

  widget = GTK_WIDGET (impl);

  /* Size based on characters and the icon size */
  font_size = pango_font_description_get_size (widget->style->font_desc);
  font_size = PANGO_PIXELS (font_size);

  width = impl->icon_size + font_size * NUM_CHARS;
  height = (impl->icon_size + font_size) * NUM_LINES;

  /* Use at least the requisition size... */
  gtk_widget_size_request (widget, &req);
  width = MAX (width, req.width);
  height = MAX (height, req.height);

  /* ... but no larger than the monitor */
  screen = gtk_widget_get_screen (widget);
  monitor_num = gdk_screen_get_monitor_at_window (screen, widget->window);

  gdk_screen_get_monitor_geometry (screen, monitor_num, &monitor);

  width = MIN (width, monitor.width * 3 / 4);
  height = MIN (height, monitor.height * 3 / 4);

  /* Set size */
  gtk_widget_set_size_request (impl->recent_view, width, height);
}

static void
egg_recent_chooser_default_map (GtkWidget *widget)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (widget);
  
  if (GTK_WIDGET_CLASS (egg_recent_chooser_default_parent_class)->map)
    GTK_WIDGET_CLASS (egg_recent_chooser_default_parent_class)->map (widget);

  /* reloads everything */
  reload_recent_items (impl);

  set_default_size (impl);
}

static void
recent_icon_data_func (GtkTreeViewColumn *tree_column,
		       GtkCellRenderer   *cell,
		       GtkTreeModel      *model,
		       GtkTreeIter       *iter,
		       gpointer           user_data)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (user_data);
  EggRecentInfo *info = NULL;
  GdkPixbuf *pixbuf;
  
  gtk_tree_model_get (model, iter,
                      RECENT_INFO_COLUMN, &info,
                      -1);
  g_assert (info != NULL);
  
  pixbuf = egg_recent_info_get_icon (info, impl->icon_size);
  
  g_object_set (cell,
                "pixbuf", pixbuf,
                NULL);
  
  if (pixbuf)  
    g_object_unref (pixbuf);
}

static void
recent_meta_data_func (GtkTreeViewColumn *tree_column,
		       GtkCellRenderer   *cell,
		       GtkTreeModel      *model,
		       GtkTreeIter       *iter,
		       gpointer           user_data)
{
  EggRecentInfo *info = NULL;
  gchar *uri;
  gchar *name;
  GString *data;
  
  data = g_string_new (NULL);
  
  gtk_tree_model_get (model, iter,
                      RECENT_DISPLAY_NAME_COLUMN, &name,
                      RECENT_INFO_COLUMN, &info,
                      -1);
  g_assert (info != NULL);
  
  uri = egg_recent_info_get_uri_display (info);
  
  if (!name)
    name = egg_recent_info_get_short_name (info);
 
  g_string_append_printf (data,
  			  "<b>%s</b>\n"
  			  "<small>Location: %s</small>",
  			  name,
  			  uri);
  
  g_object_set (cell,
                "markup", data->str,
                "sensitive", egg_recent_info_exists (info),
                NULL);
  
  g_string_free (data, TRUE);
  g_free (uri);
  g_free (name);
  egg_recent_info_unref (info);
}


static gchar *
egg_recent_chooser_default_get_current_uri (EggRecentChooser *chooser)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (chooser);
  
  g_assert (impl->selection != NULL);
  
  if (!impl->select_multiple)
    {
      GtkTreeModel *model;
      GtkTreeIter iter;
      gchar *uri = NULL;
      
      if (!gtk_tree_selection_get_selected (impl->selection, &model, &iter))
        return NULL;
      
      gtk_tree_model_get (model, &iter, RECENT_URI_COLUMN, &uri, -1);
      
      return uri;
    }
  
  return NULL;
}

typedef struct
{
  guint found : 1;
  guint do_select : 1;
  guint do_activate : 1;
  
  gchar *uri;
  
  EggRecentChooserDefault *impl;
} SelectURIData;

static gboolean
scan_for_uri_cb (GtkTreeModel *model,
		 GtkTreePath  *path,
		 GtkTreeIter  *iter,
		 gpointer      user_data)
{
  SelectURIData *select_data = (SelectURIData *) user_data;
  gchar *uri;
  
  if (!select_data)
    return TRUE;
  
  if (select_data->found)
    return TRUE;
  
  gtk_tree_model_get (model, iter, RECENT_URI_COLUMN, &uri, -1);
  if (uri && (0 == strcmp (uri, select_data->uri)))
    {
      select_data->found = TRUE;
      
      if (select_data->do_activate)
        {
          gtk_tree_view_row_activated (GTK_TREE_VIEW (select_data->impl->recent_view),
        				       path,
        				       select_data->impl->meta_column);
          
          return TRUE;
        }
      
      if (select_data->do_select)
        gtk_tree_selection_select_iter (select_data->impl->selection, iter);
      else
        gtk_tree_selection_unselect_iter (select_data->impl->selection, iter);
      
      return TRUE;
    }
  
  return FALSE;
}

static gboolean
egg_recent_chooser_default_set_current_uri (EggRecentChooser  *chooser,
					    const gchar       *uri,
					    GError           **error)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (chooser);
  SelectURIData *data;
  
  data = g_new0 (SelectURIData, 1);
  data->uri = g_strdup (uri);
  data->impl = impl;
  data->found = FALSE;
  data->do_activate = TRUE;
  data->do_select = TRUE;
  
  gtk_tree_model_foreach (GTK_TREE_MODEL (impl->recent_store),
  			  scan_for_uri_cb,
  			  data);
  
  if (!data->found)
    {
      g_free (data->uri);
      g_free (data);
      
      g_set_error (error, EGG_RECENT_CHOOSER_ERROR,
      		   EGG_RECENT_CHOOSER_ERROR_NOT_FOUND,
      		   _("No item for URI '%s' found"),
      		   uri);
      return FALSE;
    }
  
  g_free (data->uri);
  g_free (data);

  return TRUE;
}

static gboolean
egg_recent_chooser_default_select_uri (EggRecentChooser  *chooser,
				       const gchar       *uri,
				       GError           **error)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (chooser);
  SelectURIData *data;
  
  data = g_new0 (SelectURIData, 1);
  data->uri = g_strdup (uri);
  data->impl = impl;
  data->found = FALSE;
  data->do_activate = FALSE;
  data->do_select = TRUE;
  
  gtk_tree_model_foreach (GTK_TREE_MODEL (impl->recent_store),
  			  scan_for_uri_cb,
  			  data);
  
  if (!data->found)
    {
      g_free (data->uri);
      g_free (data);
      
      g_set_error (error, EGG_RECENT_CHOOSER_ERROR,
      		   EGG_RECENT_CHOOSER_ERROR_NOT_FOUND,
      		   _("No item for URI '%s' found"),
      		   uri);
      return FALSE;
    }
  
  g_free (data->uri);
  g_free (data);

  return TRUE;
}

static void
egg_recent_chooser_default_unselect_uri (EggRecentChooser *chooser,
					 const gchar      *uri)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (chooser);
  SelectURIData *data;
  
  data = g_new0 (SelectURIData, 1);
  data->uri = g_strdup (uri);
  data->impl = impl;
  data->found = FALSE;
  data->do_activate = FALSE;
  data->do_select = FALSE;
  
  gtk_tree_model_foreach (GTK_TREE_MODEL (impl->recent_store),
  			  scan_for_uri_cb,
  			  data);
  
  g_free (data->uri);
  g_free (data);
}

static void
egg_recent_chooser_default_select_all (EggRecentChooser *chooser)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (chooser);
  
  if (!impl->select_multiple)
    return;
  
  gtk_tree_selection_select_all (impl->selection);
}

static void
egg_recent_chooser_default_unselect_all (EggRecentChooser *chooser)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (chooser);
  
  gtk_tree_selection_unselect_all (impl->selection);
}

static void
egg_recent_chooser_default_set_sort_func (EggRecentChooser  *chooser,
					  EggRecentSortFunc  sort_func,
					  gpointer           sort_data,
					  GDestroyNotify     data_destroy)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (chooser);
  
  if (impl->sort_data_destroy)
    {
      impl->sort_data_destroy (impl->sort_data);
      
      impl->sort_func = NULL;
      impl->sort_data = NULL;
      impl->sort_data_destroy = NULL;
    }
  
  if (sort_func)
    {
      impl->sort_func = sort_func;
      impl->sort_data = sort_data;
      impl->sort_data_destroy = data_destroy;
    }
}

static gint
sort_recent_items_mru (EggRecentInfo *a,
		       EggRecentInfo *b,
		       gpointer       unused)
{
  g_assert (a != NULL && b != NULL);
  
  return (egg_recent_info_get_modified (a) < egg_recent_info_get_modified (b));
}

static gint
sort_recent_items_lru (EggRecentInfo *a,
		       EggRecentInfo *b,
		       gpointer       unused)
{
  g_assert (a != NULL && b != NULL);
  
  return (egg_recent_info_get_modified (a) > egg_recent_info_get_modified (b));
}

/* our proxy sorting function */
static gint
sort_recent_items_proxy (gpointer *a,
			 gpointer *b,
			 gpointer  user_data)
{
  EggRecentInfo *info_a = (EggRecentInfo *) a;
  EggRecentInfo *info_b = (EggRecentInfo *) b;
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (user_data);

  if (impl->sort_func)
    return (* impl->sort_func) (info_a,
    				info_b,
      				impl->sort_data);
  
  /* fallback */
  return 0;
}

static void
chooser_set_sort_type (EggRecentChooserDefault *impl,
		       EggRecentSortType        sort_type)
{
  if (impl->sort_type == sort_type)
    return;

  impl->sort_type = sort_type;
}

static GList *
egg_recent_chooser_default_get_items (EggRecentChooser *chooser)
{
  EggRecentChooserDefault *impl;
  gint limit;
  EggRecentSortType sort_type;
  GList *items;
  GCompareDataFunc compare_func;
  gint length;
  
  impl = EGG_RECENT_CHOOSER_DEFAULT (chooser);
  
  if (!impl->manager)
    return NULL;
  
  limit = egg_recent_chooser_get_limit (chooser);
  sort_type = egg_recent_chooser_get_sort_type (chooser);

  switch (sort_type)
    {
    case EGG_RECENT_SORT_NONE:
      compare_func = NULL;
      break;
    case EGG_RECENT_SORT_MRU:
      compare_func = (GCompareDataFunc) sort_recent_items_mru;
      break;
    case EGG_RECENT_SORT_LRU:
      compare_func = (GCompareDataFunc) sort_recent_items_lru;
      break;
    case EGG_RECENT_SORT_CUSTOM:
      compare_func = (GCompareDataFunc) sort_recent_items_proxy;
      break;
    default:
      g_assert_not_reached ();
      break;
    }
  
  items = egg_recent_manager_get_items (impl->manager);
  if (!items)
    return NULL;
  
  /* sort the items; the filtering will be dealt with using
   * the treeview's own filter object
   */
  if (compare_func)
    items = g_list_sort_with_data (items, compare_func, impl);
  
  length = g_list_length (items);
  if ((limit != -1) && (length > limit))
    {
      GList *clamp, *l;
      
      clamp = g_list_nth (items, limit - 1);
      
      if (!clamp)
        return items;
      
      l = clamp->next;
      clamp->next = NULL;
    
      g_list_foreach (l, (GFunc) egg_recent_info_unref, NULL);
      g_list_free (l);
    }
  
  return items;
}

static EggRecentManager *
egg_recent_chooser_default_get_recent_manager (EggRecentChooser *chooser)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (chooser);
  
  return impl->manager;
}

static void
show_filters (EggRecentChooserDefault *impl,
              gboolean                 show)
{
  if (show)
    gtk_widget_show (impl->filter_combo_hbox);
  else
    gtk_widget_hide (impl->filter_combo_hbox);
}

static void
egg_recent_chooser_default_add_filter (EggRecentChooser *chooser,
				       EggRecentFilter  *filter)
{
  EggRecentChooserDefault *impl;
  const gchar *name;

  impl = EGG_RECENT_CHOOSER_DEFAULT (chooser);
  
  if (g_slist_find (impl->filters, filter))
    {
      g_warning ("egg_recent_chooser_add_filter() called on filter already in list\n");
      return;
    }
  
  g_object_ref (filter);
  gtk_object_sink (GTK_OBJECT (filter));
  impl->filters = g_slist_append (impl->filters, filter);
  
  /* display new filter */
  name = egg_recent_filter_get_name (filter);
  if (!name)
    name = "Untitled filter";
    
  gtk_combo_box_append_text (GTK_COMBO_BOX (impl->filter_combo), name);
  
  if (!g_slist_find (impl->filters, impl->current_filter))
    set_current_filter (impl, filter);
  
  show_filters (impl, TRUE);
}

static void
egg_recent_chooser_default_remove_filter (EggRecentChooser *chooser,
					  EggRecentFilter  *filter)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (chooser);
  GtkTreeModel *model;
  GtkTreeIter iter;
  gint filter_idx;
  
  filter_idx = g_slist_index (impl->filters, filter);
  
  if (filter_idx < 0)
    {
      g_warning ("egg_recent_chooser_remove_filter() called on filter not in list\n");
      return;  
    }
  
  impl->filters = g_slist_remove (impl->filters, filter);
  
  if (filter == impl->current_filter)
    {
      if (impl->filters)
        set_current_filter (impl, impl->filters->data);
      else
        set_current_filter (impl, NULL);
    }
  
  model = gtk_combo_box_get_model (GTK_COMBO_BOX (impl->filter_combo));
  gtk_tree_model_iter_nth_child (model, &iter, NULL, filter_idx);
  gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
  
  g_object_unref (filter);
  
  if (!impl->filters)
    show_filters (impl, FALSE);
}

static GSList *
egg_recent_chooser_default_list_filters (EggRecentChooser *chooser)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (chooser);
  
  return g_slist_copy (impl->filters);
}

static gboolean
get_is_recent_filtered (EggRecentChooserDefault *impl,
			EggRecentInfo           *info)
{
  EggRecentFilter *current_filter;
  EggRecentFilterInfo filter_info;
  EggRecentFilterFlags needed;
  gboolean retval;

  g_assert (info != NULL);
  
  if (!impl->current_filter)
    return FALSE;
  
  current_filter = impl->current_filter;
  needed = egg_recent_filter_get_needed (current_filter);
  
  filter_info.contains = EGG_RECENT_FILTER_URI | EGG_RECENT_FILTER_MIME_TYPE;
  
  filter_info.uri = egg_recent_info_get_uri (info);
  filter_info.mime_type = egg_recent_info_get_mime_type (info);
  
  if (needed & EGG_RECENT_FILTER_DISPLAY_NAME)
    {
      filter_info.display_name = egg_recent_info_get_display_name (info);
      filter_info.contains |= EGG_RECENT_FILTER_DISPLAY_NAME;
    }
  else
    filter_info.uri = NULL;
  
  if (needed & EGG_RECENT_FILTER_APPLICATION)
    {
      filter_info.applications = (const gchar **) egg_recent_info_get_applications (info, NULL);
      filter_info.contains |= EGG_RECENT_FILTER_APPLICATION;
    }
  else
    filter_info.applications = NULL;

  if (needed & EGG_RECENT_FILTER_GROUP)
    {
      filter_info.groups = (const gchar **) egg_recent_info_get_groups (info, NULL);
      filter_info.contains |= EGG_RECENT_FILTER_GROUP;
    }
  else
    filter_info.groups = NULL;

  if (needed & EGG_RECENT_FILTER_AGE)
    {
      filter_info.age = egg_recent_info_get_age (info);
      filter_info.contains |= EGG_RECENT_FILTER_AGE;
    }
  else
    filter_info.age = -1;
  
  retval = egg_recent_filter_filter (current_filter, &filter_info);
  
  /* this we own */
  if (filter_info.applications)
    g_strfreev ((gchar **) filter_info.applications);
  
  return !retval;
}

static gboolean
recent_store_filter_func (GtkTreeModel *model,
                          GtkTreeIter  *iter,
                          gpointer      user_data)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (user_data);
  EggRecentInfo *info = NULL;

  if (!impl->current_filter)
    return TRUE;
  
  gtk_tree_model_get (model, iter,
                      RECENT_INFO_COLUMN, &info,
                      -1);
  if (!info)
    return TRUE;
    
  if (get_is_recent_filtered (impl, info))
    return FALSE;
  
  if (impl->local_only && !egg_recent_info_is_local (info))
    return FALSE;
  
  if ((!impl->show_private) && egg_recent_info_get_private_hint (info))
    return FALSE;
  
  if ((!impl->show_not_found) && !egg_recent_info_exists (info))
    return FALSE;
      
  return TRUE;
}

static void
set_current_filter (EggRecentChooserDefault *impl,
		    EggRecentFilter         *filter)
{
  if (impl->current_filter != filter)
    {
      gint filter_idx;
      
      filter_idx = g_slist_index (impl->filters, filter);
      if (impl->filters && filter && filter_idx < 0)
        return;
      
      if (impl->current_filter)
        g_object_unref (impl->current_filter);
      
      impl->current_filter = filter;
      
      if (impl->current_filter)     
        {
          g_object_ref (impl->current_filter);
          gtk_object_sink (GTK_OBJECT (impl->current_filter));
        }
      
      if (impl->filters)
        gtk_combo_box_set_active (GTK_COMBO_BOX (impl->filter_combo),
                                  filter_idx);
      
      if (impl->recent_store && impl->recent_store_filter)
        {
          gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (impl->recent_store_filter));
        }
      
      g_object_notify (G_OBJECT (impl), "filter");
    }
}

static GtkIconTheme *
get_icon_theme_for_widget (GtkWidget *widget)
{
  if (gtk_widget_has_screen (widget))
    return gtk_icon_theme_get_for_screen (gtk_widget_get_screen (widget));

  return gtk_icon_theme_get_default ();
}

static gint
get_icon_size_for_widget (GtkWidget *widget)
{
  GtkSettings *settings;
  gint width, height;

  if (gtk_widget_has_screen (widget))
    settings = gtk_settings_get_for_screen (gtk_widget_get_screen (widget));
  else
    settings = gtk_settings_get_default ();

  if (gtk_icon_size_lookup_for_settings (settings, GTK_ICON_SIZE_MENU,
                                         &width, &height))
    return MAX (width, height);

  return FALLBACK_ICON_SIZE;
}


static void
recent_manager_changed_cb (EggRecentManager *manager,
			   gpointer          user_data)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (user_data);

  reload_recent_items (impl);
}

static void
selection_changed_cb (GtkTreeSelection *selection,
		      gpointer          user_data)
{
  g_signal_emit_by_name (user_data, "selection-changed");
}

static void
row_activated_cb (GtkTreeView       *tree_view,
		  GtkTreePath       *tree_path,
		  GtkTreeViewColumn *tree_column,
		  gpointer           user_data)
{
  g_signal_emit_by_name (user_data, "item-activated");
}

static void
filter_combo_changed_cb (GtkComboBox *combo_box,
			 gpointer     user_data)
{
  EggRecentChooserDefault *impl;
  gint new_index;
  EggRecentFilter *filter;
  
  impl = EGG_RECENT_CHOOSER_DEFAULT (user_data);
  
  new_index = gtk_combo_box_get_active (combo_box);
  filter = g_slist_nth_data (impl->filters, new_index);
  
  set_current_filter (impl, filter);
}

static void
append_uri_to_urilist (GtkTreeModel *model,
		       GtkTreePath  *path,
		       GtkTreeIter  *iter,
		       gpointer      user_data)
{
  GString **uri_list = (GString **) user_data;
  GtkTreeModel *child_model;
  GtkTreeIter child_iter;
  gchar *uri = NULL;
  
  child_model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (model));
  gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (model),
  						    &child_iter,
  						    iter);
  gtk_tree_model_get (child_model, &child_iter,
  		      RECENT_URI_COLUMN, &uri,
  		      -1);
  g_assert (uri != NULL);
  
  if (*uri_list == NULL)
    *uri_list = g_string_sized_new (strlen (uri) + 3);
    
  g_string_append_printf (*uri_list, "%s\r\n", uri);
}

static void
recent_view_drag_data_get_cb (GtkWidget        *widget,
			      GdkDragContext   *context,
			      GtkSelectionData *selection_data,
			      guint             info,
			      guint32           time_,
			      gpointer          data)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (data);
  GString *uri_list = NULL;
      
  gtk_tree_selection_selected_foreach (impl->selection,
      				       append_uri_to_urilist,
      				       &uri_list);
      
  if (!uri_list)
    return;
      
  gtk_selection_data_set (selection_data,
      			   selection_data->target,
      			  8,
      			  (guchar *) uri_list->str,
      			  uri_list->len);
      
  g_string_free (uri_list, TRUE);
}



static void
remove_selected_from_list (EggRecentChooserDefault *impl)
{
  gchar *uri;
  GError *err;
  
  if (impl->select_multiple)
    return;
  
  uri = egg_recent_chooser_get_current_uri (EGG_RECENT_CHOOSER (impl));
  if (!uri)
    return;
  
  err = NULL;
  if (!egg_recent_manager_remove_item (impl->manager, uri, &err))
    {
      gchar *msg;
   
      msg = strdup (_("Could not remove item"));
      error_message (impl, msg, err->message);
      
      g_free (msg);
      g_error_free (err);
    }
  
  g_free (uri);
}

static void
copy_activated_cb (GtkMenuItem *menu_item,
		   gpointer     user_data)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (user_data);
  EggRecentInfo *info;
  gchar *utf8_uri;

  info = egg_recent_chooser_get_current_item (EGG_RECENT_CHOOSER (impl));
  if (!info)
    return;

  utf8_uri = egg_recent_info_get_uri_display (info);
  
  gtk_clipboard_set_text (gtk_widget_get_clipboard (GTK_WIDGET (impl),
			  			    GDK_SELECTION_CLIPBOARD),
                          utf8_uri, -1);

  g_free (utf8_uri);
}

static void
remove_all_activated_cb (GtkMenuItem *menu_item,
			 gpointer     user_data)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (user_data);
  GError *err = NULL;
  
  egg_recent_manager_purge_items (impl->manager, &err);
  if (err)
    {
       gchar *msg;

       msg = g_strdup (_("Could not clear list"));

       error_message (impl, msg, err->message);
       
       g_free (msg);
       g_error_free (err);
    }
}

static void
remove_item_activated_cb (GtkMenuItem *menu_item,
			  gpointer     user_data)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (user_data);
  
  remove_selected_from_list (impl);
}

static void
show_private_toggled_cb (GtkCheckMenuItem *menu_item,
			 gpointer          user_data)
{
  EggRecentChooserDefault *impl = EGG_RECENT_CHOOSER_DEFAULT (user_data);
  
  g_object_set (G_OBJECT (impl),
  		"show-private", gtk_check_menu_item_get_active (menu_item),
  		NULL);
}

static void
recent_popup_menu_detach_cb (GtkWidget *attach_widget,
			     GtkMenu   *menu)
{
  EggRecentChooserDefault *impl;
  
  impl = g_object_get_data (G_OBJECT (attach_widget), "EggRecentChooserDefault");
  g_assert (EGG_IS_RECENT_CHOOSER_DEFAULT (impl));
  
  impl->recent_popup_menu = NULL;
  impl->recent_popup_menu_remove_item = NULL;
  impl->recent_popup_menu_copy_item = NULL;
  impl->recent_popup_menu_clear_item = NULL;
  impl->recent_popup_menu_show_private_item = NULL;
}

static void
recent_view_menu_build (EggRecentChooserDefault *impl)
{
  GtkWidget *item;
  
  if (impl->recent_popup_menu)
    return;
  
  impl->recent_popup_menu = gtk_menu_new ();
  gtk_menu_attach_to_widget (GTK_MENU (impl->recent_popup_menu),
  			     impl->recent_view,
  			     recent_popup_menu_detach_cb);
  
  item = gtk_image_menu_item_new_with_mnemonic (_("Copy _Location"));
  impl->recent_popup_menu_copy_item = item;
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
		  		 gtk_image_new_from_stock (GTK_STOCK_COPY, GTK_ICON_SIZE_MENU));
  g_signal_connect (item, "activate",
		    G_CALLBACK (copy_activated_cb), impl);
  gtk_widget_show (item);
  gtk_menu_shell_append (GTK_MENU_SHELL (impl->recent_popup_menu), item);

  item = gtk_separator_menu_item_new ();
  gtk_widget_show (item);
  gtk_menu_shell_append (GTK_MENU_SHELL (impl->recent_popup_menu), item);
  
  item = gtk_image_menu_item_new_with_mnemonic (_("_Remove From List"));
  impl->recent_popup_menu_remove_item = item;
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
  				 gtk_image_new_from_stock (GTK_STOCK_REMOVE, GTK_ICON_SIZE_MENU));
  g_signal_connect (item, "activate",
  		    G_CALLBACK (remove_item_activated_cb), impl);
  gtk_widget_show (item);
  gtk_menu_shell_append (GTK_MENU_SHELL (impl->recent_popup_menu), item);

  item = gtk_image_menu_item_new_with_mnemonic (_("_Clear List"));
  impl->recent_popup_menu_clear_item = item;
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
  				 gtk_image_new_from_stock (GTK_STOCK_CLEAR, GTK_ICON_SIZE_MENU));
  g_signal_connect (item, "activate",
  		    G_CALLBACK (remove_all_activated_cb), impl);
  gtk_widget_show (item);
  gtk_menu_shell_append (GTK_MENU_SHELL (impl->recent_popup_menu), item);
  
  item = gtk_separator_menu_item_new ();
  gtk_widget_show (item);
  gtk_menu_shell_append (GTK_MENU_SHELL (impl->recent_popup_menu), item);
  
  item = gtk_check_menu_item_new_with_mnemonic (_("Show _Private Resources"));
  impl->recent_popup_menu_show_private_item = item;
  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), impl->show_private);
  g_signal_connect (item, "toggled",
  		    G_CALLBACK (show_private_toggled_cb), impl);
  gtk_widget_show (item);
  gtk_menu_shell_append (GTK_MENU_SHELL (impl->recent_popup_menu), item);
}

/* taken from gtkfilechooserdefault.c */
static void
popup_position_func (GtkMenu   *menu,
                     gint      *x,
                     gint      *y,
                     gboolean  *push_in,
                     gpointer	user_data)
{
  GtkWidget *widget = GTK_WIDGET (user_data);
  GdkScreen *screen = gtk_widget_get_screen (widget);
  GtkRequisition req;
  gint monitor_num;
  GdkRectangle monitor;

  if (G_UNLIKELY (!GTK_WIDGET_REALIZED (widget)))
    return;

  gdk_window_get_origin (widget->window, x, y);

  gtk_widget_size_request (GTK_WIDGET (menu), &req);

  *x += (widget->allocation.width - req.width) / 2;
  *y += (widget->allocation.height - req.height) / 2;

  monitor_num = gdk_screen_get_monitor_at_point (screen, *x, *y);
  gtk_menu_set_monitor (menu, monitor_num);
  gdk_screen_get_monitor_geometry (screen, monitor_num, &monitor);

  *x = CLAMP (*x, monitor.x, monitor.x + MAX (0, monitor.width - req.width));
  *y = CLAMP (*y, monitor.y, monitor.y + MAX (0, monitor.height - req.height));

  *push_in = FALSE;
}


static void
recent_view_menu_popup (EggRecentChooserDefault *impl,
			GdkEventButton          *event)
{
  recent_view_menu_build (impl);
  
  if (event)
    gtk_menu_popup (GTK_MENU (impl->recent_popup_menu),
    		    NULL, NULL, NULL, NULL,
    		    event->button, event->time);
  else
    {
      gtk_menu_popup (GTK_MENU (impl->recent_popup_menu),
      		      NULL, NULL,
      		      popup_position_func, impl->recent_view,
      		      0, GDK_CURRENT_TIME);
      gtk_menu_shell_select_first (GTK_MENU_SHELL (impl->recent_popup_menu),
      				   FALSE);
    }
}

static gboolean
recent_view_popup_menu_cb (GtkWidget *widget,
			   gpointer   user_data)
{
  recent_view_menu_popup (EGG_RECENT_CHOOSER_DEFAULT (user_data), NULL);
  return TRUE;
}

static gboolean
recent_view_button_press_cb (GtkWidget      *widget,
			     GdkEventButton *event,
			     gpointer        user_data)
{
  if (event->button != 3)
    return FALSE;
  
  recent_view_menu_popup (EGG_RECENT_CHOOSER_DEFAULT (user_data), event);
  return TRUE;
}

static void
set_recent_manager (EggRecentChooserDefault *impl,
		    EggRecentManager        *manager)
{
  if (impl->manager)
    {
      g_signal_handler_disconnect (impl, impl->manager_changed_id);
      impl->manager_changed_id = 0;

      g_object_unref (impl->manager);
      impl->manager = NULL;
    }
  
  if (manager)
    {
      impl->manager = manager;
      g_object_ref (impl->manager);
    }
#if 0
  else
    {
      /* TODO - this code should enable the usage of a singleton RecentManager
       * per session, along with an appropriate API in EggRecentManager like:
       *
       *   egg_recent_manager_get_default ();
       *
       * Anyway, this should really be decided when RecentManager goes into
       * the Gtk+ library
       */       
      GtkSettings *settings = gtk_settings_get_default ();
      EggRecentManager *default_manager = NULL;
      
      g_object_get (settings, "egg-recent-manager-default", &default_manager, NULL);
      
      if (default_manager)
        impl->manager = default_manager;
    }
#endif
  
  /* create a new recent manager object */
  if (!impl->manager)
    impl->manager = egg_recent_manager_new ();
  
  if (impl->manager)
    impl->manager_changed_id = g_signal_connect (impl->manager, "changed",
      						 G_CALLBACK (recent_manager_changed_cb),
      						 impl);
}

GtkWidget *
_egg_recent_chooser_default_new (EggRecentManager *manager)
{
  return g_object_new (EGG_TYPE_RECENT_CHOOSER_DEFAULT,
  		       "recent-manager", manager,
  		       NULL);
}