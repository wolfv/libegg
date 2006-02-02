/*
 * Copyright (C) 2002 Gustavo Gir�ldez <gustavo.giraldez@gmx.net>
 * Copyright (C) 2003 Biswapesh Chattopadhyay <biswapesh_chatterjee@tcscal.co.in>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.  
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <util/eggintl.h>
#include <stdlib.h>
#include <string.h>
#include <util/egg-macros.h>

#include "egg-dock.h"
#include "egg-dock-master.h"
#include "egg-dock-paned.h"
#include "egg-dock-notebook.h"
#include "egg-dock-placeholder.h"

#include <util/eggmarshalers.h>


/* ----- Private prototypes ----- */

static void  egg_dock_class_init      (EggDockClass *class);
static void  egg_dock_instance_init   (EggDock *dock);

static GObject *egg_dock_constructor  (GType                  type,
                                       guint                  n_construct_properties,
                                       GObjectConstructParam *construct_param);
static void  egg_dock_set_property    (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec);
static void  egg_dock_get_property    (GObject      *object,
                                       guint         prop_id,
                                       GValue       *value,
                                       GParamSpec   *pspec);
static void  egg_dock_notify_cb       (GObject      *object,
                                       GParamSpec   *pspec,
                                       gpointer      user_data);

static void  egg_dock_set_title       (EggDock      *dock);

static void  egg_dock_destroy         (GtkObject    *object);

static void  egg_dock_size_request    (GtkWidget      *widget,
                                       GtkRequisition *requisition);
static void  egg_dock_size_allocate   (GtkWidget      *widget,
                                       GtkAllocation  *allocation);
static void  egg_dock_map             (GtkWidget      *widget);
static void  egg_dock_unmap           (GtkWidget      *widget);
static void  egg_dock_show            (GtkWidget      *widget);
static void  egg_dock_hide            (GtkWidget      *widget);

static void  egg_dock_add             (GtkContainer *container,
                                       GtkWidget    *widget);
static void  egg_dock_remove          (GtkContainer *container,
                                       GtkWidget    *widget);
static void  egg_dock_forall          (GtkContainer *container,
                                       gboolean      include_internals,
                                       GtkCallback   callback,
                                       gpointer      callback_data);
static GtkType  egg_dock_child_type   (GtkContainer *container);

static void     egg_dock_detach       (EggDockObject    *object,
                                       gboolean          recursive);
static void     egg_dock_reduce       (EggDockObject    *object);
static gboolean egg_dock_dock_request (EggDockObject    *object,
                                       gint              x,
                                       gint              y,
                                       EggDockRequest   *request);
static void     egg_dock_dock         (EggDockObject    *object,
                                       EggDockObject    *requestor,
                                       EggDockPlacement  position,
                                       GValue           *other_data);
static gboolean egg_dock_reorder      (EggDockObject    *object,
                                       EggDockObject    *requestor,
                                       EggDockPlacement  new_position,
                                       GValue           *other_data);

static gboolean egg_dock_floating_window_delete_event_cb (GtkWidget *widget);

static gboolean egg_dock_child_placement  (EggDockObject    *object,
                                           EggDockObject    *child,
                                           EggDockPlacement *placement);

static void     egg_dock_present          (EggDockObject    *object,
                                           EggDockObject    *child);


/* ----- Class variables and definitions ----- */

struct _EggDockPrivate
{
    /* for floating docks */
    gboolean            floating;
    GtkWidget          *window;
    gboolean            auto_title;
    
    gint                float_x;
    gint                float_y;
    gint                width;
    gint                height;
    
    /* auxiliary fields */
    GdkGC              *xor_gc;
};

enum {
    LAYOUT_CHANGED,
    LAST_SIGNAL
};

enum {
    PROP_0,
    PROP_FLOATING,
    PROP_DEFAULT_TITLE,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_FLOAT_X,
    PROP_FLOAT_Y
};

static guint dock_signals [LAST_SIGNAL] = { 0 };

#define SPLIT_RATIO  0.3


/* ----- Private functions ----- */

EGG_CLASS_BOILERPLATE (EggDock, egg_dock, EggDockObject, EGG_TYPE_DOCK_OBJECT);

