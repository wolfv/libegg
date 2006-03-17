/* EggPageSetupUnixDialog 
 * Copyright (C) 2006 Alexander Larsson <alexl@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#include <config.h>
#include <string.h>
#include <locale.h>
#include <langinfo.h>

#include "eggintl.h"
#include <gtk/gtk.h>
#include <gtk/gtkprivate.h>

#include "eggpagesetupunixdialog.h"
#include "eggprintbackend.h"
#include "eggprinter-private.h"
#include "eggpapersize.h"

#define CUSTOM_PAPER_FILENAME ".gtk-custom-papers"

#define MM_PER_INCH 25.4
#define POINTS_PER_INCH 72


struct EggPageSetupUnixDialogPrivate
{
  GtkListStore *printer_list;
  GtkListStore *page_setup_list;
  GtkListStore *custom_paper_list;
  
  GList *print_backends;

  GtkWidget *printer_combo;
  GtkWidget *paper_size_combo;
  GtkWidget *paper_size_label;
  GtkWidget *paper_size_eventbox;
  GtkTooltips *tooltips;

  GtkWidget *portrait_radio;
  GtkWidget *landscape_radio;
  GtkWidget *reverse_portrait_radio;
  GtkWidget *reverse_landscape_radio;

  guint request_details_tag;
  
  EggPrintSettings *print_settings;

  /* Save last setup so we can re-set it after selecting manage custom sizes */
  EggPageSetup *last_setup;
};

enum {
  PROP_0
};

enum {
  PRINTER_LIST_COL_NAME,
  PRINTER_LIST_COL_PRINTER_OBJ,
  PRINTER_LIST_N_COLS
};

enum {
  PAGE_SETUP_LIST_COL_PAGE_SETUP,
  PAGE_SETUP_LIST_COL_IS_SEPARATOR,
  PAGE_SETUP_LIST_N_COLS
};

G_DEFINE_TYPE (EggPageSetupUnixDialog, egg_page_setup_unix_dialog, GTK_TYPE_DIALOG);

#define EGG_PAGE_SETUP_UNIX_DIALOG_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), EGG_TYPE_PAGE_SETUP_UNIX_DIALOG, EggPageSetupUnixDialogPrivate))

static void egg_page_setup_unix_dialog_finalize     (GObject                *object);
static void egg_page_setup_unix_dialog_set_property (GObject                *object,
						     guint                   prop_id,
						     const GValue           *value,
						     GParamSpec             *pspec);
static void egg_page_setup_unix_dialog_get_property (GObject                *object,
						     guint                   prop_id,
						     GValue                 *value,
						     GParamSpec             *pspec);
static void populate_dialog                         (EggPageSetupUnixDialog *dialog);
static void fill_paper_sizes_from_printer           (EggPageSetupUnixDialog *dialog,
						     EggPrinter             *printer);
static void run_custom_paper_dialog                 (EggPageSetupUnixDialog *dialog);

static const char * const common_paper_sizes[] = {
  "na_letter",
  "na_legal",
  "iso_a4",
  "iso_a5",
  "roc_16k",
  "iso_b5",
  "jis_b5",
  "na_number-10",
  "iso_dl",
  "jpn_chou3",
  "na_ledger",
  "iso_a3",
};

static double
to_mm (double len, EggUnit unit)
{
  switch (unit)
    {
    case EGG_UNIT_MM:
      return len;
    case EGG_UNIT_INCH:
      return len * MM_PER_INCH;
    default:
    case EGG_UNIT_PIXEL:
      g_warning ("Unsupported unit");
      /* Fall through */
    case EGG_UNIT_POINTS:
      return len * (MM_PER_INCH / POINTS_PER_INCH);
      break;
    }
}

static double
from_mm (double len, EggUnit unit)
{
  switch (unit)
    {
    case EGG_UNIT_MM:
      return len;
    case EGG_UNIT_INCH:
      return len / MM_PER_INCH;
    default:
    case EGG_UNIT_PIXEL:
      g_warning ("Unsupported unit");
      /* Fall through */
    case EGG_UNIT_POINTS:
      return len / (MM_PER_INCH / POINTS_PER_INCH);
      break;
    }
}

static EggUnit
get_default_user_units (void)
{
  /* Translate to the default units to use for presenting
   * lengths to the user. Translate to default:inch if you
   * want inches, otherwise translate to default:mm.
   * Do *not* translate it to "predefinito:mm", if it
   * it isn't default:mm or default:inch it will not work 
   */
  char *e = _("default:mm");
  
#ifdef HAVE__NL_MEASUREMENT_MEASUREMENT
  char *imperial = NULL;
  
  imperial = nl_langinfo(_NL_MEASUREMENT_MEASUREMENT);
  if (imperial && imperial[0] == 2 )
    return EGG_UNIT_INCH;  /* imperial */
  if (imperial && imperial[0] == 1 )
    return EGG_UNIT_MM;  /* metric */
#endif
  
  if (strcmp (e, "default:inch")==0)
    return EGG_UNIT_INCH;
  else if (strcmp (e, "default:mm"))
    g_warning ("Whoever translated default:mm did so wrongly.\n");
  return EGG_UNIT_MM;
}

static char *
custom_paper_get_filename (void)
{
  char *filename;

  filename = g_build_filename (g_get_home_dir (), 
			       CUSTOM_PAPER_FILENAME, NULL);
  g_assert (filename != NULL);
  return filename;
}

static gboolean
scan_double (char **text, double *val, gboolean last)
{
  char *p, *e;

  p = *text;
  
  *val = g_ascii_strtod (p, &e);
  if (p == e)
    return FALSE;

  p = e;
  if (!last)
    {
      while (g_ascii_isspace(*p))
	p++;
      if (*p++ != ',')
	return FALSE;
    }
  *text = p;
  return TRUE;
}

static void
load_custom_papers (GtkListStore *store)
{
  char *filename;
  gchar *contents;
  
  filename = custom_paper_get_filename ();

  if (g_file_get_contents (filename, &contents, NULL, NULL))
    {
      gchar **lines = g_strsplit (contents, "\n", -1);
      double w, h, top, bottom, left, right;
      EggPaperSize *paper_size;
      EggPageSetup *page_setup;
      char *name, *p;
      GtkTreeIter iter;
      int i;

      for (i = 0; lines[i]; i++)
	{
	  name = lines[i];
	  p = strchr(lines[i], ':');
	  if (p == NULL)
	    continue;
	  *p++ = 0;

	  while (g_ascii_isspace(*p))
	    p++;
	  
	  if (!scan_double (&p, &w, FALSE))
	    continue;
	  if (!scan_double (&p, &h, FALSE))
	    continue;
	  if (!scan_double (&p, &top, FALSE))
	    continue;
	  if (!scan_double (&p, &bottom, FALSE))
	    continue;
	  if (!scan_double (&p, &left, FALSE))
	    continue;
	  if (!scan_double (&p, &right, TRUE))
	    continue;

	  page_setup = egg_page_setup_new ();
	  paper_size = egg_paper_size_new_custom (name, name, w, h, EGG_UNIT_MM);
	  egg_page_setup_set_paper_size (page_setup, paper_size);
	  egg_paper_size_free (paper_size);

	  egg_page_setup_set_top_margin (page_setup, top, EGG_UNIT_MM);
	  egg_page_setup_set_bottom_margin (page_setup, bottom, EGG_UNIT_MM);
	  egg_page_setup_set_left_margin (page_setup, left, EGG_UNIT_MM);
	  egg_page_setup_set_right_margin (page_setup, right, EGG_UNIT_MM);

	  gtk_list_store_append (store, &iter);
	  gtk_list_store_set (store, &iter,
			      0, page_setup, 
			      -1);

	  g_object_unref (page_setup);
	}
      
      g_free (contents);
      g_strfreev (lines);
    }
  g_free (filename);
}

