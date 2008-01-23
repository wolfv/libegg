#include "eggtoolpalette.h"
#include "eggtoolitemgroup.h"
#include "eggenumaction.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <string.h>

typedef struct _CanvasItem CanvasItem;

struct _CanvasItem
{
  GdkPixbuf *pixbuf;
  gdouble x, y;
};

static CanvasItem *drop_item = NULL;
static GList *canvas_items = NULL;

/********************************/
/* ====== Canvas drawing ====== */
/********************************/

static CanvasItem*
canvas_item_new (GtkWidget     *widget,
                 GtkToolButton *button,
                 gdouble        x,
                 gdouble        y)
{
  CanvasItem *item = NULL;
  const gchar *stock_id;
  GdkPixbuf *pixbuf;

  stock_id = gtk_tool_button_get_stock_id (button);
  pixbuf = gtk_widget_render_icon (widget, stock_id, GTK_ICON_SIZE_DIALOG, NULL);

  if (pixbuf)
    {
      item = g_slice_new0 (CanvasItem);
      item->pixbuf = pixbuf;
      item->x = x;
      item->y = y;
    }

  return item;
}

static void
canvas_item_free (CanvasItem *item)
{
  g_object_unref (item->pixbuf);
  g_slice_free (CanvasItem, item);
}

static void
canvas_item_draw (const CanvasItem *item,
                  cairo_t          *cr,
                  gboolean          preview)
{
  gdouble cx = gdk_pixbuf_get_width (item->pixbuf);
  gdouble cy = gdk_pixbuf_get_height (item->pixbuf);

  gdk_cairo_set_source_pixbuf (cr,
                               item->pixbuf,
                               item->x - cx * 0.5,
                               item->y - cy * 0.5);

  if (preview)
    cairo_paint_with_alpha (cr, 0.6);
  else
    cairo_paint (cr);
}

static gboolean
canvas_expose_event (GtkWidget      *widget,
                     GdkEventExpose *event)
{
  cairo_t *cr;
  GList *iter;

  cr = gdk_cairo_create (widget->window);
  gdk_cairo_region (cr, event->region);
  cairo_clip (cr);

  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_rectangle (cr, 0, 0, widget->allocation.width, widget->allocation.height);
  cairo_fill (cr);

  for (iter = canvas_items; iter; iter = iter->next)
    canvas_item_draw (iter->data, cr, FALSE);

  if (drop_item)
    canvas_item_draw (drop_item, cr, TRUE);

  cairo_destroy (cr);

  return TRUE;
}

/********************************/
/* ====== Passive Canvas ====== */
/********************************/

static void
passive_canvas_drag_data_received (GtkWidget        *widget,
                                   GdkDragContext   *context,
                                   gint              x,
                                   gint              y,
                                   GtkSelectionData *selection,
                                   guint             info G_GNUC_UNUSED,
                                   guint             time G_GNUC_UNUSED,
                                   gpointer          data G_GNUC_UNUSED)
{
  /* find the tool button, which is the source of this DnD operation */

  GtkWidget *palette = gtk_drag_get_source_widget (context);
  CanvasItem *canvas_item = NULL;
  GtkToolItem *tool_item = NULL;

  while (palette && !EGG_IS_TOOL_PALETTE (palette))
    palette = gtk_widget_get_parent (palette);

  if (palette)
    tool_item = egg_tool_palette_get_drag_item (EGG_TOOL_PALETTE (palette), selection);

  g_assert (NULL == drop_item);

  /* append a new canvas item when a tool button was found */

  if (GTK_IS_TOOL_ITEM (tool_item))
    canvas_item = canvas_item_new (widget, GTK_TOOL_BUTTON (tool_item), x, y);

  if (canvas_item)
    {
      canvas_items = g_list_append (canvas_items, canvas_item);
      gtk_widget_queue_draw (widget);
    }
}

/************************************/
/* ====== Interactive Canvas ====== */
/************************************/

static gboolean
interactive_canvas_drag_motion (GtkWidget        *widget,
                                GdkDragContext   *context,
                                gint              x,
                                gint              y,
                                guint             time,
                                gpointer          data G_GNUC_UNUSED)
{
  if (drop_item)
    {
      /* already have a drop indicator - just update position */

      drop_item->x = x;
      drop_item->y = y;

      gtk_widget_queue_draw (widget);
      gdk_drag_status (context, GDK_ACTION_COPY, time);
    }
  else
    {
      /* request DnD data for creating a drop indicator */

      GdkAtom target = gtk_drag_dest_find_target (widget, context, NULL);

      if (!target)
        return FALSE;

      gtk_drag_get_data (widget, context, target, time);
    }

  return TRUE;
}

