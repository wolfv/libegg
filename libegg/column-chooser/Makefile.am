AM_CPPFLAGS = \
  $(EGG_CFLAGS) $(WARN_CFLAGS) $(DISABLE_DEPRECATED) \
  -DEGG_COMPILATION

noinst_LTLIBRARIES = libegg-columnchooser.la

libegg_columnchooser_la_LIBADD = $(EGG_LIBS)
libegg_columnchooser_la_SOURCES = \
	eggcolumnchooserdialog.c	\
	eggcolumnmodel.c

noinst_HEADERS = \
	eggcolumnchooserdialog.h	\
	eggcolumnmodel.c


noinst_PROGRAMS = test-tree-column

test_tree_column_SOURCES = test-tree-column.c
test_tree_column_LDADD = libegg-columnchooser.la $(EGG_LIBS)
test_tree_column_DEPENDENCIES = libegg-columnchooser.la