static void
save_custom_papers (GtkListStore *store)
{
  GtkTreeModel *model = GTK_TREE_MODEL (store);
  GtkTreeIter iter;
  GString *string;
  char *filename;

  string = g_string_new ("");
  
  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      do
	{
	  EggPaperSize *paper_size;
	  EggPageSetup *page_setup;
	  char buffer[G_ASCII_DTOSTR_BUF_SIZE];
	  
	  gtk_tree_model_get (model, &iter, 0, &page_setup, -1);

	  paper_size = egg_page_setup_get_paper_size (page_setup);
	  g_string_append_printf (string, "%s: ", egg_paper_size_get_name (paper_size));
	  
	  g_ascii_dtostr (buffer, G_ASCII_DTOSTR_BUF_SIZE,
			  egg_page_setup_get_paper_width (page_setup, EGG_UNIT_MM));
	  g_string_append (string, buffer);
	  g_string_append (string, ", ");
	  g_ascii_dtostr (buffer, G_ASCII_DTOSTR_BUF_SIZE,
			  egg_page_setup_get_paper_height (page_setup, EGG_UNIT_MM));
	  g_string_append (string, buffer);
	  g_string_append (string, ", ");
	  g_ascii_dtostr (buffer, G_ASCII_DTOSTR_BUF_SIZE,
			  egg_page_setup_get_top_margin (page_setup, EGG_UNIT_MM));
	  g_string_append (string, buffer);
	  g_string_append (string, ", ");
	  g_ascii_dtostr (buffer, G_ASCII_DTOSTR_BUF_SIZE, 
			  egg_page_setup_get_bottom_margin (page_setup, EGG_UNIT_MM));
	  g_string_append (string, buffer);
	  g_string_append (string, ", ");
	  g_ascii_dtostr (buffer, G_ASCII_DTOSTR_BUF_SIZE,
			  egg_page_setup_get_left_margin (page_setup, EGG_UNIT_MM));
	  g_string_append (string, buffer);
	  g_string_append (string, ", ");
	  g_ascii_dtostr (buffer, G_ASCII_DTOSTR_BUF_SIZE,
			  egg_page_setup_get_right_margin (page_setup, EGG_UNIT_MM));
	  g_string_append (string, buffer);

	  g_string_append (string, "\n");
	  
	} while (gtk_tree_model_iter_next (model, &iter));
    }

  filename = custom_paper_get_filename ();
  g_file_set_contents (filename, string->str, -1, NULL);
  g_free (filename);
  
  g_string_free (string, TRUE);
}

static void
egg_page_setup_unix_dialog_class_init (EggPageSetupUnixDialogClass *class)
{
  GObjectClass *object_class;
  GtkWidgetClass *widget_class;

  object_class = (GObjectClass *) class;
  widget_class = (GtkWidgetClass *) class;

  object_class->finalize = egg_page_setup_unix_dialog_finalize;
  object_class->set_property = egg_page_setup_unix_dialog_set_property;
  object_class->get_property = egg_page_setup_unix_dialog_get_property;

  g_type_class_add_private (class, sizeof (EggPageSetupUnixDialogPrivate));  
}

static void
egg_page_setup_unix_dialog_init (EggPageSetupUnixDialog *dialog)
{
  GtkTreeIter iter;
  
  dialog->priv = EGG_PAGE_SETUP_UNIX_DIALOG_GET_PRIVATE (dialog); 
  dialog->priv->print_backends = NULL;

  dialog->priv->printer_list = gtk_list_store_new (PRINTER_LIST_N_COLS,
						   G_TYPE_STRING, 
						   G_TYPE_OBJECT);

  gtk_list_store_append (dialog->priv->printer_list, &iter);
  gtk_list_store_set (dialog->priv->printer_list, &iter,
                      PRINTER_LIST_COL_NAME, _("<b>Any Printer</b>\nFor portable documents"),
                      PRINTER_LIST_COL_PRINTER_OBJ, NULL,
                      -1);
  
  dialog->priv->page_setup_list = gtk_list_store_new (PAGE_SETUP_LIST_N_COLS,
						      G_TYPE_OBJECT,
						      G_TYPE_BOOLEAN);

  dialog->priv->custom_paper_list = gtk_list_store_new (1, G_TYPE_OBJECT);
  load_custom_papers (dialog->priv->custom_paper_list);

  populate_dialog (dialog);
}

static void
egg_page_setup_unix_dialog_finalize (GObject *object)
{
  EggPageSetupUnixDialog *dialog = EGG_PAGE_SETUP_UNIX_DIALOG (object);
  
  g_return_if_fail (object != NULL);

  if (dialog->priv->request_details_tag)
    {
      g_source_remove (dialog->priv->request_details_tag);
      dialog->priv->request_details_tag = 0;
    }
  
  if (dialog->priv->printer_list)
    {
      g_object_unref (dialog->priv->printer_list);
      dialog->priv->printer_list = NULL;
    }

  if (dialog->priv->page_setup_list)
    {
      g_object_unref (dialog->priv->page_setup_list);
      dialog->priv->page_setup_list = NULL;
    }

  if (dialog->priv->print_settings)
    {
      g_object_unref (dialog->priv->print_settings);
      dialog->priv->print_settings = NULL;
    }
  
  if (G_OBJECT_CLASS (egg_page_setup_unix_dialog_parent_class)->finalize)
    G_OBJECT_CLASS (egg_page_setup_unix_dialog_parent_class)->finalize (object);
}

static void
_printer_added_cb (EggPrintBackend *backend, 
                   EggPrinter *printer, 
		   EggPageSetupUnixDialog *dialog)
{
  GtkTreeIter iter;
  char *str;

  if (egg_printer_is_virtual (printer))
    return;
  
  str = g_strdup_printf ("<b>%s</b>\n%s",
			 egg_printer_get_name (printer),
			 egg_printer_get_location (printer));
  
  gtk_list_store_append (dialog->priv->printer_list, &iter);
  gtk_list_store_set (dialog->priv->printer_list, &iter,
                      PRINTER_LIST_COL_NAME, str,
                      PRINTER_LIST_COL_PRINTER_OBJ, printer,
                      -1);
  g_free (str);
}

static void
_printer_removed_cb (EggPrintBackend *backend, 
                   EggPrinter *printer, 
		   EggPageSetupUnixDialog *impl)
{
}


static void
_printer_status_cb (EggPrintBackend *backend, 
                   EggPrinter *printer, 
		   EggPageSetupUnixDialog *impl)
{
}

static void
_printer_list_initialize (EggPageSetupUnixDialog *impl,
                          EggPrintBackend *print_backend)
{
  GList *list, *node;
  
  g_return_if_fail (print_backend != NULL);

  g_signal_connect (print_backend, 
                    "printer-added", 
		    (GCallback) _printer_added_cb, 
		    impl);

  g_signal_connect (print_backend, 
                    "printer-removed", 
		    (GCallback) _printer_removed_cb, 
		    impl);

  g_signal_connect (print_backend, 
                    "printer-status-changed", 
		    (GCallback) _printer_status_cb, 
		    impl);

  list = egg_print_backend_get_printer_list (print_backend);

  node = list;
  while (node != NULL)
    {
      _printer_added_cb (print_backend, node->data, impl);
      node = node->next;
    }

  g_list_free (list);
  
}