static void
interactive_canvas_drag_data_received (GtkWidget        *widget,
                                       GdkDragContext   *context,
                                       gint              x,
                                       gint              y,
                                       GtkSelectionData *selection,
                                       guint             info G_GNUC_UNUSED,
                                       guint             time G_GNUC_UNUSED,
                                       gpointer          data G_GNUC_UNUSED)

{
  /* find the tool button, which is the source of this DnD operation */

  GtkWidget *palette = gtk_drag_get_source_widget (context);
  GtkToolItem *tool_item = NULL;

  while (palette && !EGG_IS_TOOL_PALETTE (palette))
    palette = gtk_widget_get_parent (palette);

  if (palette)
    tool_item = egg_tool_palette_get_drag_item (EGG_TOOL_PALETTE (palette), selection);

  /* create a drop indicator when a tool button was found */

  g_assert (NULL == drop_item);

  if (GTK_IS_TOOL_ITEM (tool_item))
    {
      drop_item = canvas_item_new (widget, GTK_TOOL_BUTTON (tool_item), x, y);
      gdk_drag_status (context, GDK_ACTION_COPY, time);
      gtk_widget_queue_draw (widget);
    }
}

static gboolean
interactive_canvas_drag_drop (GtkWidget        *widget,
                              GdkDragContext   *context G_GNUC_UNUSED,
                              gint              x,
                              gint              y,
                              guint             time,
                              gpointer          data    G_GNUC_UNUSED)
{
  if (drop_item)
    {
      /* turn the drop indicator into a real canvas item */

      drop_item->x = x;
      drop_item->y = y;

      canvas_items = g_list_append (canvas_items, drop_item);
      drop_item = NULL;

      /* signal the item was accepted and redraw */

      gtk_drag_finish (context, TRUE, FALSE, time);
      gtk_widget_queue_draw (widget);

      return TRUE;
    }

  return FALSE;
}

static gboolean
interactive_canvas_real_drag_leave (gpointer data)
{
  if (drop_item)
    {
      GtkWidget *widget = GTK_WIDGET (data);

      canvas_item_free (drop_item);
      drop_item = NULL;

      gtk_widget_queue_draw (widget);
    }

  return FALSE;
}

static void
interactive_canvas_drag_leave (GtkWidget        *widget,
                               GdkDragContext   *context G_GNUC_UNUSED,
                               guint             time    G_GNUC_UNUSED,
                               gpointer          data    G_GNUC_UNUSED)
{
  /* defer cleanup until a potential "drag-drop" signal was received */
  g_idle_add (interactive_canvas_real_drag_leave, widget);
}

/*******************************/
/* ====== Setup Test UI ====== */
/*******************************/

static void
not_implemented (GtkAction *action,
                 GtkWindow *parent)
{
  GtkWidget *dialog = gtk_message_dialog_new (parent,
                                              GTK_DIALOG_MODAL |
                                              GTK_DIALOG_DESTROY_WITH_PARENT,
                                              GTK_MESSAGE_INFO,
                                              GTK_BUTTONS_CLOSE,
                                              _("Not implemented yet."));
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("Sorry, the '%s' action is not implemented."),
                                            gtk_action_get_name (action));

  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

