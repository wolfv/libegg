/* EggToolPalette -- A tool palette with categories and DnD support
 * Copyright (C) 2008  Openismus GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *      Mathias Hasselmann
 */

#include "eggtoolpalette.h"
#include "eggtoolpaletteprivate.h"
#include "eggtoolitemgroup.h"
#include "eggmarshalers.h"

#include <gtk/gtk.h>
#include <string.h>

#define DEFAULT_ICON_SIZE       GTK_ICON_SIZE_SMALL_TOOLBAR
#define DEFAULT_ORIENTATION     GTK_ORIENTATION_VERTICAL
#define DEFAULT_TOOLBAR_STYLE   GTK_TOOLBAR_ICONS

#define P_(msgid) (msgid)

typedef struct _EggToolPaletteDragData EggToolPaletteDragData;

enum
{
  PROP_NONE,
  PROP_ICON_SIZE,
  PROP_ORIENTATION,
  PROP_TOOLBAR_STYLE,
};

struct _EggToolPalettePrivate
{
  EggToolItemGroup **groups;
  gsize              groups_size;
  gsize              groups_length;

  GtkAdjustment     *hadjustment;
  GtkAdjustment     *vadjustment;

  GtkRequisition     item_size;
  GtkIconSize        icon_size;
  GtkOrientation     orientation;
  GtkToolbarStyle    style;

  guint              sparse_groups : 1;
  guint              drag_source : 1;
};

struct _EggToolPaletteDragData
{
  EggToolPalette *palette;
  GtkToolItem    *item;
};

static GdkAtom dnd_target_atom = GDK_NONE;
static GtkTargetEntry dnd_targets[] =
{
  { "application/x-egg-tool-palette-item", GTK_TARGET_SAME_APP, 0 },
};

G_DEFINE_TYPE (EggToolPalette,
               egg_tool_palette,
               GTK_TYPE_CONTAINER);

static void
egg_tool_palette_init (EggToolPalette *palette)
{
  palette->priv = G_TYPE_INSTANCE_GET_PRIVATE (palette,
                                            EGG_TYPE_TOOL_PALETTE,
                                            EggToolPalettePrivate);

  palette->priv->groups_size = 4;
  palette->priv->groups_length = 0;
  palette->priv->groups = g_new (EggToolItemGroup*, palette->priv->groups_size);

  palette->priv->icon_size = DEFAULT_ICON_SIZE;
  palette->priv->orientation = DEFAULT_ORIENTATION;
  palette->priv->style = DEFAULT_TOOLBAR_STYLE;
}

#ifdef GTK_TOOL_SHELL

static void
egg_tool_palette_reconfigured_foreach_item (GtkWidget *child,
                                            gpointer   data G_GNUC_UNUSED)
{
  if (GTK_IS_TOOL_ITEM (child))
    gtk_tool_item_toolbar_reconfigured (GTK_TOOL_ITEM (child));
}


static void
egg_tool_palette_reconfigured (EggToolPalette *palette)
{
  guint i;

  for (i = 0; i < palette->priv->groups_length; ++i)
    {
      EggToolItemGroup *group = palette->priv->groups[i];

      if (!group)
        continue;

      gtk_container_foreach (GTK_CONTAINER (group),
                             egg_tool_palette_reconfigured_foreach_item,
                             NULL);
    }

  gtk_widget_queue_resize_no_redraw (GTK_WIDGET (palette));
}

#else /* GTK_TOOL_SHELL */

static void
egg_tool_palette_reconfigured (EggToolPalette *palette)
{
  gtk_widget_queue_resize_no_redraw (GTK_WIDGET (palette));
}

#endif /* GTK_TOOL_SHELL */