static void
_load_print_backends (EggPageSetupUnixDialog *dialog)
{
  GList *node;

  if (g_module_supported ())
    dialog->priv->print_backends = egg_print_backend_load_modules ();

  for (node = dialog->priv->print_backends; node != NULL; node = node->next)
    _printer_list_initialize (dialog, EGG_PRINT_BACKEND (node->data));
}

static void
egg_page_setup_unix_dialog_set_property (GObject      *object,
					 guint         prop_id,
					 const GValue *value,
					 GParamSpec   *pspec)

{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
egg_page_setup_unix_dialog_get_property (GObject    *object,
					 guint       prop_id,
					 GValue     *value,
					 GParamSpec *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static gboolean
paper_size_row_is_separator (GtkTreeModel *model,
			     GtkTreeIter  *iter,
			     gpointer      data)
{
  gboolean separator;

  gtk_tree_model_get (model, iter, PAGE_SETUP_LIST_COL_IS_SEPARATOR, &separator, -1);
  return separator;
}

static EggPageSetup *
get_current_page_setup (EggPageSetupUnixDialog *dialog)
{
  EggPageSetup *current_page_setup;
  GtkComboBox *combo_box;
  GtkTreeIter iter;

  current_page_setup = NULL;
  
  combo_box = GTK_COMBO_BOX (dialog->priv->paper_size_combo);
  if (gtk_combo_box_get_active_iter (combo_box, &iter))
    gtk_tree_model_get (GTK_TREE_MODEL (dialog->priv->page_setup_list), &iter,
			PAGE_SETUP_LIST_COL_PAGE_SETUP, &current_page_setup, -1);

  if (current_page_setup)
    return current_page_setup;

  /* No selected page size, return the default one.
   * This is used to set the first page setup when the dialog is created
   * as there is no selection on the first printer_changed.
   */ 
  return egg_page_setup_new ();
}

static gboolean
page_setup_is_equal (EggPageSetup *a, EggPageSetup *b)
{
  return
    egg_paper_size_is_equal (egg_page_setup_get_paper_size (a),
			     egg_page_setup_get_paper_size (b)) &&
    egg_page_setup_get_top_margin (a, EGG_UNIT_MM) == egg_page_setup_get_top_margin (b, EGG_UNIT_MM) &&
    egg_page_setup_get_bottom_margin (a, EGG_UNIT_MM) == egg_page_setup_get_bottom_margin (b, EGG_UNIT_MM) &&
    egg_page_setup_get_left_margin (a, EGG_UNIT_MM) == egg_page_setup_get_left_margin (b, EGG_UNIT_MM) &&
    egg_page_setup_get_right_margin (a, EGG_UNIT_MM) == egg_page_setup_get_right_margin (b, EGG_UNIT_MM);
}

static gboolean
page_setup_is_same_size (EggPageSetup *a, EggPageSetup *b)
{
  return egg_paper_size_is_equal (egg_page_setup_get_paper_size (a),
				  egg_page_setup_get_paper_size (b));
}

static gboolean
set_paper_size (EggPageSetupUnixDialog *dialog,
		EggPageSetup *page_setup,
		gboolean size_only,
		gboolean add_item)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  EggPageSetup *list_page_setup;

  model = GTK_TREE_MODEL (dialog->priv->page_setup_list);

  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      do
	{
	  gtk_tree_model_get (GTK_TREE_MODEL (dialog->priv->page_setup_list), &iter,
			      PAGE_SETUP_LIST_COL_PAGE_SETUP, &list_page_setup, -1);
	  if (list_page_setup == NULL)
	    continue;
	  
	  if ((size_only && page_setup_is_same_size (page_setup, list_page_setup)) ||
	      (!size_only && page_setup_is_equal (page_setup, list_page_setup)))
	    {
	      gtk_combo_box_set_active_iter (GTK_COMBO_BOX (dialog->priv->paper_size_combo),
					     &iter);
	      g_object_unref (list_page_setup);
	      return TRUE;
	    }
	      
	  g_object_unref (list_page_setup);
	  
	} while (gtk_tree_model_iter_next (model, &iter));
    }

  if (add_item)
    {
      gtk_list_store_append (dialog->priv->page_setup_list, &iter);
      gtk_list_store_set (dialog->priv->page_setup_list, &iter,
			  PAGE_SETUP_LIST_COL_IS_SEPARATOR, TRUE,
			  -1);
      gtk_list_store_append (dialog->priv->page_setup_list, &iter);
      gtk_list_store_set (dialog->priv->page_setup_list, &iter,
			  PAGE_SETUP_LIST_COL_PAGE_SETUP, page_setup,
			  -1);
      gtk_combo_box_set_active_iter (GTK_COMBO_BOX (dialog->priv->paper_size_combo),
				     &iter);
      return TRUE;
    }

  return FALSE;
}

static void
fill_custom_paper_sizes (EggPageSetupUnixDialog *dialog)
{
  GtkTreeIter iter, paper_iter;
  GtkTreeModel *model;

  model = GTK_TREE_MODEL (dialog->priv->custom_paper_list);
  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      gtk_list_store_append (dialog->priv->page_setup_list, &paper_iter);
      gtk_list_store_set (dialog->priv->page_setup_list, &paper_iter,
			  PAGE_SETUP_LIST_COL_IS_SEPARATOR, TRUE,
			  -1);
      do
	{
	  EggPageSetup *page_setup;
	  gtk_tree_model_get (model, &iter, 0, &page_setup, -1);

	  gtk_list_store_append (dialog->priv->page_setup_list, &paper_iter);
	  gtk_list_store_set (dialog->priv->page_setup_list, &paper_iter,
			      PAGE_SETUP_LIST_COL_PAGE_SETUP, page_setup,
			      -1);

	  g_object_unref (page_setup);
	} while (gtk_tree_model_iter_next (model, &iter));
    }
  
  gtk_list_store_append (dialog->priv->page_setup_list, &paper_iter);
  gtk_list_store_set (dialog->priv->page_setup_list, &paper_iter,
                      PAGE_SETUP_LIST_COL_IS_SEPARATOR, TRUE,
                      -1);
  gtk_list_store_append (dialog->priv->page_setup_list, &paper_iter);
  gtk_list_store_set (dialog->priv->page_setup_list, &paper_iter,
                      PAGE_SETUP_LIST_COL_PAGE_SETUP, NULL,
                      -1);
}

static void
fill_paper_sizes_from_printer (EggPageSetupUnixDialog *dialog,
			       EggPrinter *printer)
{
  GList *list, *l;
  EggPageSetup *current_page_setup, *page_setup;
  EggPaperSize *paper_size;
  GtkTreeIter iter;
  int i;

  current_page_setup = get_current_page_setup (dialog);
  
  gtk_list_store_clear (dialog->priv->page_setup_list);

  if (printer == NULL)
    {
      for (i = 0; i < G_N_ELEMENTS (common_paper_sizes); i++)
	{
	  page_setup = egg_page_setup_new ();
	  paper_size = egg_paper_size_new (common_paper_sizes[i]);
	  egg_page_setup_set_paper_size_and_default_margins (page_setup, paper_size);
	  egg_paper_size_free (paper_size);
	  
	  gtk_list_store_append (dialog->priv->page_setup_list, &iter);
	  gtk_list_store_set (dialog->priv->page_setup_list, &iter,
			      PAGE_SETUP_LIST_COL_PAGE_SETUP, page_setup,
			      -1);
	  g_object_unref (page_setup);
	}
    }
  else
    {
      list = _egg_printer_list_papers (printer);
      /* TODO: We should really sort this list so interesting size
	 are at the top */
      for (l = list; l != NULL; l = l->next)
	{
	  page_setup = l->data;
	  gtk_list_store_append (dialog->priv->page_setup_list, &iter);
	  gtk_list_store_set (dialog->priv->page_setup_list, &iter,
			      PAGE_SETUP_LIST_COL_PAGE_SETUP, page_setup,
			      -1);
	  g_object_unref (page_setup);
	}
      g_list_free (list);
    }

  fill_custom_paper_sizes (dialog);
  
  if (!set_paper_size (dialog, current_page_setup, FALSE, FALSE))
    set_paper_size (dialog, current_page_setup, TRUE, TRUE);
  
  if (current_page_setup)
    g_object_unref (current_page_setup);
}

