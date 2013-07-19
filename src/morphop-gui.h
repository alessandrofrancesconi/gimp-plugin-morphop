#ifndef __MORPHOP_GUI_H__
#define __MORPHOP_GUI_H__

#include <gtk/gtk.h>
#include <libgimp/gimp.h>

gboolean morphop_show_gui(gint32, GimpDrawable*);
const char* operator_get_string(MorphOperator);

#endif