static void
egg_tool_palette_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  EggToolPalette *palette = EGG_TOOL_PALETTE (object);

  switch (prop_id)
    {
      case PROP_ICON_SIZE:
        if ((guint) g_value_get_enum (value) != palette->priv->icon_size)
          {
            palette->priv->icon_size = g_value_get_enum (value);
            egg_tool_palette_reconfigured (palette);
          }
        break;

      case PROP_ORIENTATION:
        if ((guint) g_value_get_enum (value) != palette->priv->orientation)
          {
            palette->priv->orientation = g_value_get_enum (value);
            egg_tool_palette_reconfigured (palette);
          }
        break;

      case PROP_TOOLBAR_STYLE:
        if ((guint) g_value_get_enum (value) != palette->priv->style)
          {
            palette->priv->style = g_value_get_enum (value);
            egg_tool_palette_reconfigured (palette);
          }
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
egg_tool_palette_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  EggToolPalette *palette = EGG_TOOL_PALETTE (object);

  switch (prop_id)
    {
      case PROP_ICON_SIZE:
        g_value_set_enum (value, egg_tool_palette_get_icon_size (palette));
        break;

      case PROP_ORIENTATION:
        g_value_set_enum (value, egg_tool_palette_get_orientation (palette));
        break;

      case PROP_TOOLBAR_STYLE:
        g_value_set_enum (value, egg_tool_palette_get_style (palette));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
egg_tool_palette_dispose (GObject *object)
{
  EggToolPalette *palette = EGG_TOOL_PALETTE (object);

  if (palette->priv->hadjustment)
    {
      g_object_unref (palette->priv->hadjustment);
      palette->priv->hadjustment = NULL;
    }

  if (palette->priv->vadjustment)
    {
      g_object_unref (palette->priv->vadjustment);
      palette->priv->vadjustment = NULL;
    }

  G_OBJECT_CLASS (egg_tool_palette_parent_class)->dispose (object);
}

static void
egg_tool_palette_finalize (GObject *object)
{
  EggToolPalette *palette = EGG_TOOL_PALETTE (object);

  if (palette->priv->groups)
    {
      g_free (palette->priv->groups);
      palette->priv->groups = NULL;
    }

  G_OBJECT_CLASS (egg_tool_palette_parent_class)->finalize (object);
}

static void
egg_tool_palette_size_request (GtkWidget      *widget,
                               GtkRequisition *requisition)
{
  const gint border_width = GTK_CONTAINER (widget)->border_width;
  EggToolPalette *palette = EGG_TOOL_PALETTE (widget);
  GtkRequisition child_requisition;
  guint i;

  requisition->width = 0;
  requisition->height = 0;

  palette->priv->item_size.width = 0;
  palette->priv->item_size.height = 0;

  for (i = 0; i < palette->priv->groups_length; ++i)
    {
      EggToolItemGroup *group = palette->priv->groups[i];

      if (!group)
        continue;

      gtk_widget_size_request (GTK_WIDGET (group), &child_requisition);

      if (GTK_ORIENTATION_VERTICAL == palette->priv->orientation)
        {
          requisition->width = MAX (requisition->width, child_requisition.width);
          requisition->height += child_requisition.height;
        }
      else
        {
          requisition->width += child_requisition.width;
          requisition->height = MAX (requisition->height, child_requisition.height);
        }

      _egg_tool_item_group_item_size_request (group, &child_requisition);

      palette->priv->item_size.width = MAX (palette->priv->item_size.width,
                                            child_requisition.width);
      palette->priv->item_size.height = MAX (palette->priv->item_size.height,
                                             child_requisition.height);
    }

  requisition->width += border_width * 2;
  requisition->height += border_width * 2;
}

static void
egg_tool_palette_size_allocate (GtkWidget     *widget,
                                GtkAllocation *allocation)
{
  const gint border_width = GTK_CONTAINER (widget)->border_width;
  EggToolPalette *palette = EGG_TOOL_PALETTE (widget);
  GtkAllocation child_allocation;
  gint offset = 0;
  guint i;

  GTK_WIDGET_CLASS (egg_tool_palette_parent_class)->size_allocate (widget, allocation);

  if (palette->priv->vadjustment)
    offset = gtk_adjustment_get_value (palette->priv->vadjustment);

  child_allocation.x = border_width;
  child_allocation.y = border_width - offset;
  child_allocation.width = allocation->width - border_width * 2;

  for (i = 0; i < palette->priv->groups_length; ++i)
    {
      EggToolItemGroup *group = palette->priv->groups[i];

      if (egg_tool_item_group_get_n_items (group))
        {
          child_allocation.height = _egg_tool_item_group_get_height_for_width (group, child_allocation.width);

          gtk_widget_size_allocate (GTK_WIDGET (group), &child_allocation);
          gtk_widget_show (GTK_WIDGET (group));

          child_allocation.y += child_allocation.height;
        }
      else
        gtk_widget_hide (GTK_WIDGET (group));
    }

  child_allocation.y += border_width;
  child_allocation.y += offset;

  if (palette->priv->vadjustment)
    {
      GtkAdjustment *vadj = palette->priv->vadjustment;

      vadj->page_increment = allocation->height * 0.9;
      vadj->step_increment = allocation->height * 0.1;
      vadj->upper = MAX (0, child_allocation.y);
      vadj->page_size = allocation->height;

      gtk_adjustment_clamp_page (vadj,
                                 MIN (offset, vadj->upper - vadj->page_size),
                                 offset + allocation->height);

      gtk_adjustment_changed (vadj);
    }
}

static gboolean
egg_tool_palette_expose_event (GtkWidget      *widget,
                               GdkEventExpose *event)
{
  EggToolPalette *palette = EGG_TOOL_PALETTE (widget);
  GdkDisplay *display;
  cairo_t *cr;
  guint i;

  display = gdk_drawable_get_display (widget->window);

  if (!gdk_display_supports_composite (display))
    return FALSE;

  cr = gdk_cairo_create (widget->window);
  gdk_cairo_region (cr, event->region);
  cairo_clip (cr);

  cairo_push_group (cr);

  for (i = 0; i < palette->priv->groups_length; ++i)
    {
      EggToolItemGroup *group = palette->priv->groups[i];

      if (group)
        _egg_tool_item_group_paint (group, cr);
    }

  cairo_pop_group_to_source (cr);
  cairo_paint (cr);
  cairo_destroy (cr);

  return FALSE;
}

static void
egg_tool_palette_realize (GtkWidget *widget)
{
  const gint border_width = GTK_CONTAINER (widget)->border_width;
  gint attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
  GdkWindowAttr attributes;

  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.x = widget->allocation.x + border_width;
  attributes.y = widget->allocation.y + border_width;
  attributes.width = widget->allocation.width - border_width * 2;
  attributes.height = widget->allocation.height - border_width * 2;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes.colormap = gtk_widget_get_colormap (widget);
  attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK | GDK_EXPOSURE_MASK
                        | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
                        | GDK_BUTTON_MOTION_MASK;

  widget->window = gdk_window_new (gtk_widget_get_parent_window (widget),
                                   &attributes, attributes_mask);

  gdk_window_set_user_data (widget->window, widget);
  widget->style = gtk_style_attach (widget->style, widget->window);
  gtk_style_set_background (widget->style, widget->window, GTK_STATE_NORMAL);
  GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

  gtk_container_forall (GTK_CONTAINER (widget),
                        (GtkCallback) gtk_widget_set_parent_window,
                        widget->window);

  gtk_widget_queue_resize_no_redraw (widget);
}

static void
egg_tool_palette_adjustment_value_changed (GtkAdjustment *adjustment G_GNUC_UNUSED,
                                           gpointer       data)
{
  GtkWidget *widget = GTK_WIDGET (data);
  egg_tool_palette_size_allocate (widget, &widget->allocation);
}

static void
egg_tool_palette_set_scroll_adjustments (GtkWidget     *widget,
                                         GtkAdjustment *hadjustment,
                                         GtkAdjustment *vadjustment)
{
  EggToolPalette *palette = EGG_TOOL_PALETTE (widget);

  if (palette->priv->hadjustment)
    g_object_unref (palette->priv->hadjustment);
  if (palette->priv->vadjustment)
    g_object_unref (palette->priv->vadjustment);

  if (hadjustment)
    g_object_ref_sink (hadjustment);
  if (vadjustment)
    g_object_ref_sink (vadjustment);

  palette->priv->hadjustment = hadjustment;
  palette->priv->vadjustment = vadjustment;

  if (palette->priv->hadjustment)
    g_signal_connect (palette->priv->hadjustment, "value-changed",
                      G_CALLBACK (egg_tool_palette_adjustment_value_changed),
                      palette);
  if (palette->priv->vadjustment)
    g_signal_connect (palette->priv->vadjustment, "value-changed",
                      G_CALLBACK (egg_tool_palette_adjustment_value_changed),
                      palette);
}

static void
egg_tool_palette_repack (EggToolPalette *palette)
{
  guint si, di;

  if (palette->priv->sparse_groups)
    for (si = di = 0; di < palette->priv->groups_length; ++si)
      {
        if (palette->priv->groups[si])
          {
            palette->priv->groups[di] = palette->priv->groups[si];
            ++di;
          }
        else
          --palette->priv->groups_length;
      }

  palette->priv->sparse_groups = FALSE;
}

static void
egg_tool_palette_add (GtkContainer *container,
                      GtkWidget    *child)
{
  EggToolPalette *palette;

  g_return_if_fail (EGG_IS_TOOL_PALETTE (container));
  g_return_if_fail (EGG_IS_TOOL_ITEM_GROUP (child));

  palette = EGG_TOOL_PALETTE (container);

  if (palette->priv->groups_length == palette->priv->groups_size)
    egg_tool_palette_repack (palette);

  if (palette->priv->groups_length == palette->priv->groups_size)
    {
      palette->priv->groups_size *= 2;
      palette->priv->groups = g_renew (EggToolItemGroup*,
                                       palette->priv->groups,
                                       palette->priv->groups_size);
    }

  palette->priv->groups[palette->priv->groups_length] = g_object_ref_sink (child);
  palette->priv->groups_length += 1;

  gtk_widget_set_parent (child, GTK_WIDGET (palette));
}

static void
egg_tool_palette_remove (GtkContainer *container,
                         GtkWidget    *child)
{
  EggToolPalette *palette;
  guint i;

  g_return_if_fail (EGG_IS_TOOL_PALETTE (container));
  palette = EGG_TOOL_PALETTE (container);

  for (i = 0; i < palette->priv->groups_length; ++i)
    if ((GtkWidget*) palette->priv->groups[i] == child)
      {
        g_object_unref (child);
        gtk_widget_unparent (child);
        palette->priv->groups[i] = NULL;
        palette->priv->sparse_groups = TRUE;
      }
}

static void
egg_tool_palette_forall (GtkContainer *container,
                         gboolean      internals G_GNUC_UNUSED,
                         GtkCallback   callback,
                         gpointer      callback_data)
{
  EggToolPalette *palette = EGG_TOOL_PALETTE (container);
  guint i;

  if (palette->priv->groups)
    for (i = 0; i < palette->priv->groups_length; ++i)
      {
        EggToolItemGroup *group = palette->priv->groups[i];

        if (group)
          callback (GTK_WIDGET (group), callback_data);
      }
}

static GType
egg_tool_palette_child_type (GtkContainer *container G_GNUC_UNUSED)
{
  return EGG_TYPE_TOOL_ITEM_GROUP;
}

static void
egg_tool_palette_class_init (EggToolPaletteClass *cls)
{
  GObjectClass *oclass = G_OBJECT_CLASS (cls);
  GtkWidgetClass *wclass = GTK_WIDGET_CLASS (cls);
  GtkContainerClass *cclass = GTK_CONTAINER_CLASS (cls);

  oclass->set_property        = egg_tool_palette_set_property;
  oclass->get_property        = egg_tool_palette_get_property;
  oclass->dispose             = egg_tool_palette_dispose;
  oclass->finalize            = egg_tool_palette_finalize;

  wclass->size_request        = egg_tool_palette_size_request;
  wclass->size_allocate       = egg_tool_palette_size_allocate;
  wclass->expose_event        = egg_tool_palette_expose_event;
  wclass->realize             = egg_tool_palette_realize;

  cclass->add                 = egg_tool_palette_add;
  cclass->remove              = egg_tool_palette_remove;
  cclass->forall              = egg_tool_palette_forall;
  cclass->child_type          = egg_tool_palette_child_type;

  cls->set_scroll_adjustments = egg_tool_palette_set_scroll_adjustments;

  wclass->set_scroll_adjustments_signal =
    g_signal_new ("set-scroll-adjustments",
                  G_TYPE_FROM_CLASS (oclass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EggToolPaletteClass, set_scroll_adjustments),
                  NULL, NULL,
                  _egg_marshal_VOID__OBJECT_OBJECT,
                  G_TYPE_NONE, 2,
                  GTK_TYPE_ADJUSTMENT,
                  GTK_TYPE_ADJUSTMENT);

  g_object_class_install_property (oclass, PROP_ICON_SIZE,
                                   g_param_spec_enum ("icon-size",
                                                      P_("Icon Size"),
                                                      P_("The size of palette icons"),
                                                      GTK_TYPE_ICON_SIZE,
                                                      DEFAULT_ICON_SIZE,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

  g_object_class_install_property (oclass, PROP_ORIENTATION,
                                   g_param_spec_enum ("orientation",
                                                      P_("Orientation"),
                                                      P_("Orientation of the tool palette"),
                                                      GTK_TYPE_ORIENTATION,
                                                      DEFAULT_ORIENTATION,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

  g_object_class_install_property (oclass, PROP_TOOLBAR_STYLE,
                                   g_param_spec_enum ("toolbar-style",
                                                      P_("Toolbar Style"),
                                                      P_("Style of items in the tool palette"),
                                                      GTK_TYPE_TOOLBAR_STYLE,
                                                      DEFAULT_TOOLBAR_STYLE,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

  g_type_class_add_private (cls, sizeof (EggToolPalettePrivate));

  dnd_target_atom = gdk_atom_intern_static_string (dnd_targets[0].target);
}

GtkWidget*
egg_tool_palette_new (void)
{
  return g_object_new (EGG_TYPE_TOOL_PALETTE, NULL);
}

void
egg_tool_palette_set_icon_size (EggToolPalette *palette,
                                GtkIconSize     icon_size)
{
  g_return_if_fail (EGG_IS_TOOL_PALETTE (palette));

  if (icon_size != palette->priv->icon_size)
    g_object_set (palette, "icon-size", icon_size, NULL);
}

void
egg_tool_palette_set_orientation (EggToolPalette *palette,
                                  GtkOrientation  orientation)
{
  g_return_if_fail (EGG_IS_TOOL_PALETTE (palette));

  if (orientation != palette->priv->orientation)
    g_object_set (palette, "orientation", orientation, NULL);
}

void
egg_tool_palette_set_style (EggToolPalette  *palette,
                            GtkToolbarStyle  style)
{
  g_return_if_fail (EGG_IS_TOOL_PALETTE (palette));

  if (style != palette->priv->style)
    g_object_set (palette, "style", style, NULL);
}

GtkIconSize
egg_tool_palette_get_icon_size (EggToolPalette *palette)
{
  g_return_val_if_fail (EGG_IS_TOOL_PALETTE (palette), DEFAULT_ICON_SIZE);
  return palette->priv->icon_size;
}

GtkOrientation
egg_tool_palette_get_orientation (EggToolPalette *palette)
{
  g_return_val_if_fail (EGG_IS_TOOL_PALETTE (palette), DEFAULT_ORIENTATION);
  return palette->priv->orientation;
}

GtkToolbarStyle
egg_tool_palette_get_style (EggToolPalette *palette)
{
  g_return_val_if_fail (EGG_IS_TOOL_PALETTE (palette), DEFAULT_TOOLBAR_STYLE);
  return palette->priv->style;
}

void
egg_tool_palette_reorder_group (EggToolPalette *palette,
                                GtkWidget      *group,
                                guint           position)
{
  g_return_if_fail (EGG_IS_TOOL_PALETTE (palette));
  g_return_if_fail (EGG_IS_TOOL_ITEM_GROUP (group));
  g_return_if_fail (position < palette->priv->groups_length);

  g_return_if_reached ();
}

GtkToolItem*
egg_tool_palette_get_drop_item (EggToolPalette   *palette,
                                gint              x,
                                gint              y)
{
  GtkAllocation *allocation;

  g_return_val_if_fail (EGG_IS_TOOL_PALETTE (palette), NULL);

  allocation = &GTK_WIDGET (palette)->allocation;

  g_return_val_if_fail (x >= 0 && x < allocation->width, NULL);
  g_return_val_if_fail (y >= 0 && y < allocation->width, NULL);

  g_return_val_if_reached (NULL);
}

GtkToolItem*
egg_tool_palette_get_drag_item (EggToolPalette   *palette,
                                GtkSelectionData *selection)
{
  EggToolPaletteDragData *data;

  g_return_val_if_fail (EGG_IS_TOOL_PALETTE (palette), NULL);
  g_return_val_if_fail (NULL != selection, NULL);

  g_return_val_if_fail (selection->format == 8, NULL);
  g_return_val_if_fail (selection->length == sizeof (EggToolPaletteDragData), NULL);
  g_return_val_if_fail (selection->target == dnd_target_atom, NULL);

  data = (EggToolPaletteDragData*) selection->data;

  g_return_val_if_fail (data->palette == palette, NULL);

  return data->item;
}

void
egg_tool_palette_set_drag_source (EggToolPalette *palette)
{
  guint i;

  g_return_if_fail (EGG_IS_TOOL_PALETTE (palette));

  if (palette->priv->drag_source)
    return;

  palette->priv->drag_source = TRUE;

  for (i = 0; i < palette->priv->groups_length; ++i)
    gtk_container_foreach (GTK_CONTAINER (palette->priv->groups[i]),
                           _egg_tool_palette_item_set_drag_source,
                           palette);
}

void
egg_tool_palette_add_drag_dest (EggToolPalette  *palette,
                                GtkWidget       *widget,
                                GtkDestDefaults  flags)
{
  g_return_if_fail (EGG_IS_TOOL_PALETTE (palette));
  g_return_if_fail (GTK_IS_WIDGET (widget));

  egg_tool_palette_set_drag_source (palette);

  gtk_drag_dest_set (widget, flags, dnd_targets,
                     G_N_ELEMENTS (dnd_targets),
                     GDK_ACTION_COPY);
}

void
_egg_tool_palette_get_item_size (EggToolPalette *palette,
                                 GtkRequisition *item_size)
{
  g_return_if_fail (EGG_IS_TOOL_PALETTE (palette));
  g_return_if_fail (NULL != item_size);

  *item_size = palette->priv->item_size;
}

static GtkToolItem*
egg_tool_palette_find_tool_item (GtkWidget *widget)
{
  while (widget)
    {
      if (GTK_IS_TOOL_ITEM (widget))
        return GTK_TOOL_ITEM (widget);

      widget = gtk_widget_get_parent (widget);
    }

  return NULL;
}

static void
egg_tool_palette_item_drag_data_get (GtkWidget        *widget,
                                     GdkDragContext   *context G_GNUC_UNUSED,
                                     GtkSelectionData *selection,
                                     guint             info G_GNUC_UNUSED,
                                     guint             time G_GNUC_UNUSED,
                                     gpointer          data)
{
  EggToolPaletteDragData drag_data = {
    EGG_TOOL_PALETTE (data), NULL
  };

  if (selection->target == dnd_target_atom)
    drag_data.item = egg_tool_palette_find_tool_item (widget);

  if (drag_data.item)
    gtk_selection_data_set (selection, selection->target, 8,
                            (guchar*) &drag_data, sizeof (drag_data));
}

void
_egg_tool_palette_item_set_drag_source (GtkWidget *widget,
                                        gpointer   data)
{
  EggToolPalette *palette = EGG_TOOL_PALETTE (data);

  g_return_if_fail (GTK_IS_TOOL_ITEM (widget));

  if (palette->priv->drag_source)
    {
      /* Connect to child, instead of the item itself work arround bug 510377.
       */
      GtkWidget *child = gtk_bin_get_child (GTK_BIN (widget));

      gtk_drag_source_set (child,
                           GDK_BUTTON1_MASK | GDK_BUTTON3_MASK,
                           dnd_targets, G_N_ELEMENTS (dnd_targets),
                           GDK_ACTION_COPY);

      g_signal_connect (child, "drag-data-get",
                        G_CALLBACK (egg_tool_palette_item_drag_data_get),
                        palette);
    }
}