static void
printer_changed_finished_callback (EggPrinter *printer,
				   gboolean success,
				   EggPageSetupUnixDialog *dialog)
{
  dialog->priv->request_details_tag = 0;
  
  if (success)
    fill_paper_sizes_from_printer (dialog, printer);

}

static void
printer_changed_callback (GtkComboBox *combo_box,
			  EggPageSetupUnixDialog *dialog)
{
  EggPrinter *printer;
  GtkTreeIter iter;

  if (dialog->priv->request_details_tag)
    {
      g_source_remove (dialog->priv->request_details_tag);
      dialog->priv->request_details_tag = 0;
    }
  
  if (gtk_combo_box_get_active_iter (combo_box, &iter))
    {
      gtk_tree_model_get (gtk_combo_box_get_model (combo_box), &iter,
			  PRINTER_LIST_COL_PRINTER_OBJ, &printer, -1);

      if (printer == NULL || _egg_printer_has_details (printer))
	fill_paper_sizes_from_printer (dialog, printer);
      else
	{
	  dialog->priv->request_details_tag =
	    g_signal_connect (printer, "details-acquired",
			      G_CALLBACK (printer_changed_finished_callback), dialog);
	  _egg_printer_request_details (printer);

	}

      if (printer)
	g_object_unref (printer);

      /* TODO: Add format-for-printer to print_settings */
    }
}

/* We do this munging because we don't want to show zero digits
   after the decimal point, and not to many such digits if they
   are nonzero. I wish printf let you specify max precision for %f... */
static char *
double_to_string (double d, EggUnit unit)
{
  char *val, *p;
  struct lconv *locale_data;
  const char *decimal_point;
  int decimal_point_len;

  locale_data = localeconv ();
  decimal_point = locale_data->decimal_point;
  decimal_point_len = strlen (decimal_point);
  
  /* Max two decimal digits for inch, max one for mm */
  if (unit == EGG_UNIT_INCH)
    val = g_strdup_printf ("%.2f", d);
  else
    val = g_strdup_printf ("%.1f", d);

  if (strstr (val, decimal_point))
    {
      p = val + strlen (val) - 1;
      while (*p == '0')
        p--;
      if (p - val + 1 >= decimal_point_len &&
	  strncmp (p - (decimal_point_len - 1), decimal_point, decimal_point_len) == 0)
        p -= decimal_point_len;
      p[1] = '\0';
    }

  return val;
}

static void
paper_size_changed (GtkComboBox *combo_box, EggPageSetupUnixDialog *dialog)
{
  GtkTreeIter iter;
  EggPageSetup *page_setup, *last_page_setup;
  EggUnit unit;
  char *str, *w, *h;
  char *top, *bottom, *left, *right;
  GtkLabel *label;
  const char *unit_str;

  label = GTK_LABEL (dialog->priv->paper_size_label);
   
  if (gtk_combo_box_get_active_iter (combo_box, &iter))
    {
      gtk_tree_model_get (gtk_combo_box_get_model (combo_box),
			  &iter, PAGE_SETUP_LIST_COL_PAGE_SETUP, &page_setup, -1);

      if (page_setup == NULL)
	{
	  run_custom_paper_dialog (dialog);

	  /* Save current last_setup as it is changed by updating the list */
	  last_page_setup = NULL;
	  if (dialog->priv->last_setup)
	    last_page_setup = g_object_ref (dialog->priv->last_setup);

	  /* Update printer page list */
	  printer_changed_callback (GTK_COMBO_BOX (dialog->priv->printer_combo), dialog);

	  /* Change from "manage" menu item to last value */
	  if (last_page_setup == NULL)
	    last_page_setup = egg_page_setup_new (); /* "good" default */
	  set_paper_size (dialog, last_page_setup, FALSE, TRUE);
	  g_object_unref (last_page_setup);
	  
	  return;
	}

      if (dialog->priv->last_setup)
	g_object_unref (dialog->priv->last_setup);

      dialog->priv->last_setup = g_object_ref (page_setup);
      
      unit = get_default_user_units ();

      if (unit == EGG_UNIT_MM)
	unit_str = _("mm");
      else
	unit_str = _("inch");
	

      w = double_to_string (egg_page_setup_get_paper_width (page_setup, unit),
			    unit);
      h = double_to_string (egg_page_setup_get_paper_height (page_setup, unit),
			    unit);
      str = g_strdup_printf ("%s x %s %s", w, h, unit_str);
      g_free (w);
      g_free (h);
      
      gtk_label_set_text (label, str);
      g_free (str);

      top = double_to_string (egg_page_setup_get_top_margin (page_setup, unit), unit);
      bottom = double_to_string (egg_page_setup_get_bottom_margin (page_setup, unit), unit);
      left = double_to_string (egg_page_setup_get_left_margin (page_setup, unit), unit);
      right = double_to_string (egg_page_setup_get_right_margin (page_setup, unit), unit);

      str = g_strdup_printf (_("Margins:\n"
			       " Left: %s %s\n"
			       " Right: %s %s\n"
			       " Top: %s %s\n"
			       " Bottom: %s %s"
			       ),
			     left, unit_str,
			     right, unit_str,
			     top, unit_str,
			     bottom, unit_str);
      g_free (top);
      g_free (bottom);
      g_free (left);
      g_free (right);
      
      gtk_tooltips_set_tip (GTK_TOOLTIPS (dialog->priv->tooltips),
			    dialog->priv->paper_size_eventbox, str, NULL);
      g_free (str);
      
      g_object_unref (page_setup);
    }
  else
    {
      gtk_label_set_text (label, "");
      gtk_tooltips_set_tip (GTK_TOOLTIPS (dialog->priv->tooltips),
			    dialog->priv->paper_size_eventbox, NULL, NULL);
      if (dialog->priv->last_setup)
	g_object_unref (dialog->priv->last_setup);
      dialog->priv->last_setup = NULL;
    }
}