static void
egg_dock_class_init (EggDockClass *klass)
{
    GObjectClass       *g_object_class;
    GtkObjectClass     *gtk_object_class;
    GtkWidgetClass     *widget_class;
    GtkContainerClass  *container_class;
    EggDockObjectClass *object_class;
    
    g_object_class = G_OBJECT_CLASS (klass);
    gtk_object_class = GTK_OBJECT_CLASS (klass);
    widget_class = GTK_WIDGET_CLASS (klass);
    container_class = GTK_CONTAINER_CLASS (klass);
    object_class = EGG_DOCK_OBJECT_CLASS (klass);
    
    g_object_class->constructor = egg_dock_constructor;
    g_object_class->set_property = egg_dock_set_property;
    g_object_class->get_property = egg_dock_get_property;
    
    /* properties */

    g_object_class_install_property (
        g_object_class, PROP_FLOATING,
        g_param_spec_boolean ("floating", _("Floating"),
                              _("Whether the dock is floating in its own window"),
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                              EGG_DOCK_PARAM_EXPORT));
    
    g_object_class_install_property (
        g_object_class, PROP_DEFAULT_TITLE,
        g_param_spec_string ("default_title", _("Default title"),
                             _("Default title for the newly created floating docks"),
                             NULL,
                             G_PARAM_READWRITE));
    
    g_object_class_install_property (
        g_object_class, PROP_WIDTH,
        g_param_spec_int ("width", _("Width"),
                          _("Width for the dock when it's of floating type"),
                          -1, G_MAXINT, -1,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
                          EGG_DOCK_PARAM_EXPORT));
    
    g_object_class_install_property (
        g_object_class, PROP_HEIGHT,
        g_param_spec_int ("height", _("Height"),
                          _("Height for the dock when it's of floating type"),
                          -1, G_MAXINT, -1,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
                          EGG_DOCK_PARAM_EXPORT));
    
    g_object_class_install_property (
        g_object_class, PROP_FLOAT_X,
        g_param_spec_int ("floatx", _("Float X"),
                          _("X coordinate for a floating dock"),
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
                          EGG_DOCK_PARAM_EXPORT));
    
    g_object_class_install_property (
        g_object_class, PROP_FLOAT_Y,
        g_param_spec_int ("floaty", _("Float Y"),
                          _("Y coordinate for a floating dock"),
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
                          EGG_DOCK_PARAM_EXPORT));
    
    gtk_object_class->destroy = egg_dock_destroy;

    widget_class->size_request = egg_dock_size_request;
    widget_class->size_allocate = egg_dock_size_allocate;
    widget_class->map = egg_dock_map;
    widget_class->unmap = egg_dock_unmap;
    widget_class->show = egg_dock_show;
    widget_class->hide = egg_dock_hide;
    
    container_class->add = egg_dock_add;
    container_class->remove = egg_dock_remove;
    container_class->forall = egg_dock_forall;
    container_class->child_type = egg_dock_child_type;
    
    object_class->is_compound = TRUE;
    
    object_class->detach = egg_dock_detach;
    object_class->reduce = egg_dock_reduce;
    object_class->dock_request = egg_dock_dock_request;
    object_class->dock = egg_dock_dock;
    object_class->reorder = egg_dock_reorder;    
    object_class->child_placement = egg_dock_child_placement;
    object_class->present = egg_dock_present;
    
    /* signals */

    dock_signals [LAYOUT_CHANGED] = 
        g_signal_new ("layout_changed", 
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (EggDockClass, layout_changed),
                      NULL, /* accumulator */
                      NULL, /* accu_data */
                      _egg_marshal_VOID__VOID,
                      G_TYPE_NONE, /* return type */
                      0);

    klass->layout_changed = NULL;
}

static void
egg_dock_instance_init (EggDock *dock)
{
    GTK_WIDGET_SET_FLAGS (GTK_WIDGET (dock), GTK_NO_WINDOW);

    dock->root = NULL;
    dock->_priv = g_new0 (EggDockPrivate, 1);
    dock->_priv->width = -1;
    dock->_priv->height = -1;
}

static gboolean 
egg_dock_floating_configure_event_cb (GtkWidget         *widget,
                                      GdkEventConfigure *event,
                                      gpointer           user_data)
{
    EggDock *dock;
    
    g_return_val_if_fail (user_data != NULL && EGG_IS_DOCK (user_data), TRUE);

    dock = EGG_DOCK (user_data);
    dock->_priv->float_x = event->x;
    dock->_priv->float_y = event->y;
    dock->_priv->width = event->width;
    dock->_priv->height = event->height;

    return FALSE;
}

