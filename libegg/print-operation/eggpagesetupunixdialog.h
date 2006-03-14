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
#ifndef __EGG_PAGE_SETUP_UNIX_DIALOG_H__
#define __EGG_PAGE_SETUP_UNIX_DIALOG_H__

#include <gtk/gtk.h>

#include "eggpagesetup.h"
#include "eggprintsettings.h"

G_BEGIN_DECLS

#define EGG_TYPE_PAGE_SETUP_UNIX_DIALOG                  (egg_page_setup_unix_dialog_get_type ())
#define EGG_PAGE_SETUP_UNIX_DIALOG(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), EGG_TYPE_PAGE_SETUP_UNIX_DIALOG, EggPageSetupUnixDialog))
#define EGG_PAGE_SETUP_UNIX_DIALOG_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), EGG_TYPE_PAGE_SETUP_UNIX_DIALOG, EggPageSetupUnixDialogClass))
#define EGG_IS_PAGE_SETUP_UNIX_DIALOG(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EGG_TYPE_PAGE_SETUP_UNIX_DIALOG))
#define EGG_IS_PAGE_SETUP_UNIX_DIALOG_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), EGG_TYPE_PAGE_SETUP_UNIX_DIALOG))
#define EGG_PAGE_SETUP_UNIX_DIALOG_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), EGG_TYPE_PAGE_SETUP_UNIX_DIALOG, EggPageSetupUnixDialogClass))


typedef struct _EggPageSetupUnixDialog         EggPageSetupUnixDialog;
typedef struct _EggPageSetupUnixDialogClass    EggPageSetupUnixDialogClass;
typedef struct EggPageSetupUnixDialogPrivate   EggPageSetupUnixDialogPrivate;

struct _EggPageSetupUnixDialog
{
  GtkDialog parent_instance;

  EggPageSetupUnixDialogPrivate *priv;
};

struct _EggPageSetupUnixDialogClass
{
  GtkDialogClass parent_class;

  /* Padding for future expansion */
  void (*_egg_reserved1) (void);
  void (*_egg_reserved2) (void);
  void (*_egg_reserved3) (void);
  void (*_egg_reserved4) (void);
  void (*_egg_reserved5) (void);
  void (*_egg_reserved6) (void);
  void (*_egg_reserved7) (void);
};

GType 		  egg_page_setup_unix_dialog_get_type	        (void) G_GNUC_CONST;
GtkWidget *       egg_page_setup_unix_dialog_new                (const gchar            *title,
								 GtkWindow              *parent);
void              egg_page_setup_unix_dialog_set_page_setup     (EggPageSetupUnixDialog *dialog,
								 EggPageSetup           *page_setup);
EggPageSetup *    egg_page_setup_unix_dialog_get_page_setup     (EggPageSetupUnixDialog *dialog);
void              egg_page_setup_unix_dialog_set_print_settings (EggPageSetupUnixDialog *dialog,
								 EggPrintSettings       *print_settings);
EggPrintSettings *egg_page_setup_unix_dialog_get_print_settings (EggPageSetupUnixDialog *dialog);

G_END_DECLS

#endif /* __EGG_PAGE_SETUP_UNIX_DIALOG_H__ */