static void
page_name_func (GtkCellLayout   *cell_layout,
		GtkCellRenderer *cell,
		GtkTreeModel    *tree_model,
		GtkTreeIter     *iter,
		gpointer         data)
{
  EggPageSetup *page_setup;
  EggPaperSize *paper_size;
  
  gtk_tree_model_get (tree_model, iter,
		      PAGE_SETUP_LIST_COL_PAGE_SETUP, &page_setup, -1);
  if (page_setup)
    {
      paper_size = egg_page_setup_get_paper_size (page_setup);
      g_object_set (cell, "text",  egg_paper_size_get_display_name (paper_size), NULL);
      g_object_unref (page_setup);
    }
  else
    g_object_set (cell, "text",  _("Manage Custom Sizes..."), NULL);
      
}

 
static void
populate_dialog (EggPageSetupUnixDialog *dialog)
{
  EggPageSetupUnixDialogPrivate *priv;
  GtkWidget *table, *label, *combo, *radio_button, *ebox;
  GtkCellRenderer *cell;
  
  g_return_if_fail (EGG_IS_PAGE_SETUP_UNIX_DIALOG (dialog));
  
  priv = dialog->priv;

  table = gtk_table_new (3, 5, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
		      table, TRUE, TRUE, 6);
  gtk_widget_show (table);

  label = gtk_label_new_with_mnemonic (_("_Format for:"));
  gtk_table_attach (GTK_TABLE (table), label,
		    0, 1, 0, 1,
		    GTK_FILL, 0, 0, 0);
  gtk_widget_show (label);

  combo = gtk_combo_box_new_with_model (GTK_TREE_MODEL (dialog->priv->printer_list));
  dialog->priv->printer_combo = combo;
  
  cell = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), cell, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo), cell,
                                  "markup", PRINTER_LIST_COL_NAME,
                                  NULL);

  gtk_table_attach (GTK_TABLE (table), combo,
		    1, 3, 0, 1,
		    GTK_FILL | GTK_EXPAND, 0, 0, 0);
  gtk_widget_show (combo);

  label = gtk_label_new_with_mnemonic (_("_Paper size:"));
  gtk_table_attach (GTK_TABLE (table), label,
		    0, 1, 1, 2,
		    GTK_FILL, 0, 0, 0);
  gtk_widget_show (label);

  combo = gtk_combo_box_new_with_model (GTK_TREE_MODEL (dialog->priv->page_setup_list));
  dialog->priv->paper_size_combo = combo;
  gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (combo), 
					paper_size_row_is_separator, NULL, NULL);
  
  cell = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), cell, TRUE);
  gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (combo), cell,
				      page_name_func, NULL, NULL);

  gtk_table_attach (GTK_TABLE (table), combo,
		    1, 3, 1, 2,
		    GTK_FILL | GTK_EXPAND, 0, 0, 0);
  gtk_widget_show (combo);

  gtk_table_set_row_spacing (GTK_TABLE (table), 1, 0);

  ebox = gtk_event_box_new ();
  dialog->priv->paper_size_eventbox = ebox;
  gtk_event_box_set_visible_window (GTK_EVENT_BOX (ebox), FALSE);
  gtk_table_attach (GTK_TABLE (table), ebox,
		    1, 3, 2, 3,
		    GTK_FILL, 0, 0, 0);
  gtk_widget_show (ebox);
  
  label = gtk_label_new_with_mnemonic ("");
  dialog->priv->paper_size_label = label;
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_misc_set_padding (GTK_MISC (label), 12, 4);
  gtk_container_add (GTK_CONTAINER (ebox), label);
  gtk_widget_show (label);

  label = gtk_label_new_with_mnemonic (_("_Orientation:"));
  gtk_table_attach (GTK_TABLE (table), label,
		    0, 1, 3, 4,
		    GTK_FILL, 0, 0, 0);
  gtk_widget_show (label);

  radio_button = gtk_radio_button_new_with_label (NULL, "port");
  dialog->priv->portrait_radio = radio_button;
  gtk_table_attach (GTK_TABLE (table), radio_button,
		    1, 2, 3, 4,
		    GTK_FILL, 0, 0, 0);
  gtk_widget_show (radio_button);

  radio_button = gtk_radio_button_new_with_label (gtk_radio_button_get_group (GTK_RADIO_BUTTON(radio_button)),
						  "land");
  dialog->priv->landscape_radio = radio_button;
  gtk_table_attach (GTK_TABLE (table), radio_button,
		    2, 3, 3, 4,
		    GTK_FILL, 0, 0, 0);
  gtk_widget_show (radio_button);

  gtk_table_set_row_spacing (GTK_TABLE (table), 3, 0);
  
  radio_button = gtk_radio_button_new_with_label (gtk_radio_button_get_group (GTK_RADIO_BUTTON(radio_button)),
						  "rport");
  dialog->priv->reverse_portrait_radio = radio_button;
  gtk_table_attach (GTK_TABLE (table), radio_button,
		    1, 2, 4, 5,
		    GTK_FILL, 0, 0, 0);
  gtk_widget_show (radio_button);

  radio_button = gtk_radio_button_new_with_label (gtk_radio_button_get_group (GTK_RADIO_BUTTON(radio_button)),
						  "rland");
  dialog->priv->reverse_landscape_radio = radio_button;
  gtk_table_attach (GTK_TABLE (table), radio_button,
		    2, 3, 4, 5,
		    GTK_FILL, 0, 0, 0);
  gtk_widget_show (radio_button);

  dialog->priv->tooltips = gtk_tooltips_new ();

  g_signal_connect (dialog->priv->paper_size_combo, "changed", G_CALLBACK (paper_size_changed), dialog);
  g_signal_connect (dialog->priv->printer_combo, "changed", G_CALLBACK (printer_changed_callback), dialog);
  gtk_combo_box_set_active (GTK_COMBO_BOX (dialog->priv->printer_combo), 0);

  _load_print_backends (dialog);
}

GtkWidget *
egg_page_setup_unix_dialog_new (const gchar *title,
				GtkWindow *parent)
{
  GtkWidget *result;

  if (title == NULL)
    title = _("Page Setup");
  
  result = g_object_new (EGG_TYPE_PAGE_SETUP_UNIX_DIALOG,
                         "title", title,
			 "has-separator", FALSE,
                         NULL);

  if (parent)
    gtk_window_set_transient_for (GTK_WINDOW (result), parent);

  gtk_dialog_add_buttons (GTK_DIALOG (result), 
                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                          GTK_STOCK_OK, GTK_RESPONSE_OK,
                          NULL);

  return result;
}

static EggPageOrientation
get_orientation (EggPageSetupUnixDialog *dialog)
{
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dialog->priv->portrait_radio)))
    return EGG_PAGE_ORIENTATION_PORTRAIT;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dialog->priv->landscape_radio)))
    return EGG_PAGE_ORIENTATION_LANDSCAPE;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dialog->priv->reverse_portrait_radio)))
    return EGG_PAGE_ORIENTATION_REVERSE_PORTRAIT;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dialog->priv->reverse_landscape_radio)))
    return EGG_PAGE_ORIENTATION_REVERSE_LANDSCAPE;
  return EGG_PAGE_ORIENTATION_PORTRAIT;
}

static void
set_orientation (EggPageSetupUnixDialog *dialog, EggPageOrientation orientation)
{
  switch (orientation)
    {
    case EGG_PAGE_ORIENTATION_PORTRAIT:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->portrait_radio), TRUE);
      break;
    case EGG_PAGE_ORIENTATION_LANDSCAPE:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->landscape_radio), TRUE);
      break;
    case EGG_PAGE_ORIENTATION_REVERSE_PORTRAIT:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->reverse_portrait_radio), TRUE);
      break;
    case EGG_PAGE_ORIENTATION_REVERSE_LANDSCAPE:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->reverse_landscape_radio), TRUE);
      break;
    }
}


void
egg_page_setup_unix_dialog_set_page_setup (EggPageSetupUnixDialog *dialog,
					   EggPageSetup           *page_setup)
{
  if (page_setup)
    {
      set_paper_size (dialog, page_setup, FALSE, TRUE);
      set_orientation (dialog, egg_page_setup_get_orientation (page_setup));
    }
}

EggPageSetup *
egg_page_setup_unix_dialog_get_page_setup (EggPageSetupUnixDialog *dialog)
{
  EggPageSetup *page_setup;
  
  page_setup = get_current_page_setup (dialog);
  if (page_setup == NULL)
    page_setup = egg_page_setup_new ();

  egg_page_setup_set_orientation (page_setup, get_orientation (dialog));

  return page_setup;
}

