
#include <gtk/gtk.h>
#include <glib.h>
#include <math.h>
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>
#include "morphop.h"
#include "morphop-gui.h"
#include "morphop-algorithms.h"
#include "morphop-logo.h"

static void operator_changed(GtkWidget*, gpointer); 
static gboolean element_changed (GtkWidget*, GdkEvent*, gpointer);
static void size_changed (GtkWidget*, gpointer); 
static void update_preview(GimpPreview*, gpointer);
static void open_about(void);
const char* operator_get_info(MorphOperator);
const char* size_get_string(ElementSize);

GtkWidget *morphop_window_main;
GtkWidget *panel_preview, *combo_operator, *combo_size, *grid_strelem_def;
GtkWidget *label_info;

GtkWidget* strelem_drawarea_matrix[STRELEM_DEFAULT_SIZE][STRELEM_DEFAULT_SIZE];

gboolean morphop_show_gui(gint32 image_id, GimpDrawable* drawable) 
{
	gboolean run;
	
	// main widgets
	GtkWidget *main_container, *center_container, *panel_settings;
	
	// widgets for settings panel
	GtkWidget *panel_opsel, *label_opsel, *panel_size, *label_size;
	GtkWidget *label_strelem_def;
	GtkWidget *panel_info, *icon_info;
	
	gimp_ui_init (MORPHOP_BINARY, FALSE);
	
	morphop_window_main = gimp_dialog_new(
		MORPHOP_FULLNAME,
		MORPHOP_BINARY,
		NULL,
		0,
		NULL,
		NULL,
		GTK_STOCK_ABOUT, GTK_RESPONSE_HELP,
		GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
		GTK_STOCK_APPLY, GTK_RESPONSE_APPLY, NULL
	);
	
	gimp_window_set_transient (GTK_WINDOW(morphop_window_main));
	gtk_widget_set_size_request (morphop_window_main, 530, 470);
	gtk_window_set_resizable (GTK_WINDOW(morphop_window_main), FALSE);
	gtk_window_set_position(GTK_WINDOW(morphop_window_main), GTK_WIN_POS_CENTER);
	gtk_container_set_border_width(GTK_CONTAINER(morphop_window_main), 5);
	
	main_container = gtk_vbox_new(FALSE, 10);
	
	// operator chooser
	GtkWidget* align_opsel = gtk_alignment_new (0.5, 0, 0, 0);
	
	panel_opsel = gtk_hbox_new(FALSE, 5);
	label_opsel = gtk_label_new("Operation:");
	combo_operator = gtk_combo_box_new_text();
	int i;
	for(i = 0; i < OPERATOR_END; i++) {
		gtk_combo_box_append_text(GTK_COMBO_BOX(combo_operator), operator_get_string(i));
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo_operator), msettings.operator);
	g_signal_connect(G_OBJECT(combo_operator), "changed", G_CALLBACK(operator_changed), NULL);
	
	gtk_box_pack_start (GTK_BOX (panel_opsel), label_opsel, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (panel_opsel), combo_operator, FALSE, FALSE, 0);
	
	gtk_container_add(GTK_CONTAINER(align_opsel), panel_opsel);
	gtk_box_pack_start (GTK_BOX (main_container), align_opsel, FALSE, FALSE, 0);
	
	center_container = gtk_hbox_new(FALSE, 5);
	
	// preview panel
	panel_preview = gimp_drawable_preview_new (drawable, NULL);
	gtk_widget_set_size_request (panel_preview, 200, 300);
	
	
	// settings panel
	panel_settings = gtk_vbox_new(FALSE, 10);
	
	// structuring element settings
	label_strelem_def = gtk_label_new("Structuring element");
	GtkWidget* align_strelem = gtk_alignment_new (0.5, 0, 0, 0);
	grid_strelem_def = gtk_table_new(STRELEM_DEFAULT_SIZE, STRELEM_DEFAULT_SIZE, TRUE);
	
	GtkWidget* event_box;
	GdkColor color_black, color_red, color_white;
	gdk_color_parse ("black", &color_black);
	gdk_color_parse ("white", &color_white);
	gdk_color_parse ("red", &color_red);
	int j;
	
	for(i = 0; i < STRELEM_DEFAULT_SIZE; i++) {
		for (j = 0; j < STRELEM_DEFAULT_SIZE; j++) {
			
			strelem_drawarea_matrix[i][j] = gtk_drawing_area_new();
			gtk_widget_set_size_request (strelem_drawarea_matrix[i][j], 30, 30);
			
			event_box = gtk_event_box_new();
			
			if (msettings.element.matrix[i][j] == 0) {
				gtk_widget_modify_bg(strelem_drawarea_matrix[i][j], GTK_STATE_NORMAL, &color_black);
			}
			else if (msettings.element.matrix[i][j] == 1) {
				gtk_widget_modify_bg(strelem_drawarea_matrix[i][j], GTK_STATE_NORMAL, &color_white);
			}
			else if (msettings.element.matrix[i][j] == -1) {
				gtk_widget_modify_bg(strelem_drawarea_matrix[i][j], GTK_STATE_NORMAL, &color_red);
			}
			
			gtk_container_add(GTK_CONTAINER(event_box), strelem_drawarea_matrix[i][j]);
			gtk_table_attach (GTK_TABLE (grid_strelem_def), event_box, j, j+1, i, i+1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 0, 0);
			g_signal_connect (event_box, "button_press_event", G_CALLBACK (element_changed), GINT_TO_POINTER(i * STRELEM_DEFAULT_SIZE + j));
		}
	}
	
	gtk_box_pack_start (GTK_BOX (panel_settings), label_strelem_def, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(align_strelem), grid_strelem_def);
	gtk_box_pack_start (GTK_BOX (panel_settings), align_strelem, TRUE, TRUE, 0);
	
	GtkWidget* align_size = gtk_alignment_new (0.5, 0, 0, 0);
	panel_size = gtk_hbox_new(FALSE, 5);
	label_size = gtk_label_new("Element size:");
	combo_size = gtk_combo_box_new_text();
	for(i = 0; i < SIZE_END; i++) {
		gtk_combo_box_append_text(GTK_COMBO_BOX(combo_size), size_get_string(i));
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo_size), msettings.element.size);
	g_signal_connect(G_OBJECT(combo_size), "changed", G_CALLBACK(size_changed), NULL);
	
	gtk_box_pack_start (GTK_BOX (panel_size), label_size, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (panel_size), combo_size, FALSE, FALSE, 0);
	
	gtk_container_add(GTK_CONTAINER(align_size), panel_size);
	gtk_box_pack_start (GTK_BOX (panel_settings), align_size, FALSE, FALSE, 0);
	
	gtk_box_pack_start (GTK_BOX (center_container), panel_preview, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (center_container), panel_settings, TRUE, TRUE, 0);
	
	gtk_box_pack_start (GTK_BOX (main_container), center_container, TRUE, TRUE, 0);
	
	// info panel
	panel_info = gtk_hbox_new(FALSE, 10);
	icon_info = gtk_image_new_from_stock(GIMP_STOCK_INFO, GTK_ICON_SIZE_BUTTON);
	gtk_widget_set_size_request (icon_info, 40, 100);
	label_info = gtk_label_new(operator_get_info(gtk_combo_box_get_active(GTK_COMBO_BOX(combo_operator))));
	gtk_label_set_line_wrap (GTK_LABEL(label_info), TRUE);
	gtk_widget_set_size_request (label_info, 450, 100);
	
	gtk_box_pack_start (GTK_BOX (panel_info), icon_info, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (panel_info), label_info, FALSE, FALSE, 0);
	
	gtk_box_pack_start (GTK_BOX (main_container), panel_info, TRUE, TRUE, 0);
	
	gtk_container_add (GTK_CONTAINER (GTK_DIALOG(morphop_window_main)->vbox), main_container);
	gtk_widget_show_all(morphop_window_main);
	
	g_signal_connect (panel_preview, "invalidated", G_CALLBACK (update_preview), NULL);
	
	while(TRUE) {
		gint run = gimp_dialog_run (GIMP_DIALOG(morphop_window_main));
		if (run == GTK_RESPONSE_APPLY) {
			
			gtk_widget_destroy (morphop_window_main);
			
			gimp_progress_init (operator_get_string(msettings.operator));
			start_operation(
				drawable,
				NULL, 
				msettings
			);
			
			return TRUE;
		}
		else if (run == GTK_RESPONSE_HELP) {
			open_about();
		}
		else {
			gtk_widget_destroy (morphop_window_main);
			return FALSE;
		}
	}

	return FALSE;
}