static void
load_stock_items (EggToolPalette *palette)
{
  GtkWidget *group_af = egg_tool_item_group_new (_("Stock Icons (A-F)"));
  GtkWidget *group_gn = egg_tool_item_group_new (_("Stock Icons (G-N)"));
  GtkWidget *group_or = egg_tool_item_group_new (_("Stock Icons (O-R)"));
  GtkWidget *group_sz = egg_tool_item_group_new (_("Stock Icons (S-Z)"));
  GtkWidget *group = NULL;

  GtkToolItem *item;
  GSList *stock_ids;
  GSList *iter;

  stock_ids = gtk_stock_list_ids ();
  stock_ids = g_slist_sort (stock_ids, (GCompareFunc) strcmp);

  gtk_container_add (GTK_CONTAINER (palette), group_af);
  gtk_container_add (GTK_CONTAINER (palette), group_gn);
  gtk_container_add (GTK_CONTAINER (palette), group_or);
  gtk_container_add (GTK_CONTAINER (palette), group_sz);

  for (iter = stock_ids; iter; iter = g_slist_next (iter))
    {
      GtkStockItem stock_item;
      gchar *id = iter->data;

      switch (id[4])
        {
          case 'a':
            group = group_af;
            break;

          case 'g':
            group = group_gn;
            break;

          case 'o':
            group = group_or;
            break;

          case 's':
            group = group_sz;
            break;
        }

      item = gtk_tool_button_new_from_stock (id);
      gtk_tool_item_set_tooltip_text (GTK_TOOL_ITEM (item), id);
      egg_tool_item_group_insert (EGG_TOOL_ITEM_GROUP (group), item, -1);

      if (!gtk_stock_lookup (id, &stock_item) || !stock_item.label)
        gtk_tool_button_set_label (GTK_TOOL_BUTTON (item), id);

      g_free (id);
    }

  g_slist_free (stock_ids);
}

static void
load_special_items (EggToolPalette *palette)
{
  GtkToolItem *item;
  GtkWidget *group;

  group = egg_tool_item_group_new (_("Advanced Features"));
  gtk_container_add (GTK_CONTAINER (palette), group);

  item = gtk_tool_button_new_from_stock (GTK_STOCK_GO_UP);
  gtk_tool_button_set_label (GTK_TOOL_BUTTON (item),
                             "Show on vertical palettes only");
  gtk_tool_item_set_visible_horizontal (item, FALSE);
  egg_tool_item_group_insert (EGG_TOOL_ITEM_GROUP (group), item, -1);

  item = gtk_tool_button_new_from_stock (GTK_STOCK_GO_FORWARD);
  gtk_tool_button_set_label (GTK_TOOL_BUTTON (item),
                             "Show on horizontal palettes only");
  gtk_tool_item_set_visible_vertical (item, FALSE);
  egg_tool_item_group_insert (EGG_TOOL_ITEM_GROUP (group), item, -1);

  item = gtk_tool_button_new_from_stock (GTK_STOCK_DELETE);
  gtk_tool_button_set_label (GTK_TOOL_BUTTON (item),
                             "Do not show at all");
  gtk_widget_set_no_show_all (GTK_WIDGET (item), TRUE);
  egg_tool_item_group_insert (EGG_TOOL_ITEM_GROUP (group), item, -1);

  item = gtk_tool_button_new_from_stock (GTK_STOCK_FULLSCREEN);
  gtk_tool_button_set_label (GTK_TOOL_BUTTON (item),
                             "Expanded this item");
  gtk_tool_item_set_expand (item, TRUE);
  egg_tool_item_group_insert (EGG_TOOL_ITEM_GROUP (group), item, -1);

  item = gtk_tool_button_new_from_stock (GTK_STOCK_HELP);
  gtk_tool_button_set_label (GTK_TOOL_BUTTON (item),
                             "A regular item");
  egg_tool_item_group_insert (EGG_TOOL_ITEM_GROUP (group), item, -1);
}

static gboolean
drop_invalid_icon_size (GEnumValue *enum_value,
                        gpointer    user_data G_GNUC_UNUSED)
{
  return (enum_value->value != GTK_ICON_SIZE_INVALID);
}