void
egg_page_setup_unix_dialog_set_print_settings (EggPageSetupUnixDialog *dialog,
					       EggPrintSettings       *print_settings)
{
  if (dialog->priv->print_settings)
    g_object_unref (dialog->priv->print_settings);

  dialog->priv->print_settings = print_settings;

  if (print_settings)
    g_object_ref (print_settings);

  /* TODO: Set format-for-printer if set. Delayed... */
}

EggPrintSettings *
egg_page_setup_unix_dialog_get_print_settings (EggPageSetupUnixDialog *dialog)
{
  return dialog->priv->print_settings;
}

static GtkWidget *
wrap_in_frame (const char *label, GtkWidget *child)
{
  GtkWidget *frame, *alignment;
  
  frame = gtk_frame_new (label);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
  
  alignment = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
  gtk_alignment_set_padding (GTK_ALIGNMENT (alignment),
			     0, 0, 12, 0);
  gtk_container_add (GTK_CONTAINER (frame), alignment);

  gtk_container_add (GTK_CONTAINER (alignment), child);

  gtk_widget_show (frame);
  gtk_widget_show (alignment);
  
  return frame;
}

typedef struct {
  EggUnit display_unit;
  GtkWidget *spin_button;
} UnitWidget;

typedef struct {
  EggPageSetupUnixDialog *dialog;
  GtkWidget *treeview;
  GtkTreeViewColumn *text_column;
  gboolean non_user_change;
  GtkWidget *printer_combo;
  GtkWidget *width_widget;
  GtkWidget *height_widget;
  GtkWidget *top_widget;
  GtkWidget *bottom_widget;
  GtkWidget *left_widget;
  GtkWidget *right_widget;
  guint request_details_tag;
} CustomPaperDialog;

static void unit_widget_changed (CustomPaperDialog *data);

static GtkWidget *
new_unit_widget (CustomPaperDialog *dialog, EggUnit unit)
{
  GtkWidget *hbox, *button, *label;
  UnitWidget *data;

  data = g_new0 (UnitWidget, 1);
  data->display_unit = unit;
  
  hbox = gtk_hbox_new (FALSE, 0);

  button = gtk_spin_button_new_with_range (0.0, 9999.0, 1);
  if (unit == EGG_UNIT_INCH)
    gtk_spin_button_set_digits (GTK_SPIN_BUTTON (button), 2);
  else
    gtk_spin_button_set_digits (GTK_SPIN_BUTTON (button), 1);

  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
  gtk_widget_show (button);

  data->spin_button = button;

  g_signal_connect_swapped (button, "value_changed",
			    G_CALLBACK (unit_widget_changed), dialog);
  
  if (unit == EGG_UNIT_INCH)
    label = gtk_label_new (_(" inch"));
  else
    label = gtk_label_new (_(" mm"));

  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  g_object_set_data_full (G_OBJECT (hbox), "unit-data", data, g_free);
  
  return hbox;
}

static double
unit_widget_get (GtkWidget *unit_widget)
{
  UnitWidget *data = g_object_get_data (G_OBJECT (unit_widget), "unit-data");
  return to_mm (gtk_spin_button_get_value (GTK_SPIN_BUTTON (data->spin_button)),
		data->display_unit);
}

static void
unit_widget_set (GtkWidget *unit_widget, double val)
{
  UnitWidget *data = g_object_get_data (G_OBJECT (unit_widget), "unit-data");
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (data->spin_button),
			     from_mm (val, data->display_unit));

}

static void
unit_widget_set_sensitive (GtkWidget *unit_widget, gboolean sensitive)
{
  UnitWidget *data = g_object_get_data (G_OBJECT (unit_widget), "unit-data");
  gtk_widget_set_sensitive (data->spin_button, sensitive);
}

static void
custom_paper_printer_data_func (GtkCellLayout   *cell_layout,
				GtkCellRenderer *cell,
				GtkTreeModel    *tree_model,
				GtkTreeIter     *iter,
				gpointer         data)
{
  EggPrinter *printer;
  gtk_tree_model_get (tree_model, iter,
		      PRINTER_LIST_COL_PRINTER_OBJ, &printer, -1);

  if (printer)
    g_object_set (cell, "text",  egg_printer_get_name (printer), NULL);
  else
    g_object_set (cell, "text",  _("Margins from Printer..."), NULL);
  
  if (printer)
    g_object_unref (printer);
}

static void
update_custom_widgets_from_list (CustomPaperDialog *data)
{
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter iter;
  EggPageSetup *page_setup;
  
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (data->treeview));
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview));

  data->non_user_change = TRUE;
  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
      gtk_tree_model_get (model, &iter, 0, &page_setup, -1);
      
      unit_widget_set (data->width_widget,
		       egg_page_setup_get_paper_width (page_setup, EGG_UNIT_MM));
      unit_widget_set (data->height_widget, 
		       egg_page_setup_get_paper_height (page_setup, EGG_UNIT_MM));
      unit_widget_set (data->top_widget,
		       egg_page_setup_get_top_margin (page_setup, EGG_UNIT_MM));
      unit_widget_set (data->bottom_widget, 
		       egg_page_setup_get_bottom_margin (page_setup, EGG_UNIT_MM));
      unit_widget_set (data->left_widget,
		       egg_page_setup_get_left_margin (page_setup, EGG_UNIT_MM));
      unit_widget_set (data->right_widget,
		       egg_page_setup_get_right_margin (page_setup, EGG_UNIT_MM));
      
      unit_widget_set_sensitive (data->width_widget, TRUE);
      unit_widget_set_sensitive (data->height_widget, TRUE);
      unit_widget_set_sensitive (data->top_widget, TRUE);
      unit_widget_set_sensitive (data->bottom_widget, TRUE);
      unit_widget_set_sensitive (data->left_widget, TRUE);
      unit_widget_set_sensitive (data->right_widget, TRUE);
      gtk_widget_set_sensitive (data->printer_combo, TRUE);
    }
  else
    {
      unit_widget_set_sensitive (data->width_widget, FALSE);
      unit_widget_set_sensitive (data->height_widget, FALSE);
      unit_widget_set_sensitive (data->top_widget, FALSE);
      unit_widget_set_sensitive (data->bottom_widget, FALSE);
      unit_widget_set_sensitive (data->left_widget, FALSE);
      unit_widget_set_sensitive (data->right_widget, FALSE);
      gtk_widget_set_sensitive (data->printer_combo, FALSE);
    }
  data->non_user_change = FALSE;
}

static void
selected_custom_paper_changed (GtkTreeSelection *selection,
			       CustomPaperDialog *data)
{
  update_custom_widgets_from_list (data);
}

static void
unit_widget_changed (CustomPaperDialog *data)
{
  double w, h, top, bottom, left, right;
  GtkTreeSelection *selection;
  GtkTreeIter iter;
  EggPageSetup *page_setup;
  EggPaperSize *paper_size;

  if (data->non_user_change)
    return;
  
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview));

  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
      gtk_tree_model_get (GTK_TREE_MODEL (data->dialog->priv->custom_paper_list), &iter, 0, &page_setup, -1);

      w = unit_widget_get (data->width_widget);
      h = unit_widget_get (data->height_widget);

      paper_size = egg_page_setup_get_paper_size (page_setup);
      egg_paper_size_set_size (paper_size, w, h, EGG_UNIT_MM);
      
      top = unit_widget_get (data->top_widget);
      bottom = unit_widget_get (data->bottom_widget);
      left = unit_widget_get (data->left_widget);
      right = unit_widget_get (data->right_widget);

      egg_page_setup_set_top_margin (page_setup, top, EGG_UNIT_MM);
      egg_page_setup_set_bottom_margin (page_setup, bottom, EGG_UNIT_MM);
      egg_page_setup_set_left_margin (page_setup, left, EGG_UNIT_MM);
      egg_page_setup_set_right_margin (page_setup, right, EGG_UNIT_MM);
      
      g_object_unref (page_setup);
    }
}