static GObject *
egg_dock_constructor (GType                  type,
                      guint                  n_construct_properties,
                      GObjectConstructParam *construct_param)
{
    GObject *g_object;
    
    g_object = EGG_CALL_PARENT_WITH_DEFAULT (G_OBJECT_CLASS, 
                                               constructor, 
                                               (type,
                                                n_construct_properties,
                                                construct_param),
                                               NULL);
    if (g_object) {
        EggDock *dock = EGG_DOCK (g_object);
        EggDockMaster *master;
        
        /* create a master for the dock if none was provided in the construction */
        master = EGG_DOCK_OBJECT_GET_MASTER (EGG_DOCK_OBJECT (dock));
        if (!master) {
            EGG_DOCK_OBJECT_UNSET_FLAGS (dock, EGG_DOCK_AUTOMATIC);
            master = g_object_new (EGG_TYPE_DOCK_MASTER, NULL);
            /* the controller owns the master ref */
            egg_dock_object_bind (EGG_DOCK_OBJECT (dock), G_OBJECT (master));
        }

        if (dock->_priv->floating) {
            EggDockObject *controller;
            
            /* create floating window for this dock */
            dock->_priv->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
            g_object_set_data (G_OBJECT (dock->_priv->window), "dock", dock);
            
            /* set position and default size */
            gtk_window_set_position (GTK_WINDOW (dock->_priv->window),
                                     GTK_WIN_POS_MOUSE);
            gtk_window_set_default_size (GTK_WINDOW (dock->_priv->window),
                                         dock->_priv->width,
                                         dock->_priv->height);
            gtk_window_set_type_hint (GTK_WINDOW (dock->_priv->window),
                                      GDK_WINDOW_TYPE_HINT_NORMAL);
            
            /* metacity ignores this */
            gtk_window_move (GTK_WINDOW (dock->_priv->window),
                             dock->_priv->float_x,
                             dock->_priv->float_y);
            
            /* connect to the configure event so we can track down window geometry */
            g_signal_connect (dock->_priv->window, "configure_event",
                              (GCallback) egg_dock_floating_configure_event_cb,
                              dock);
            
            /* set the title and connect to the long_name notify queue
               so we can reset the title when this prop changes */
            egg_dock_set_title (dock);
            g_signal_connect (dock, "notify::long_name",
                              (GCallback) egg_dock_notify_cb, NULL);
            
            /* set transient for the first dock if that is a non-floating dock */
            controller = egg_dock_master_get_controller (master);
            if (controller && EGG_IS_DOCK (controller)) {
                gboolean first_is_floating;
                g_object_get (controller, "floating", &first_is_floating, NULL);
                if (!first_is_floating) {
                    GtkWidget *toplevel =
                        gtk_widget_get_toplevel (GTK_WIDGET (controller));

                    if (GTK_IS_WINDOW (toplevel))
                        gtk_window_set_transient_for (GTK_WINDOW (dock->_priv->window),
                                                      GTK_WINDOW (toplevel));
                }
            }

            gtk_container_add (GTK_CONTAINER (dock->_priv->window), GTK_WIDGET (dock));
    
            g_signal_connect (dock->_priv->window, "delete_event",
                              G_CALLBACK (egg_dock_floating_window_delete_event_cb), 
                              NULL);
        }
        EGG_DOCK_OBJECT_SET_FLAGS (dock, EGG_DOCK_ATTACHED);
    }
    
    return g_object;
}