static void operator_changed(GtkWidget* widget, gpointer data) 
{
	msettings.operator = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
	
	GdkColor color_black;
	gdk_color_parse ("black", &color_black);
	
	int i, j;
	for(i = 0; i < STRELEM_DEFAULT_SIZE; i++) {
		for (j = 0; j < STRELEM_DEFAULT_SIZE; j++) {
			if (
				(msettings.operator != OPERATOR_HITORMISS && msettings.operator != OPERATOR_THINNING && msettings.operator != OPERATOR_THICKENING) &&
				msettings.element.matrix[i][j] == -1
			) {
				msettings.element.matrix[i][j] = 0;
				gtk_widget_modify_bg(strelem_drawarea_matrix[i][j], GTK_STATE_NORMAL, &color_black);
				gtk_widget_queue_draw (strelem_drawarea_matrix[i][j]);
			}
		}
	}
	
	gtk_label_set_text (GTK_LABEL(label_info), operator_get_info(msettings.operator));
	
	if (gimp_preview_get_update(GIMP_PREVIEW(panel_preview))) gimp_preview_invalidate(GIMP_PREVIEW(panel_preview));
}

static gboolean element_changed (GtkWidget *evbox, GdkEvent *event, gpointer id) 
{
	int id_i = GPOINTER_TO_INT(id);
	int i = (int)ceil((id_i + 1.0) / STRELEM_DEFAULT_SIZE) - 1;
	int j = id_i % STRELEM_DEFAULT_SIZE;
	GtkWidget* drawarea = strelem_drawarea_matrix[i][j];
	
	GdkColor color_black, color_red, color_white;
	gdk_color_parse ("black", &color_black);
	gdk_color_parse ("red", &color_red);
	gdk_color_parse ("white", &color_white);
	
	if (msettings.element.matrix[i][j] == -1) {
		msettings.element.matrix[i][j] = 0;
		gtk_widget_modify_bg(drawarea, GTK_STATE_NORMAL, &color_black);
	}
	else if (msettings.element.matrix[i][j] == 0) {
		msettings.element.matrix[i][j] = 1;
		gtk_widget_modify_bg(drawarea, GTK_STATE_NORMAL, &color_white);
	}
	else if (msettings.element.matrix[i][j] == 1) {
		if (msettings.operator == OPERATOR_HITORMISS || msettings.operator == OPERATOR_THINNING || msettings.operator == OPERATOR_THICKENING) {
			msettings.element.matrix[i][j] = -1;
			gtk_widget_modify_bg(drawarea, GTK_STATE_NORMAL, &color_red);
		}
		else {
			msettings.element.matrix[i][j] = 0;
			gtk_widget_modify_bg(drawarea, GTK_STATE_NORMAL, &color_black);
		}
	}
	gtk_widget_queue_draw (drawarea);
	
	int i2, j2;
	gboolean at_least_one = FALSE;
	for(i2 = 0; i2 < STRELEM_DEFAULT_SIZE; i2++) {
		for(j2 = 0; j2 < STRELEM_DEFAULT_SIZE; j2++) {
			if(msettings.element.matrix[i2][j2] != 0) {
				at_least_one = TRUE; 
				break;
			}
		}
	}
	
	if (at_least_one == FALSE) {
		msettings.element.matrix[i][j] = 1;
		gtk_widget_modify_bg(drawarea, GTK_STATE_NORMAL, &color_white);
	}
	else {
		if (gimp_preview_get_update(GIMP_PREVIEW(panel_preview))) gimp_preview_invalidate(GIMP_PREVIEW(panel_preview));
	}
	
	return TRUE;
}