static gboolean
custom_paper_name_used (CustomPaperDialog *data, const char *name)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  EggPageSetup *page_setup;
  EggPaperSize *paper_size;
  
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (data->treeview));
	  
  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      do
	{
	  gtk_tree_model_get (model, &iter, 0, &page_setup, -1);
	  paper_size = egg_page_setup_get_paper_size (page_setup);
	  if (strcmp (name,
		      egg_paper_size_get_name (paper_size)) == 0)
	    {
	      g_object_unref (page_setup);
	      return TRUE;
	    }
	  g_object_unref (page_setup);
	} while (gtk_tree_model_iter_next (model, &iter));
    }
  
  return FALSE;
}

static void
add_custom_paper (CustomPaperDialog *data)
{
  GtkListStore *store;
  EggPageSetup *page_setup;
  EggPaperSize *paper_size;
  GtkTreeSelection *selection;
  GtkTreePath *path;
  GtkTreeIter iter;
  char *name;
  int i;
  
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview));
  store = data->dialog->priv->custom_paper_list;

  i = 1;
  name = NULL;
  do
    {
      g_free (name);
      name = g_strdup_printf (_("Custom Size %d"), i);
      i++;
    } while (custom_paper_name_used (data, name));

  page_setup = egg_page_setup_new ();
  paper_size = egg_paper_size_new_custom (name, name,
					  egg_page_setup_get_paper_width (page_setup, EGG_UNIT_MM),
					  egg_page_setup_get_paper_height (page_setup, EGG_UNIT_MM),
					  EGG_UNIT_MM);
  egg_page_setup_set_paper_size (page_setup, paper_size);
  egg_paper_size_free (paper_size);
  
  gtk_list_store_append (store, &iter);
  gtk_list_store_set (store, &iter, 0, page_setup, -1);
  g_object_unref (page_setup);

  gtk_tree_selection_select_iter (selection, &iter);
  path = gtk_tree_model_get_path (GTK_TREE_MODEL (store), &iter);
  gtk_widget_grab_focus (data->treeview);
  gtk_tree_view_set_cursor (GTK_TREE_VIEW (data->treeview), path,
			    data->text_column, TRUE);
  gtk_tree_path_free (path);
  
}

static void
remove_custom_paper (CustomPaperDialog *data)
{
  GtkTreeSelection *selection;
  GtkTreeIter iter;
  GtkListStore *store;
  
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview));
  store = data->dialog->priv->custom_paper_list;

  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
      GtkTreePath *path = gtk_tree_model_get_path (GTK_TREE_MODEL (store), &iter);
      gtk_list_store_remove (store, &iter);
      
      if (gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &iter, path))
	gtk_tree_selection_select_iter (selection, &iter);
      else if (gtk_tree_path_prev (path) && gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &iter, path))
	gtk_tree_selection_select_iter (selection, &iter);
      
      gtk_tree_path_free (path);
    }
}

static void
set_margins_from_printer (CustomPaperDialog *data,
			  EggPrinter *printer)
{
  double top, bottom, left, right;

  top = bottom = left = right = 0;
  _egg_printer_get_hard_margins (printer, &top, &bottom, &left, &right);
  
  data->non_user_change = TRUE;
  unit_widget_set (data->top_widget, to_mm (top, EGG_UNIT_POINTS));
  unit_widget_set (data->bottom_widget, to_mm (bottom, EGG_UNIT_POINTS));
  unit_widget_set (data->left_widget, to_mm (left, EGG_UNIT_POINTS));
  unit_widget_set (data->right_widget, to_mm (right, EGG_UNIT_POINTS));
  data->non_user_change = FALSE;

  /* Only send one change */
  unit_widget_changed (data);
}

static void
get_margins_finished_callback (EggPrinter *printer,
			       gboolean success,
			       CustomPaperDialog *data)
{
  data->request_details_tag = 0;
  
  if (success)
    set_margins_from_printer (data, printer);

  gtk_combo_box_set_active (GTK_COMBO_BOX (data->printer_combo), 0);
}

static void
margins_from_printer_changed (CustomPaperDialog *data)
{
  GtkTreeIter iter;
  GtkComboBox *combo;
  EggPrinter *printer;

  combo = GTK_COMBO_BOX (data->printer_combo);

  if (data->request_details_tag)
    {
      g_source_remove (data->request_details_tag);
      data->request_details_tag = 0;
    }
  
  if (gtk_combo_box_get_active_iter  (combo, &iter))
    {
      gtk_tree_model_get (gtk_combo_box_get_model (combo), &iter,
			  PRINTER_LIST_COL_PRINTER_OBJ, &printer, -1);

      if (printer)
	{
	  if (_egg_printer_has_details (printer))
	    {
	      set_margins_from_printer (data, printer);
	      gtk_combo_box_set_active (combo, 0);
	    }
	  else
	    {
	      data->request_details_tag =
		g_signal_connect (printer, "details-acquired",
				  G_CALLBACK (get_margins_finished_callback), data);
	      _egg_printer_request_details (printer);
	    }

	  g_object_unref (printer);
	}
    }
}


static void
custom_paper_dialog_free (gpointer p)
{
  CustomPaperDialog *data = p;
  if (data->request_details_tag)
    {
      g_source_remove (data->request_details_tag);
      data->request_details_tag = 0;
    }
  
  g_free (data);
}

static void
custom_size_name_edited (GtkCellRenderer   *cell,
			 gchar             *path_string,
			 gchar             *new_text,
			 CustomPaperDialog *data)
{
  GtkTreePath *path;
  GtkTreeIter iter;
  GtkListStore *store;
  EggPageSetup *page_setup;
  EggPaperSize *paper_size;

  store = data->dialog->priv->custom_paper_list;
  path = gtk_tree_path_new_from_string (path_string);
  gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &iter, path);
  gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, 0, &page_setup, -1);
  gtk_tree_path_free (path);

  paper_size = egg_paper_size_new_custom (new_text, new_text,
					  egg_page_setup_get_paper_width (page_setup, EGG_UNIT_MM),
					  egg_page_setup_get_paper_height (page_setup, EGG_UNIT_MM),
					  EGG_UNIT_MM);
  egg_page_setup_set_paper_size (page_setup, paper_size);
  egg_paper_size_free (paper_size);
  
  g_object_unref (page_setup);
}

static void
custom_name_func (GtkTreeViewColumn *tree_column,
		  GtkCellRenderer   *cell,
		  GtkTreeModel      *tree_model,
		  GtkTreeIter       *iter,
		  gpointer           data)
{
  EggPageSetup *page_setup;
  EggPaperSize *paper_size;

  gtk_tree_model_get (tree_model, iter, 0, &page_setup, -1);
  if (page_setup)
    {
      paper_size = egg_page_setup_get_paper_size (page_setup);
      g_object_set (cell, "text",  egg_paper_size_get_display_name (paper_size), NULL);
      g_object_unref (page_setup);
    }
}

