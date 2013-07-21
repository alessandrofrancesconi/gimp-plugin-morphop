/*
 * Morphological Operators for GIMP
 * Alessandro Francesconi <alessandrofrancesconi@live.it>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 */



#include <gtk/gtk.h>
#include <libgimp/gimp.h>
#include <stdlib.h>
#include <string.h>
#include "morphop.h"
#include "morphop-gui.h"

static void query (void);

static void run (
	const gchar *name,
	gint nparams,
	const GimpParam *param,
	gint *nreturn_vals,
	GimpParam **return_vals
);

const GimpPlugInInfo PLUG_IN_INFO = {
	NULL,  /* init_proc  */
	NULL,  /* quit_proc  */
	query, /* query_proc */
	run,   /* run_proc   */
};

MAIN ()

static void query (void)
{
	static GimpParamDef args[] = {
		{ GIMP_PDB_INT32, "run-mode", "The run mode { RUN-INTERACTIVE (0), RUN-NONINTERACTIVE (1) }" },
		{ GIMP_PDB_IMAGE, "image", "Input image" },
		{ GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
		{ GIMP_PDB_INT32, "operator", "The morphological operator { EROSION (0), DILATION (1), OPENING (2), CLOSING (3), BOUNDEXTR(4), GRADIENT(5), HIT-OR-MISS(6), SKELETONIZATION(7), THICKENING(8), THINNING(9), WHITE-TOP-HAT(10), BLACK-TOP-HAT(11) }"},
		{ GIMP_PDB_INT32, "element-size", "Initial size of the structuring element (fake parameter, it's always 7)" },
		{ GIMP_PDB_INT8ARRAY, "element",   ""
			"The structuring element. Must be declared as an array representing a matrix, with size 7x7. "
			"The first 7 cells represent the first row, and so on. To define the element, set each element[i] to 1, 0 or -1 in case of HIT-OR-MISS, THICKENING or THINNING" },
		{ GIMP_PDB_INT32, "center", "Center of the structuring element, or rather the i-th index of the 'element' array (0 <= i <= 48)" },
		{ GIMP_PDB_INT32, "size", "Final scaled size of the structuring element { 3x3 (0), 5x5 (1), 7x7 (2), 9x9 (3), 11x11 (4)}" }
	};
	
	gimp_install_procedure (
		MORPHOP_PROC,
		"Morphological operators",
		"A set of morphological operators for GIMP: erosion, dilation, opening, closing, boundary extraction and more.",
		"Alessandro Francesconi <alessandrofrancesconi@live.it>",
		"Copyright (C) Alessandro Francesconi\n"
		"http://www.alessandrofrancesconi.it/projects/morphop",
		"2013",
		g_strconcat("Morphological operators", "...", NULL),
		"RGB*, GRAY*", // indexed?
		GIMP_PLUGIN,
		G_N_ELEMENTS (args),
		0,
		args, 
		NULL
	);

	gimp_plugin_menu_register (MORPHOP_PROC, "<Image>/Filters/Generic"); 
}

static void run (
	const gchar *name,
	gint nparams,
	const GimpParam *param,
	gint *nreturn_vals,
	GimpParam **return_vals)
{
	static GimpParam values[3];
	
	gint32 image_id;
	GimpDrawable *drawable;
	
	GimpPDBStatusType status = GIMP_PDB_SUCCESS;
	GimpRunMode run_mode;
	
	*nreturn_vals = 1;
	*return_vals  = values;
  
	values[0].type = GIMP_PDB_STATUS;
	values[0].data.d_status = status;
	
	image_id = param[1].data.d_image;
	drawable = gimp_drawable_get (param[2].data.d_drawable);
	
	run_mode = param[0].data.d_int32;
	
	// default settings
	MorphOpSettings default_set = {
		.operator = OPERATOR_EROSION,
		.element = {
			.matrix = {
				{0, 0, 0, 1, 0, 0, 0},
				{0, 0, 1, 1, 1, 0, 0},
				{0, 1, 1, 1, 1, 1, 0},
				{1, 1, 1, 1, 1, 1, 1},
				{0, 1, 1, 1, 1, 1, 0},
				{0, 0, 1, 1, 1, 0, 0},
				{0, 0, 0, 1, 0, 0, 0},
			},
			.size = SIZE_7x7
		}
	};
	msettings = default_set;
	
	if (strcmp (name, MORPHOP_PROC) == 0) {
		switch (run_mode) {
			case GIMP_RUN_WITH_LAST_VALS:
			
				gimp_get_data (MORPHOP_PROC, &msettings);
				start_operation(drawable, NULL, msettings);
				break;
				
			case GIMP_RUN_INTERACTIVE:
				
				gimp_get_data (MORPHOP_PROC, &msettings);
				if (! morphop_show_gui(image_id, drawable))
					return;
				gimp_set_data (MORPHOP_PROC, &msettings, sizeof(MorphOpSettings));
				break;

			case GIMP_RUN_NONINTERACTIVE:
			
				if (nparams != 8) {
					values[0].data.d_status = GIMP_PDB_CALLING_ERROR;
					break;
				}
				
				msettings.operator = param[3].data.d_int32;
				
				int i;
				for (i = 0; i < STRELEM_DEFAULT_SIZE * STRELEM_DEFAULT_SIZE; i++) {
					msettings.element.matrix[i % STRELEM_DEFAULT_SIZE][(int)ceil((i + 1.0) / STRELEM_DEFAULT_SIZE) - 1] = param[5].data.d_int8array[i];
				}
				
				msettings.element.size = param[7].data.d_int32;
				
				start_operation(gimp_drawable_get(param[2].data.d_drawable), NULL, msettings);
				break;
				
			default:
			
				values[0].data.d_status = GIMP_PDB_CALLING_ERROR;
				break;
		}
		
		if (status == GIMP_PDB_SUCCESS) {			
			if (run_mode != GIMP_RUN_NONINTERACTIVE) 
				gimp_displays_flush ();
		}
		else {
			status  = GIMP_PDB_EXECUTION_ERROR;
			*nreturn_vals = 2;
			values[1].type = GIMP_PDB_STRING;
			values[1].data.d_string = "Execution error.";
		}
	}

	values[0].data.d_status = status;
}