static void
egg_dock_set_property  (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
    EggDock *dock = EGG_DOCK (object);
    
    switch (prop_id) {
        case PROP_FLOATING:
            dock->_priv->floating = g_value_get_boolean (value);
            break;
        case PROP_DEFAULT_TITLE:
            if (EGG_DOCK_OBJECT (object)->master)
                g_object_set (EGG_DOCK_OBJECT (object)->master,
                              "default_title", g_value_get_string (value),
                              NULL);
            break;
        case PROP_WIDTH:
            dock->_priv->width = g_value_get_int (value);
            break;
        case PROP_HEIGHT:
            dock->_priv->height = g_value_get_int (value);
            break;
        case PROP_FLOAT_X:
            dock->_priv->float_x = g_value_get_int (value);
            break;
        case PROP_FLOAT_Y:
            dock->_priv->float_y = g_value_get_int (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }

    switch (prop_id) {
        case PROP_WIDTH:
        case PROP_HEIGHT:
        case PROP_FLOAT_X:
        case PROP_FLOAT_Y:
            if (dock->_priv->floating && dock->_priv->window) {
                gtk_window_resize (GTK_WINDOW (dock->_priv->window),
                                   dock->_priv->width,
                                   dock->_priv->height);
            }
            break;
    }
}

static void
egg_dock_get_property  (GObject      *object,
                        guint         prop_id,
                        GValue       *value,
                        GParamSpec   *pspec)
{
    EggDock *dock = EGG_DOCK (object);

    switch (prop_id) {
        case PROP_FLOATING:
            g_value_set_boolean (value, dock->_priv->floating);
            break;
        case PROP_DEFAULT_TITLE:
            if (EGG_DOCK_OBJECT (object)->master) {
                gchar *default_title;
                g_object_get (EGG_DOCK_OBJECT (object)->master,
                              "default_title", &default_title,
                              NULL);
                g_value_take_string (value, default_title);
            }
            else
                g_value_set_string (value, NULL);
            break;
        case PROP_WIDTH:
            g_value_set_int (value, dock->_priv->width);
            break;
        case PROP_HEIGHT:
            g_value_set_int (value, dock->_priv->height);
            break;
        case PROP_FLOAT_X:
            g_value_set_int (value, dock->_priv->float_x);
            break;
        case PROP_FLOAT_Y:
            g_value_set_int (value, dock->_priv->float_y);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
egg_dock_set_title (EggDock *dock)
{
    EggDockObject *object = EGG_DOCK_OBJECT (dock);
    gchar         *title = NULL;
    gboolean       free_title = FALSE;
    
    if (!dock->_priv->window)
        return;
    
    if (!dock->_priv->auto_title && object->long_name) {
        title = object->long_name;
    }
    else if (object->master) {
        g_object_get (object->master, "default_title", &title, NULL);
        free_title = TRUE;
    }

    if (!title && dock->root) {
        g_object_get (dock->root, "long_name", &title, NULL);
        free_title = TRUE;
    }
    
    if (!title) {
        /* set a default title in the long_name */
        dock->_priv->auto_title = TRUE;
        free_title = FALSE;
        title = object->long_name = g_strdup_printf (
            _("Dock #%d"), EGG_DOCK_MASTER (object->master)->dock_number++);
    }

    gtk_window_set_title (GTK_WINDOW (dock->_priv->window), title);
    if (free_title)
        g_free (title);
}

static void
egg_dock_notify_cb (GObject    *object,
                    GParamSpec *pspec,
                    gpointer    user_data)
{
    EggDock *dock;
    
    g_return_if_fail (object != NULL || EGG_IS_DOCK (object));
    
    dock = EGG_DOCK (object);
    dock->_priv->auto_title = FALSE;
    egg_dock_set_title (dock);
}

static void
egg_dock_destroy (GtkObject *object)
{
    EggDock *dock = EGG_DOCK (object);

    if (dock->_priv) {
        EggDockPrivate *priv = dock->_priv;
        dock->_priv = NULL;

        if (priv->window) {
            gtk_widget_destroy (priv->window);
            priv->floating = FALSE;
            priv->window = NULL;
        }
        
        /* destroy the xor gc */
        if (priv->xor_gc) {
            g_object_unref (priv->xor_gc);
            priv->xor_gc = NULL;
        }

        g_free (priv);
    }
    
    EGG_CALL_PARENT (GTK_OBJECT_CLASS, destroy, (object));
}

static void
egg_dock_size_request (GtkWidget      *widget,
                       GtkRequisition *requisition)
{
    EggDock      *dock;
    GtkContainer *container;
    guint         border_width;

    g_return_if_fail (widget != NULL);
    g_return_if_fail (EGG_IS_DOCK (widget));

    dock = EGG_DOCK (widget);
    container = GTK_CONTAINER (widget);
    border_width = container->border_width;

    /* make request to root */
    if (dock->root && GTK_WIDGET_VISIBLE (dock->root))
        gtk_widget_size_request (GTK_WIDGET (dock->root), requisition);
    else {
        requisition->width = 0;
        requisition->height = 0;
    };

    requisition->width += 2 * border_width;
    requisition->height += 2 * border_width;

    widget->requisition = *requisition;
}

static void
egg_dock_size_allocate (GtkWidget     *widget,
                        GtkAllocation *allocation)
{
    EggDock      *dock;
    GtkContainer *container;
    guint         border_width;

    g_return_if_fail (widget != NULL);
    g_return_if_fail (EGG_IS_DOCK (widget));
    
    dock = EGG_DOCK (widget);
    container = GTK_CONTAINER (widget);
    border_width = container->border_width;

    widget->allocation = *allocation;

    /* reduce allocation by border width */
    allocation->x += border_width;
    allocation->y += border_width;
    allocation->width = MAX (1, allocation->width - 2 * border_width);
    allocation->height = MAX (1, allocation->height - 2 * border_width);

    if (dock->root && GTK_WIDGET_VISIBLE (dock->root))
        gtk_widget_size_allocate (GTK_WIDGET (dock->root), allocation);
}

static void
egg_dock_map (GtkWidget *widget)
{
    GtkWidget *child;
    EggDock   *dock;

    g_return_if_fail (widget != NULL);
    g_return_if_fail (EGG_IS_DOCK (widget));

    dock = EGG_DOCK (widget);

    EGG_CALL_PARENT (GTK_WIDGET_CLASS, map, (widget));

    if (dock->root) {
        child = GTK_WIDGET (dock->root);
        if (GTK_WIDGET_VISIBLE (child) && !GTK_WIDGET_MAPPED (child))
            gtk_widget_map (child);
    }
}

static void
egg_dock_unmap (GtkWidget *widget)
{
    GtkWidget *child;
    EggDock   *dock;
    
    g_return_if_fail (widget != NULL);
    g_return_if_fail (EGG_IS_DOCK (widget));

    dock = EGG_DOCK (widget);

    EGG_CALL_PARENT (GTK_WIDGET_CLASS, unmap, (widget));

    if (dock->root) {
        child = GTK_WIDGET (dock->root);
        if (GTK_WIDGET_VISIBLE (child) && GTK_WIDGET_MAPPED (child))
            gtk_widget_unmap (child);
    }
    
    if (dock->_priv->window)
        gtk_widget_unmap (dock->_priv->window);
}

static void
egg_dock_foreach_automatic (EggDockObject *object,
                            gpointer       user_data)
{
    void (* function) (GtkWidget *) = user_data;

    if (EGG_DOCK_OBJECT_AUTOMATIC (object))
        (* function) (GTK_WIDGET (object));
}

static void
egg_dock_show (GtkWidget *widget)
{
    EggDock *dock;
    
    g_return_if_fail (widget != NULL);
    g_return_if_fail (EGG_IS_DOCK (widget));
    
    EGG_CALL_PARENT (GTK_WIDGET_CLASS, show, (widget));
    
    dock = EGG_DOCK (widget);
    if (dock->_priv->floating && dock->_priv->window)
        gtk_widget_show (dock->_priv->window);

    if (EGG_DOCK_IS_CONTROLLER (dock)) {
        egg_dock_master_foreach_toplevel (EGG_DOCK_OBJECT_GET_MASTER (dock),
                                          FALSE, (GFunc) egg_dock_foreach_automatic,
                                          gtk_widget_show);
    }
}

static void
egg_dock_hide (GtkWidget *widget)
{
    EggDock *dock;
    
    g_return_if_fail (widget != NULL);
    g_return_if_fail (EGG_IS_DOCK (widget));
    
    EGG_CALL_PARENT (GTK_WIDGET_CLASS, hide, (widget));
    
    dock = EGG_DOCK (widget);
    if (dock->_priv->floating && dock->_priv->window)
        gtk_widget_hide (dock->_priv->window);

    if (EGG_DOCK_IS_CONTROLLER (dock)) {
        egg_dock_master_foreach_toplevel (EGG_DOCK_OBJECT_GET_MASTER (dock),
                                          FALSE, (GFunc) egg_dock_foreach_automatic,
                                          gtk_widget_hide);
    }
}

static void
egg_dock_add (GtkContainer *container,
              GtkWidget    *widget)
{
    g_return_if_fail (container != NULL);
    g_return_if_fail (EGG_IS_DOCK (container));
    g_return_if_fail (EGG_IS_DOCK_ITEM (widget));

    egg_dock_add_item (EGG_DOCK (container), 
                       EGG_DOCK_ITEM (widget), 
                       EGG_DOCK_TOP);  /* default position */
}

static void
egg_dock_remove (GtkContainer *container,
                 GtkWidget    *widget)
{
    EggDock  *dock;
    gboolean  was_visible;

    g_return_if_fail (container != NULL);
    g_return_if_fail (widget != NULL);

    dock = EGG_DOCK (container);
    was_visible = GTK_WIDGET_VISIBLE (widget);

    if (GTK_WIDGET (dock->root) == widget) {
        dock->root = NULL;
        EGG_DOCK_OBJECT_UNSET_FLAGS (widget, EGG_DOCK_ATTACHED);
        gtk_widget_unparent (widget);

        if (was_visible && GTK_WIDGET_VISIBLE (GTK_WIDGET (container)))
            gtk_widget_queue_resize (GTK_WIDGET (dock));
    }
}

static void
egg_dock_forall (GtkContainer *container,
                 gboolean      include_internals,
                 GtkCallback   callback,
                 gpointer      callback_data)
{
    EggDock *dock;

    g_return_if_fail (container != NULL);
    g_return_if_fail (EGG_IS_DOCK (container));
    g_return_if_fail (callback != NULL);

    dock = EGG_DOCK (container);

    if (dock->root)
        (*callback) (GTK_WIDGET (dock->root), callback_data);
}

static GtkType
egg_dock_child_type (GtkContainer *container)
{
    return EGG_TYPE_DOCK_ITEM;
}

static void
egg_dock_detach (EggDockObject *object,
                 gboolean       recursive)
{
    EggDock *dock = EGG_DOCK (object);
    
    /* detach children */
    if (recursive && dock->root) {
        egg_dock_object_detach (dock->root, recursive);
    }
    EGG_DOCK_OBJECT_UNSET_FLAGS (object, EGG_DOCK_ATTACHED);
}

static void
egg_dock_reduce (EggDockObject *object)
{
    EggDock *dock = EGG_DOCK (object);
    
    if (dock->root)
        return;
    
    if (EGG_DOCK_OBJECT_AUTOMATIC (dock)) {
        gtk_widget_destroy (GTK_WIDGET (dock));

    } else if (!EGG_DOCK_OBJECT_ATTACHED (dock)) {
        /* if the user explicitly detached the object */
        if (dock->_priv->floating)
            gtk_widget_hide (GTK_WIDGET (dock));
        else {
            GtkWidget *widget = GTK_WIDGET (object);
            if (widget->parent) 
                gtk_container_remove (GTK_CONTAINER (widget->parent), widget);
        }
    }
}

static gboolean
egg_dock_dock_request (EggDockObject  *object,
                       gint            x,
                       gint            y,
                       EggDockRequest *request)
{
    EggDock            *dock;
    guint               bw;
    gint                rel_x, rel_y;
    GtkAllocation      *alloc;
    gboolean            may_dock = FALSE;
    EggDockRequest      my_request;

    g_return_val_if_fail (EGG_IS_DOCK (object), FALSE);

    /* we get (x,y) in our allocation coordinates system */
    
    dock = EGG_DOCK (object);
    
    /* Get dock size. */
    alloc = &(GTK_WIDGET (dock)->allocation);
    bw = GTK_CONTAINER (dock)->border_width;

    /* Get coordinates relative to our allocation area. */
    rel_x = x - alloc->x;
    rel_y = y - alloc->y;

    if (request)
        my_request = *request;
        
    /* Check if coordinates are in EggDock widget. */
    if (rel_x > 0 && rel_x < alloc->width &&
        rel_y > 0 && rel_y < alloc->height) {

        /* It's inside our area. */
        may_dock = TRUE;

	/* Set docking indicator rectangle to the EggDock size. */
        my_request.rect.x = alloc->x + bw;
        my_request.rect.y = alloc->y + bw;
        my_request.rect.width = alloc->width - 2*bw;
        my_request.rect.height = alloc->height - 2*bw;

	/* If EggDock has no root item yet, set the dock itself as 
	   possible target. */
        if (!dock->root) {
            my_request.position = EGG_DOCK_TOP;
            my_request.target = object;
        } else {
            my_request.target = dock->root;

            /* See if it's in the border_width band. */
            if (rel_x < bw) {
                my_request.position = EGG_DOCK_LEFT;
                my_request.rect.width *= SPLIT_RATIO;
            } else if (rel_x > alloc->width - bw) {
                my_request.position = EGG_DOCK_RIGHT;
                my_request.rect.x += my_request.rect.width * (1 - SPLIT_RATIO);
                my_request.rect.width *= SPLIT_RATIO;
            } else if (rel_y < bw) {
                my_request.position = EGG_DOCK_TOP;
                my_request.rect.height *= SPLIT_RATIO;
            } else if (rel_y > alloc->height - bw) {
                my_request.position = EGG_DOCK_BOTTOM;
                my_request.rect.y += my_request.rect.height * (1 - SPLIT_RATIO);
                my_request.rect.height *= SPLIT_RATIO;
            } else {
                /* Otherwise try our children. */
                /* give them allocation coordinates (we are a
                   GTK_NO_WINDOW) widget */
                may_dock = egg_dock_object_dock_request (EGG_DOCK_OBJECT (dock->root), 
                                                         x, y, &my_request);
            }
        }
    }

    if (may_dock && request)
        *request = my_request;
    
    return may_dock;
}

static void
egg_dock_dock (EggDockObject    *object,
               EggDockObject    *requestor,
               EggDockPlacement  position,
               GValue           *user_data)
{
    EggDock *dock;
    
    g_return_if_fail (EGG_IS_DOCK (object));
    /* only dock items allowed at this time */
    g_return_if_fail (EGG_IS_DOCK_ITEM (requestor));

    dock = EGG_DOCK (object);
    
    if (position == EGG_DOCK_FLOATING) {
        EggDockItem *item = EGG_DOCK_ITEM (requestor);
        gint x, y, width, height;

        if (user_data && G_VALUE_HOLDS (user_data, GDK_TYPE_RECTANGLE)) {
            GdkRectangle *rect;

            rect = g_value_get_boxed (user_data);
            x = rect->x;
            y = rect->y;
            width = rect->width;
            height = rect->height;
        }
        else {
            x = y = 0;
            width = height = -1;
        }
        
        egg_dock_add_floating_item (dock, item,
                                    x, y, width, height);
    }
    else if (dock->root) {
        /* This is somewhat a special case since we know which item to
           pass the request on because we only have on child */
        egg_dock_object_dock (dock->root, requestor, position, NULL);
        egg_dock_set_title (dock);
        
    }
    else { /* Item about to be added is root item. */
        GtkWidget *widget = GTK_WIDGET (requestor);
        
        dock->root = requestor;
        EGG_DOCK_OBJECT_SET_FLAGS (requestor, EGG_DOCK_ATTACHED);
        gtk_widget_set_parent (widget, GTK_WIDGET (dock));
        
        egg_dock_item_show_grip (EGG_DOCK_ITEM (requestor));

        /* Realize the item (create its corresponding GdkWindow) when 
           EggDock has been realized. */
        if (GTK_WIDGET_REALIZED (dock))
            gtk_widget_realize (widget);
        
        /* Map the widget if it's visible and the parent is visible and has 
           been mapped. This is done to make sure that the GdkWindow is 
           visible. */
        if (GTK_WIDGET_VISIBLE (dock) && 
            GTK_WIDGET_VISIBLE (widget)) {
            if (GTK_WIDGET_MAPPED (dock))
                gtk_widget_map (widget);
            
            /* Make the widget resize. */
            gtk_widget_queue_resize (widget);
        }
        egg_dock_set_title (dock);
    }
}
    
static gboolean
egg_dock_floating_window_delete_event_cb (GtkWidget *widget)
{
    EggDock *dock;
    
    g_return_val_if_fail (GTK_IS_WINDOW (widget), FALSE);
    
    dock = EGG_DOCK (g_object_get_data (G_OBJECT (widget), "dock"));
    if (dock->root) {
        /* this will call reduce on ourselves, hiding the window if appropiate */
        egg_dock_item_hide_item (EGG_DOCK_ITEM (dock->root));
    }

    return TRUE;
}

static void
_egg_dock_foreach_build_list (EggDockObject *object,
                              gpointer       user_data)
{
    GList **l = (GList **) user_data;

    if (EGG_IS_DOCK_ITEM (object))
        *l = g_list_prepend (*l, object);
}

static gboolean
egg_dock_reorder (EggDockObject    *object,
                  EggDockObject    *requestor,
                  EggDockPlacement  new_position,
                  GValue           *other_data)
{
    EggDock *dock = EGG_DOCK (object);
    gboolean handled = FALSE;
    
    if (dock->_priv->floating &&
        new_position == EGG_DOCK_FLOATING &&
        dock->root == requestor) {
        
        if (other_data && G_VALUE_HOLDS (other_data, GDK_TYPE_RECTANGLE)) {
            GdkRectangle *rect;

            rect = g_value_get_boxed (other_data);
            gtk_window_move (GTK_WINDOW (dock->_priv->window),
                             rect->x,
                             rect->y);
            handled = TRUE;
        }
    }
    
    return handled;
}

static gboolean 
egg_dock_child_placement (EggDockObject    *object,
                          EggDockObject    *child,
                          EggDockPlacement *placement)
{
    EggDock *dock = EGG_DOCK (object);
    gboolean retval = TRUE;
    
    if (dock->root == child) {
        if (placement) {
            if (*placement == EGG_DOCK_NONE || *placement == EGG_DOCK_FLOATING)
                *placement = EGG_DOCK_TOP;
        }
    } else 
        retval = FALSE;

    return retval;
}

static void 
egg_dock_present (EggDockObject *object,
                  EggDockObject *child)
{
    EggDock *dock = EGG_DOCK (object);

    if (dock->_priv->floating)
        gtk_window_present (GTK_WINDOW (dock->_priv->window));
}


/* ----- Public interface ----- */

GtkWidget *
egg_dock_new (void)
{
    GObject *dock;

    dock = g_object_new (EGG_TYPE_DOCK, NULL);
    EGG_DOCK_OBJECT_UNSET_FLAGS (dock, EGG_DOCK_AUTOMATIC);
    
    return GTK_WIDGET (dock);
}

GtkWidget *
egg_dock_new_from (EggDock  *original,
                   gboolean  floating)
{
    GObject *new_dock;
    
    g_return_val_if_fail (original != NULL, NULL);
    
    new_dock = g_object_new (EGG_TYPE_DOCK, 
                             "master", EGG_DOCK_OBJECT_GET_MASTER (original), 
                             "floating", floating,
                             NULL);
    EGG_DOCK_OBJECT_UNSET_FLAGS (new_dock, EGG_DOCK_AUTOMATIC);
    
    return GTK_WIDGET (new_dock);
}

void
egg_dock_add_item (EggDock          *dock,
                   EggDockItem      *item,
                   EggDockPlacement  placement)
{
    g_return_if_fail (dock != NULL);
    g_return_if_fail (item != NULL);

    if (placement == EGG_DOCK_FLOATING)
        /* Add the item to a new floating dock */
        egg_dock_add_floating_item (dock, item, 0, 0, -1, -1);

    else {
        /* Non-floating item. */
        egg_dock_object_dock (EGG_DOCK_OBJECT (dock),
                              EGG_DOCK_OBJECT (item),
                              placement, NULL);
    }
}

void
egg_dock_add_floating_item (EggDock        *dock,
                            EggDockItem    *item,
                            gint            x,
                            gint            y,
                            gint            width,
                            gint            height)
{
    EggDock *new_dock;
    
    g_return_if_fail (dock != NULL);
    g_return_if_fail (item != NULL);
    
    new_dock = EGG_DOCK (g_object_new (EGG_TYPE_DOCK, 
                                       "master", EGG_DOCK_OBJECT_GET_MASTER (dock), 
                                       "floating", TRUE,
                                       "width", width,
                                       "height", height,
                                       "floatx", x,
                                       "floaty", y,
                                       NULL));
    
    if (GTK_WIDGET_VISIBLE (dock)) {
        gtk_widget_show (GTK_WIDGET (new_dock));
        if (GTK_WIDGET_MAPPED (dock))
            gtk_widget_map (GTK_WIDGET (new_dock));
        
        /* Make the widget resize. */
        gtk_widget_queue_resize (GTK_WIDGET (new_dock));
    }

    egg_dock_add_item (EGG_DOCK (new_dock), item, EGG_DOCK_TOP);
}

EggDockItem *
egg_dock_get_item_by_name (EggDock     *dock,
                           const gchar *name)
{
    EggDockObject *found;
    
    g_return_val_if_fail (dock != NULL && name != NULL, NULL);
    
    /* proxy the call to our master */
    found = egg_dock_master_get_object (EGG_DOCK_OBJECT_GET_MASTER (dock), name);

    return (found && EGG_IS_DOCK_ITEM (found)) ? EGG_DOCK_ITEM (found) : NULL;
}

EggDockPlaceholder *
egg_dock_get_placeholder_by_name (EggDock     *dock,
                                  const gchar *name)
{
    EggDockObject *found;
    
    g_return_val_if_fail (dock != NULL && name != NULL, NULL);
    
    /* proxy the call to our master */
    found = egg_dock_master_get_object (EGG_DOCK_OBJECT_GET_MASTER (dock), name);

    return (found && EGG_IS_DOCK_PLACEHOLDER (found)) ?
        EGG_DOCK_PLACEHOLDER (found) : NULL;
}

GList *
egg_dock_get_named_items (EggDock *dock)
{
    GList *list = NULL;
    
    g_return_val_if_fail (dock != NULL, NULL);

    egg_dock_master_foreach (EGG_DOCK_OBJECT_GET_MASTER (dock),
                             (GFunc) _egg_dock_foreach_build_list, &list);

    return list;
}

EggDock *
egg_dock_object_get_toplevel (EggDockObject *object)
{
    EggDockObject *parent = object;
    
    g_return_val_if_fail (object != NULL, NULL);

    while (parent && !EGG_IS_DOCK (parent))
        parent = egg_dock_object_get_parent_object (parent);

    return parent ? EGG_DOCK (parent) : NULL;
}

void
egg_dock_xor_rect (EggDock      *dock,
                   GdkRectangle *rect)
{
    GtkWidget *widget;
    gint8      dash_list [2];

    widget = GTK_WIDGET (dock);

    if (!dock->_priv->xor_gc) {
        if (GTK_WIDGET_REALIZED (widget)) {
            GdkGCValues values;

            values.function = GDK_INVERT;
            values.subwindow_mode = GDK_INCLUDE_INFERIORS;
            dock->_priv->xor_gc = gdk_gc_new_with_values 
                (widget->window, &values, GDK_GC_FUNCTION | GDK_GC_SUBWINDOW);
        } else 
            return;
    };

    gdk_gc_set_line_attributes (dock->_priv->xor_gc, 1,
                                GDK_LINE_ON_OFF_DASH,
                                GDK_CAP_NOT_LAST,
                                GDK_JOIN_BEVEL);
    
    dash_list [0] = 1;
    dash_list [1] = 1;
    
    gdk_gc_set_dashes (dock->_priv->xor_gc, 1, dash_list, 2);

    gdk_draw_rectangle (widget->window, dock->_priv->xor_gc, 0, 
                        rect->x, rect->y,
                        rect->width, rect->height);

    gdk_gc_set_dashes (dock->_priv->xor_gc, 0, dash_list, 2);

    gdk_draw_rectangle (widget->window, dock->_priv->xor_gc, 0, 
                        rect->x + 1, rect->y + 1,
                        rect->width - 2, rect->height - 2);
}