static void
run_custom_paper_dialog (EggPageSetupUnixDialog *dialog)
{
  GtkWidget *custom_dialog, *image, *table, *label, *widget, *frame, *combo;
  GtkWidget *hbox, *vbox, *treeview, *scrolled, *button_box, *button;
  GtkCellRenderer *cell;
  GtkTreeViewColumn *column;
  GtkTreeSelection *selection;
  CustomPaperDialog *data;
  EggUnit user_units;
  
  custom_dialog = gtk_dialog_new_with_buttons (_("Custom Paper Sizes"),
					       GTK_WINDOW (dialog),
					       GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_NO_SEPARATOR,
					       GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
					       NULL);

  data = g_new0 (CustomPaperDialog, 1);
  data->dialog = dialog;
  g_object_set_data_full (G_OBJECT (custom_dialog), "custom-dialog", data, custom_paper_dialog_free);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (custom_dialog)->vbox), hbox, TRUE, TRUE, 0);
  gtk_widget_show (hbox);

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);
  gtk_widget_show (vbox);
  
  scrolled = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled),
				       GTK_SHADOW_IN);
  gtk_box_pack_start (GTK_BOX (vbox), scrolled, TRUE, TRUE, 0);
  gtk_widget_show (scrolled);

  treeview = gtk_tree_view_new_with_model (GTK_TREE_MODEL (dialog->priv->custom_paper_list));
  data->treeview = treeview;
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (treeview), FALSE);
  gtk_widget_set_size_request (treeview, 140, -1);
  
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
  g_signal_connect (selection, "changed", G_CALLBACK (selected_custom_paper_changed), data);

  cell = gtk_cell_renderer_text_new ();
  g_object_set (cell, "editable", TRUE, NULL);
  g_signal_connect (cell, "edited", 
		    G_CALLBACK (custom_size_name_edited), data);
  data->text_column = column =
    gtk_tree_view_column_new_with_attributes ("paper", cell,
					      NULL);
  gtk_tree_view_column_set_cell_data_func  (column, cell, custom_name_func, NULL, NULL);
  
  gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

  gtk_container_add (GTK_CONTAINER (scrolled), treeview);
  gtk_widget_show (treeview);

  button_box = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), button_box, FALSE, FALSE, 0);
  gtk_widget_show (button_box);

  button = gtk_button_new ();
  image = gtk_image_new_from_stock (GTK_STOCK_ADD, GTK_ICON_SIZE_BUTTON);
  gtk_widget_show (image);
  gtk_container_add (GTK_CONTAINER (button), image);
  gtk_box_pack_start (GTK_BOX (button_box), button, FALSE, FALSE, 0);
  gtk_widget_show (button);

  g_signal_connect_swapped (button, "clicked", G_CALLBACK (add_custom_paper), data);
  
  button = gtk_button_new ();
  image = gtk_image_new_from_stock (GTK_STOCK_REMOVE, GTK_ICON_SIZE_BUTTON);
  gtk_widget_show (image);
  gtk_container_add (GTK_CONTAINER (button), image);
  gtk_box_pack_start (GTK_BOX (button_box), button, FALSE, FALSE, 0);
  gtk_widget_show (button);

  g_signal_connect_swapped (button, "clicked", G_CALLBACK (remove_custom_paper), data);

  user_units = get_default_user_units ();
  
  vbox = gtk_vbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);
  gtk_widget_show (vbox);
  
  table = gtk_table_new (2, 2, FALSE);
  
  label = gtk_label_new (_("Width: "));
  gtk_widget_show (label);
  gtk_table_attach (GTK_TABLE (table), label,
		    0, 1, 0, 1, 0, 0, 0, 0);
  
  widget = new_unit_widget (data, user_units);
  data->width_widget = widget;
  gtk_table_attach (GTK_TABLE (table), widget,
		    1, 2, 0, 1, 0, 0, 0, 0);
  gtk_widget_show (widget);
  
  label = gtk_label_new (_("Height: "));
  gtk_widget_show (label);
  gtk_table_attach (GTK_TABLE (table), label,
		    0, 1, 1, 2, 0, 0, 0, 0);
  
  widget = new_unit_widget (data, user_units);
  data->height_widget = widget;
  gtk_table_attach (GTK_TABLE (table), widget,
		    1, 2, 1, 2, 0, 0, 0, 0);
  gtk_widget_show (widget);

  frame = wrap_in_frame (_("Paper Size"), table);
  gtk_widget_show (table);
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);
  

  table = gtk_table_new (3, 5, FALSE);

  widget = new_unit_widget (data, user_units);
  data->top_widget = widget;
  gtk_table_attach (GTK_TABLE (table), widget,
		    1, 2, 0, 1, 0, 0, 0, 0);
  gtk_widget_show (widget);

  label = gtk_label_new (_("top"));
  gtk_table_attach (GTK_TABLE (table), label,
		    1, 2, 1, 2, 0, GTK_FILL|GTK_EXPAND, 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label),
			  0.5, 0.0);
  gtk_widget_show (label);
  
  widget = new_unit_widget (data, user_units);
  data->bottom_widget = widget;
  gtk_table_attach (GTK_TABLE (table), widget,
		    1, 2, 2, 3, 0, 0, 0, 0);
  gtk_widget_show (widget);

  label = gtk_label_new (_("bottom"));
  gtk_table_attach (GTK_TABLE (table), label,
		    1, 2, 3, 4, 0, GTK_FILL|GTK_EXPAND, 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label),
			  0.5, 0.0);
  gtk_widget_show (label);

  widget = new_unit_widget (data, user_units);
  data->left_widget = widget;
  gtk_table_attach (GTK_TABLE (table), widget,
		    0, 1, 1, 2, 0, 0, 0, 0);
  gtk_widget_show (widget);

  label = gtk_label_new (_("left"));
  gtk_table_attach (GTK_TABLE (table), label,
		    0, 1, 2, 3, 0, GTK_FILL|GTK_EXPAND, 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label),
			  0.5, 0.0);
  gtk_widget_show (label);
  
  widget = new_unit_widget (data, user_units);
  data->right_widget = widget;
  gtk_table_attach (GTK_TABLE (table), widget,
		    2, 3, 1, 2, 0, 0, 0, 0);
  gtk_widget_show (widget);

  label = gtk_label_new (_("right"));
  gtk_table_attach (GTK_TABLE (table), label,
		    2, 3, 2, 3, 0, GTK_FILL|GTK_EXPAND, 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label),
			  0.5, 0.0);
  gtk_widget_show (label);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_table_attach (GTK_TABLE (table), hbox,
		    0, 3, 4, 5, GTK_FILL | GTK_EXPAND, 0, 0, 0);
  gtk_widget_show (hbox);
  gtk_table_set_row_spacing (GTK_TABLE (table), 3, 8);
  
  combo = gtk_combo_box_new_with_model (GTK_TREE_MODEL (dialog->priv->printer_list));
  data->printer_combo = combo;
  
  cell = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), cell, TRUE);
  gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (combo), cell,
				      custom_paper_printer_data_func,
				      NULL, NULL);

  gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
  gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, FALSE, 0);
  gtk_widget_show (combo);

  g_signal_connect_swapped (combo, "changed",
			    G_CALLBACK (margins_from_printer_changed), data);
  
  frame = wrap_in_frame (_("Paper Margins"), table);
  gtk_widget_show (table);
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  update_custom_widgets_from_list (data);
  
  gtk_dialog_run (GTK_DIALOG (custom_dialog));
  gtk_widget_destroy (custom_dialog);

  save_custom_papers (dialog->priv->custom_paper_list);
}