static void size_changed (GtkWidget* widget, gpointer data) 
{
	msettings.element.size = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
	if (gimp_preview_get_update(GIMP_PREVIEW(panel_preview))) gimp_preview_invalidate(GIMP_PREVIEW(panel_preview));
}

static void update_preview(GimpPreview* preview, gpointer data) 
{
	gtk_widget_set_sensitive(grid_strelem_def, FALSE);
	gtk_widget_set_sensitive(combo_operator, FALSE);
	gtk_widget_set_sensitive(combo_size, FALSE);
	
	start_operation(
		gimp_drawable_preview_get_drawable (GIMP_DRAWABLE_PREVIEW (preview)),
		preview, 
		msettings
	);
	
	gtk_widget_set_sensitive(grid_strelem_def, TRUE);
	gtk_widget_set_sensitive(combo_operator, TRUE);
	gtk_widget_set_sensitive(combo_size, TRUE);
}

static void open_about() 
{
	const gchar *auth[] = { 
		"Alessandro Francesconi <alessandrofrancesconi@live.it>",
		NULL };
	const gchar *license = 
		"This program is free software; you can redistribute it and/or modify "
		"it under the terms of the GNU General Public License as published by "
		"the Free Software Foundation; either version 2 of the License, or "
		"(at your option) any later version. \n\n"
		"This program is distributed in the hope that it will be useful, "
		"but WITHOUT ANY WARRANTY; without even the implied warranty of "
		"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
		"GNU General Public License for more details.\n\n"
		"You should have received a copy of the GNU General Public License "
		"along with this program; if not, write to the Free Software "
		"Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, "
		"MA 02110-1301, USA. ";

	gtk_show_about_dialog( 
		GTK_WINDOW(morphop_window_main),
		"program-name", MORPHOP_FULLNAME,
		"version", g_strdup_printf("%d.%d", PLUG_IN_VERSION_MAJ, PLUG_IN_VERSION_MIN),
		"comments", "A set of morphological operators for GIMP",
		"logo", gdk_pixbuf_from_pixdata(&pixdata_morphoplogo, FALSE, NULL),
		"copyright", MORPHOP_COPYRIGHT,
		"license", license,
		"wrap-license", TRUE,
		"website", MORPHOP_WEBSITE,
		"authors", auth,
		NULL 
	);
}