static GtkWidget*
create_ui (void)
{
  static const gchar ui_spec[] = "              \
    <ui>                                        \
      <menubar>                                 \
        <menu action='FileMenu'>                \
          <menuitem action='FileNew' />         \
          <menuitem action='FileOpen' />        \
          <separator />                         \
          <menuitem action='FileSave' />        \
          <menuitem action='FileSaveAs' />      \
          <separator />                         \
          <menuitem action='FileClose' />       \
          <menuitem action='FileQuit' />        \
        </menu>                                 \
                                                \
        <menu action='ViewMenu'>                \
          <menuitem action='ViewIconSize' />    \
          <menuitem action='ViewOrientation' /> \
          <menuitem action='ViewStyle' />       \
        </menu>                                 \
                                                \
        <menu action='HelpMenu'>                \
          <menuitem action='HelpAbout' />       \
        </menu>                                 \
      </menubar>                                \
                                                \
      <toolbar>                                 \
        <toolitem action='FileNew' />           \
        <toolitem action='FileOpen' />          \
        <toolitem action='FileSave' />          \
        <separator />                           \
        <toolitem action='ViewIconSize' />      \
        <toolitem action='ViewOrientation' />   \
        <toolitem action='ViewStyle' />         \
        <separator />                           \
        <separator />                           \
        <toolitem action='HelpAbout' />         \
      </toolbar>                                \
    </ui>";

  static GtkActionEntry actions[] = {
    { "FileMenu",   NULL, N_("_File"),       NULL, NULL, NULL },
    { "FileNew",    GTK_STOCK_NEW, NULL,     NULL, NULL, G_CALLBACK (not_implemented) },
    { "FileOpen",   GTK_STOCK_OPEN, NULL,    NULL, NULL, G_CALLBACK (not_implemented) },
    { "FileSave",   GTK_STOCK_SAVE, NULL,    NULL, NULL, G_CALLBACK (not_implemented) },
    { "FileSaveAs", GTK_STOCK_SAVE_AS, NULL, NULL, NULL, G_CALLBACK (not_implemented) },
    { "FileClose",  GTK_STOCK_CLOSE, NULL,   NULL, NULL, G_CALLBACK (not_implemented) },
    { "FileQuit",   GTK_STOCK_QUIT, NULL,    NULL, NULL, G_CALLBACK (gtk_main_quit) },
    { "ViewMenu",   NULL, N_("_View"),       NULL, NULL, NULL },
    { "HelpMenu",   NULL, N_("_Help"),       NULL, NULL, NULL },
    { "HelpAbout",  GTK_STOCK_ABOUT, NULL,   NULL, NULL, G_CALLBACK (not_implemented) },
  };

  GtkActionGroup *group;
  GError *error = NULL;
  GtkUIManager *ui;

  GtkWidget *window, *vbox, *notebook;
  GtkWidget *menubar, *toolbar, *hpaned;
  GtkWidget *palette, *palette_scroller;
  GtkWidget *contents, *contents_scroller;
  GtkAction *action;

  /* ===== menubar/toolbar ===== */

  palette = egg_tool_palette_new ();
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  group = gtk_action_group_new ("");
  ui = gtk_ui_manager_new ();

  gtk_action_group_add_actions (group, actions, G_N_ELEMENTS (actions), window);

  action = egg_enum_action_new ("ViewIconSize", _("Icon Size"), NULL, GTK_TYPE_ICON_SIZE);
  egg_enum_action_set_filter (EGG_ENUM_ACTION (action), drop_invalid_icon_size, NULL, NULL);
  egg_enum_action_bind (EGG_ENUM_ACTION (action), G_OBJECT (palette), "icon-size");
  gtk_action_group_add_action (group, action);

  action = egg_enum_action_new ("ViewOrientation", _("Orientation"), NULL, GTK_TYPE_ORIENTATION);
  egg_enum_action_bind (EGG_ENUM_ACTION (action), G_OBJECT (palette), "orientation");
  gtk_action_group_add_action (group, action);

  action = egg_enum_action_new ("ViewStyle", _("Style"), NULL, GTK_TYPE_TOOLBAR_STYLE);
  egg_enum_action_bind (EGG_ENUM_ACTION (action), G_OBJECT (palette), "toolbar-style");
  gtk_action_group_add_action (group, action);

  gtk_ui_manager_insert_action_group (ui, group, -1);

  if (!gtk_ui_manager_add_ui_from_string (ui, ui_spec, sizeof ui_spec - 1, &error))
    {
      g_message ("building ui_spec failed: %s", error->message);
      g_clear_error (&error);
    }

  menubar = gtk_ui_manager_get_widget (ui, "/menubar");
  toolbar = gtk_ui_manager_get_widget (ui, "/toolbar");

  /* ===== palette ===== */

  load_stock_items (EGG_TOOL_PALETTE (palette));
  load_special_items (EGG_TOOL_PALETTE (palette));

  palette_scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (palette_scroller),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_container_set_border_width (GTK_CONTAINER (palette_scroller), 6);
  gtk_container_add (GTK_CONTAINER (palette_scroller), palette);

  /* ===== notebook ===== */

  notebook = gtk_notebook_new ();
  gtk_container_set_border_width (GTK_CONTAINER (notebook), 6);

  /* ===== passive DnD dest ===== */

  contents = gtk_drawing_area_new ();
  gtk_widget_set_app_paintable (contents, TRUE);

  g_object_connect (contents,
                    "signal::expose-event",       canvas_expose_event, NULL,
                    "signal::drag-data-received", passive_canvas_drag_data_received, NULL,
                    NULL);

  egg_tool_palette_add_drag_dest (EGG_TOOL_PALETTE (palette),
                                  contents, GTK_DEST_DEFAULT_ALL);

  contents_scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (contents_scroller),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (contents_scroller), contents);
  gtk_container_set_border_width (GTK_CONTAINER (contents_scroller), 6);

  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), contents_scroller,
                            gtk_label_new ("Passive DnD Mode"));

  /* ===== interactive DnD dest ===== */

  contents = gtk_drawing_area_new ();
  gtk_widget_set_app_paintable (contents, TRUE);

  g_object_connect (contents,
                    "signal::expose-event",       canvas_expose_event, NULL,
                    "signal::drag-motion",        interactive_canvas_drag_motion, NULL,
                    "signal::drag-data-received", interactive_canvas_drag_data_received, NULL,
                    "signal::drag-leave",         interactive_canvas_drag_leave, NULL,
                    "signal::drag-drop",          interactive_canvas_drag_drop, NULL,
                    NULL);

  egg_tool_palette_add_drag_dest (EGG_TOOL_PALETTE (palette),
                                  contents, GTK_DEST_DEFAULT_HIGHLIGHT);

  contents_scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (contents_scroller),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (contents_scroller), contents);
  gtk_container_set_border_width (GTK_CONTAINER (contents_scroller), 6);

  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), contents_scroller,
                            gtk_label_new ("Interactive DnD Mode"));

  /* ===== hpaned ===== */

  hpaned = gtk_hpaned_new ();
  gtk_paned_pack1 (GTK_PANED (hpaned), palette_scroller, FALSE, FALSE);
  gtk_paned_pack2 (GTK_PANED (hpaned), notebook, TRUE, FALSE);

  /* ===== vbox ===== */

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), menubar, FALSE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), toolbar, FALSE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hpaned, TRUE, TRUE, 0);
  gtk_widget_show_all (vbox);

  /* ===== window ===== */

  gtk_container_add (GTK_CONTAINER (window), vbox);
  gtk_window_set_default_size (GTK_WINDOW (window), 600, 500);
  gtk_window_add_accel_group (GTK_WINDOW (window), gtk_ui_manager_get_accel_group (ui));
  g_signal_connect (window, "destroy", G_CALLBACK (gtk_main_quit), NULL);

  g_object_unref (ui);
  return window;
}

