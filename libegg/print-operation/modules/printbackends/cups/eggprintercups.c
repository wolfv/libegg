/* EggPrinterCupsCups
 * Copyright (C) 2006 John (J5) Palmieri  <johnp@redhat.com>
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

#include "config.h"
#include "eggprintercups.h"

#define EGG_PRINTER_CUPS_GET_PRIVATE(o)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((o), EGG_TYPE_PRINTER_CUPS, EggPrinterCupsPrivate))

static void egg_printer_cups_finalize     (GObject *object);

G_DEFINE_TYPE (EggPrinterCups, egg_printer_cups, EGG_TYPE_PRINTER);

static void
egg_printer_cups_class_init (EggPrinterCupsClass *class)
{
  GObjectClass *object_class;
  object_class = (GObjectClass *) class;

  object_class->finalize = egg_printer_cups_finalize;

}

static void
egg_printer_cups_init (EggPrinterCups *printer)
{
  printer->device_uri = NULL;
  printer->printer_uri = NULL;
  printer->state = 0;
  printer->hostname = NULL;
  printer->port = 0;
  printer->ppd_file = NULL;
}

static void
egg_printer_cups_finalize (GObject *object)
{
  g_return_if_fail (object != NULL);

  EggPrinterCups *printer = EGG_PRINTER_CUPS (object);

  g_free (printer->device_uri);
  g_free (printer->printer_uri);
  g_free (printer->hostname);

  if (printer->ppd_file)
    ppdClose (printer->ppd_file);

  if (G_OBJECT_CLASS (egg_printer_cups_parent_class)->finalize)
    G_OBJECT_CLASS (egg_printer_cups_parent_class)->finalize (object);
}

/**
 * egg_printer_cups_new:
 *
 * Creates a new #EggPrinterCups.
 *
 * Return value: a new #EggPrinterCups
 *
 * Since: 2.10
 **/
EggPrinterCups *
egg_printer_cups_new (void)
{
  GObject *result;
  
  result = g_object_new (EGG_TYPE_PRINTER_CUPS,
                         NULL);

  return (EggPrinterCups *) result;
}

ppd_file_t *
egg_printer_cups_get_ppd (EggPrinterCups *printer)
{
  return printer->ppd_file;
}