const char* operator_get_string(MorphOperator o)
{
	switch (o) {
		case OPERATOR_EROSION: return "Erosion"; break;
		case OPERATOR_DILATION: return "Dilation"; break;
		case OPERATOR_OPENING: return "Opening"; break;
		case OPERATOR_CLOSING: return "Closing"; break;
		case OPERATOR_BOUNDEXTR: return "Boundary extraction"; break;
		case OPERATOR_GRADIENT: return "Gradient"; break;
		case OPERATOR_HITORMISS: return "Hit-or-Miss"; break;
		case OPERATOR_SKELETON: return "Skeletonization (it's slow!)"; break;
		case OPERATOR_THICKENING: return "Thickening"; break;
		case OPERATOR_THINNING: return "Thinning"; break;
		case OPERATOR_WTOPHAT: return "White Top-hat"; break;
		case OPERATOR_BTOPHAT: return "Black Top-hat"; break;
		default: return "<unknown>"; break;
	}
}

const char* operator_get_info(MorphOperator o)
{	
	switch (o) {
		case OPERATOR_EROSION: return "Erodes the boundaries of bright regions. That is, brighter areas shrink in size and darker spots within those areas become larger."; break;
		case OPERATOR_DILATION: return "Dual of erosion, it enlarges the boundaries of brighter regions. For this, brighter areas grow in size and darker spots within those areas become smaller."; break;
		case OPERATOR_OPENING: return "Opening is a \"less destructive\" erosion. It shrinks brighter regions but preserving those that have a similar shape to the structuring element."; break;
		case OPERATOR_CLOSING: return "Closing is a \"less destructive\" dilation. It shrinks darker regions but preserving those that have a similar shape to the structuring element."; break;
		case OPERATOR_BOUNDEXTR: return "It produces a dark image where bright areas represents the boundaries of the original objects in the input."; break;
		case OPERATOR_GRADIENT: return "Useful for edge detection, it results in an usually dark image where pixels indicate the contrast intensity in its close neghborhood."; break;
		case OPERATOR_SKELETON: return "Creates a topological skeletonization of the input.\nWARNING! This is can be a very slow operation and it will work best with binary images with a black background."; break;
		case OPERATOR_HITORMISS: return "Looks for very particular patterns on a binary image. The structuring element has three filters: \"white\" areas refers to the search of only white pixels patterns, \"black\" for blacks, \"red\" stand for \"don't care\"."; break;
		case OPERATOR_THICKENING: return "Converts the black patterns found by the \"Hit-or-Miss\" transform to white, causing the outern shapes to become thicker"; break;
		case OPERATOR_THINNING: return "Removes the white patterns found by the \"Hit-or-Miss\" transform, causing the outern shapes to become thinner"; break;
		case OPERATOR_WTOPHAT: return "\"Top-hat\" operations extract small details from the image. The \"white\" version maintains the objects that are smaller than the structuring element and are brighter than their surroundings."; break;
		case OPERATOR_BTOPHAT: return "\"Top-hat\" operations extract small details from the image. The \"black\" version maintains the objects that are smaller than the structuring element and are darker than their surroundings."; break;
		default: return "<unknown>"; break;
	}
}

const char* size_get_string(ElementSize s)
{
	switch (s) {
		case SIZE_3x3: return "3x3"; break;
		case SIZE_5x5: return "5x5"; break;
		case SIZE_7x7: return "7x7 (original)"; break;
		case SIZE_9x9: return "9x9"; break;
		case SIZE_11x11: return "11x11"; break;
		default: return "<unknown>"; break;
	}
}