int
main (int   argc,
      char *argv[])
{
  GtkWidget *ui;

  gtk_init (&argc, &argv);

  gtk_rc_parse_string ("                                \
    style 'egg-tool-item-group' {                       \
      EggToolItemGroup::expander-size = 10              \
    }                                                   \
                                                        \
    style 'egg-tool-item-group-header' {                \
      bg[NORMAL] = @selected_bg_color                   \
      fg[NORMAL] = @selected_fg_color                   \
      bg[PRELIGHT] = shade(1.04, @selected_bg_color)    \
      fg[PRELIGHT] = @selected_fg_color                 \
      bg[ACTIVE] = shade(0.9, @selected_bg_color)       \
      fg[ACTIVE] = shade(0.9, @selected_fg_color)       \
                                                        \
      font_name = 'Sans Serif Bold 10.'                 \
      GtkButton::inner_border = { 0, 3, 0, 0 }          \
    }                                                   \
                                                        \
    style 'egg-tool-item-group-button' {                \
      GtkToolButton::icon-spacing = 12                  \
    }                                                   \
                                                        \
    class 'EggToolItemGroup'                            \
    style 'egg-tool-item-group'                         \
                                                        \
    widget_class '*<EggToolItemGroup>.GtkButton*'       \
    style 'egg-tool-item-group-header'                  \
                                                        \
    widget_class '*<EggToolItemGroup>.GtkToolButton'    \
    style 'egg-tool-item-group-button'                  \
    ");

  ui = create_ui ();
  gtk_widget_show (ui);
  gtk_main ();

  return 0;
}