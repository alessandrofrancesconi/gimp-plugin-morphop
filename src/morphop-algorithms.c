
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "morphop-algorithms.h"
#include "morphop-gui.h"

#define min(a,b) a < b ? a : b
#define USE_2_7_API (!(defined _WIN32 || (!defined _WIN32 && (GIMP_MAJOR_VERSION == 2) && (GIMP_MINOR_VERSION <= 6))))

#if USE_2_7_API
	// in the new 2.7 API, the function 'gimp_drawable_get_image' has been replaced by 'gimp_item_get_image'
	#define gimp_drawable_get_image gimp_item_get_image
#endif

static void do_morph_operation(MorphOperator, GimpPixelRgn*, GimpPixelRgn*, guchar*, guchar*, StructuringElement, SourceTansformation);
static void do_merge_operation(MergeOperation, GimpPixelRgn*, GimpPixelRgn*, GimpPixelRgn*, guchar*, guchar*, guchar*, SourceTansformation);
static gboolean is_black(GimpPixelRgn*, guchar*);
static void fill_black(GimpPixelRgn*, GimpPixelRgn*, guchar*, guchar*); 
static void prepare_image_buffers(GimpDrawable*, GimpPixelRgn*, GimpPixelRgn*, guchar**, guchar**, int, int, int, int, gboolean);


/* start_operation()
 *  - GimpDrawable *drawable: the original, entire GIMP input drawable 
 *  - GimpPreview *preview: the (optional) preview object. It is != NULL when the operation is used to update the preview window
 *  - MorphOpSettings settings: the settings object
 * 
 * 	Called when the user requests to start a morphological operation. It prepares the graphic buffers (or "regions")
 *  and calls the right operator using the given settings. 
 */
void start_operation(GimpDrawable *drawable, GimpPreview *preview, MorphOpSettings settings)
{
	GimpPixelRgn src_rgn, dst_rgn; // input and output regions
	guchar* src_preview = NULL, *dst_preview = NULL; // preview buffers (used only when preview != NULL)
	int sel_x, sel_y, sel_w, sel_h; // selection boundaries (will work on a subimage)
	
	gboolean is_preview = (preview != NULL);
	
	// init selection boundaries
	if (is_preview) {
		gimp_preview_get_position (preview, &sel_x, &sel_y);
		gimp_preview_get_size (preview, &sel_w, &sel_h);
	}
	else {
		gimp_drawable_mask_intersect (drawable->drawable_id, &sel_x, &sel_y, &sel_w, &sel_h);
		gimp_progress_init (operator_get_string(settings.operator)); // init progress bar
		
		// from this point, subsequent changes to the drawable will result in a unique modification
		// so, to go back, the user will have to press "undo" only once.
		gimp_image_undo_group_start (gimp_drawable_get_image(drawable->drawable_id)); 
	}
	
	// init GIMP tiles cache and buffers
	gimp_tile_cache_ntiles (2 * ((sel_w * drawable->bpp) / gimp_tile_width() + 1));
	prepare_image_buffers(drawable, &src_rgn, &dst_rgn, &src_preview, &dst_preview, sel_x, sel_y, sel_w, sel_h, is_preview);
	
	// start the requested operation...
	
	if (settings.operator == OPERATOR_EROSION) {
		
		do_morph_operation(OPERATOR_EROSION, &src_rgn, &dst_rgn, src_preview, dst_preview, settings.element, SRC_ORIGINAL);
		
	}
	else if (settings.operator == OPERATOR_DILATION) {
		
		do_morph_operation(OPERATOR_DILATION, &src_rgn, &dst_rgn, src_preview, dst_preview, settings.element, SRC_ORIGINAL);
		
	}
	else if (settings.operator == OPERATOR_OPENING) {
		
		// opening is an erosion followed by a dilation
		
		guchar* temp_preview = NULL;
		if (is_preview) {
			// in case of preview, be need an intermediate buffer
			temp_preview = g_new(guchar, sel_w * sel_h * drawable->bpp);
			memcpy(src_preview, src_preview, sel_w * sel_h * drawable->bpp);
		}
		
		// do erosion
		do_morph_operation(OPERATOR_EROSION, &src_rgn, &dst_rgn, src_preview, temp_preview, settings.element, SRC_ORIGINAL);
		
		if (!is_preview) {
			// if not a preview, save the contents of the destination buffer to the real image
			// the subsequent dilation will be applied to the updated drawable
			gimp_drawable_flush (drawable);
			gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
			gimp_drawable_update (drawable->drawable_id, dst_rgn.x, dst_rgn.y, dst_rgn.w, dst_rgn.h);
		}
		
		// now dilate
		do_morph_operation(OPERATOR_DILATION, &src_rgn, &dst_rgn, temp_preview, dst_preview, settings.element, SRC_ORIGINAL);
		
		if (is_preview) g_free(temp_preview);
		
	}
	else if (settings.operator == OPERATOR_CLOSING) {
		
		// closing is the dual of the opening
		
		guchar* temp_preview = NULL;
		if (is_preview) {
			temp_preview = g_new(guchar, sel_w * sel_h * drawable->bpp);
			memcpy(src_preview, src_preview, sel_w * sel_h * drawable->bpp);
		}
		
		do_morph_operation(OPERATOR_DILATION, &src_rgn, &dst_rgn, src_preview, temp_preview, settings.element, SRC_ORIGINAL);
		
		if (!is_preview) {
			gimp_drawable_flush (drawable);
			gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
			gimp_drawable_update (drawable->drawable_id, dst_rgn.x, dst_rgn.y, dst_rgn.w, dst_rgn.h);
		}
		
		do_morph_operation(OPERATOR_EROSION, &src_rgn, &dst_rgn, temp_preview, dst_preview, settings.element, SRC_ORIGINAL);
		
		if (is_preview) g_free(temp_preview);
	}
	else if (settings.operator == OPERATOR_GRADIENT) {
		
		// gradient is an image that is a difference between its eroded and its dilated versions
		
		// need to double the input: the first one will be eroded, the second will be dilated
		int drawable_d_id;
		GimpDrawable* drawable_d;
		GimpPixelRgn src_rgn_d, dst_rgn_d;
		guchar* src_preview_d = NULL, *dst_preview_d = NULL;
		
		drawable_d_id = gimp_layer_new_from_drawable(drawable->drawable_id, gimp_drawable_get_image(drawable->drawable_id));
		drawable_d = gimp_drawable_get(drawable_d_id);
		
		// prepare buffers for the dilation drawable
		prepare_image_buffers(drawable_d, &src_rgn_d, &dst_rgn_d, &src_preview_d, &dst_preview_d, sel_x, sel_y, sel_w, sel_h, is_preview);
		
		do_morph_operation(OPERATOR_EROSION, &src_rgn, &dst_rgn, src_preview, dst_preview, settings.element, SRC_ORIGINAL);
		do_morph_operation(OPERATOR_DILATION, &src_rgn_d, &dst_rgn_d, src_preview_d, dst_preview_d, settings.element, SRC_ORIGINAL);
		
		// save the difference
		do_merge_operation(MERGE_DIFF, &dst_rgn, &dst_rgn_d, &dst_rgn, dst_preview, dst_preview_d, dst_preview, SRC_ORIGINAL);
		
		if (is_preview) {
			g_free(src_preview_d);
			g_free(dst_preview_d);
		}
		else {
			gimp_drawable_detach (drawable_d);
		}
	}
	else if (settings.operator == OPERATOR_BOUNDEXTR) {
		
		// boundary extraction is the difference between the original image and its erosion
		do_morph_operation(OPERATOR_EROSION, &src_rgn, &dst_rgn, src_preview, dst_preview, settings.element, SRC_ORIGINAL);
		do_merge_operation(MERGE_DIFF, &src_rgn, &dst_rgn, &dst_rgn, src_preview, dst_preview, dst_preview, SRC_ORIGINAL);
		
	}
	else if (settings.operator == OPERATOR_HITORMISS) {
		
		// hit-or-miss is an interception between two erosions: the first is the erosion of the original image with
		// the structuring element composed by "white points", the second is the inverted image eroded with "black points" (see the GUI)
		
		StructuringElement B1, B2;
		int drawable_c_id;
		GimpDrawable* drawable_c;
		GimpPixelRgn src_rgn_c, dst_rgn_c;
		guchar* src_preview_c = NULL, *dst_preview_c = NULL;
		
		int i, j;
		// white structuring element
		for(i = 0; i < STRELEM_DEFAULT_SIZE; i++) {
			for(j = 0; j < STRELEM_DEFAULT_SIZE; j++) {
				if (settings.element.matrix[i][j] == 1) B1.matrix[i][j] = 1;
				else B1.matrix[i][j] = 0;
			}
		}
		B1.size = settings.element.size;
		
		// black structuring element
		for(i = 0; i < STRELEM_DEFAULT_SIZE; i++) {
			for(j = 0; j < STRELEM_DEFAULT_SIZE; j++) {
				if (settings.element.matrix[i][j] == 0) B2.matrix[i][j] = 1;
				else B2.matrix[i][j] = 0;
			}
		}
		B2.size = settings.element.size;
		
		// prepare the "complement" drawable
		drawable_c_id = gimp_layer_new_from_drawable(drawable->drawable_id, gimp_drawable_get_image(drawable->drawable_id));
		drawable_c = gimp_drawable_get(drawable_c_id);
		prepare_image_buffers(drawable_c, &src_rgn_c, &dst_rgn_c, &src_preview_c, &dst_preview_c, sel_x, sel_y, sel_w, sel_h, is_preview);
		
		// do erosions
		do_morph_operation(OPERATOR_EROSION, &src_rgn, &dst_rgn, src_preview, dst_preview, B1, SRC_ORIGINAL);
		// the second one is the erosion of the absolute set complement of the source image, so the flag 'SRC_INVERSE'
		do_morph_operation(OPERATOR_EROSION, &src_rgn, &dst_rgn_c, src_preview_c, dst_preview_c, B2, SRC_INVERSE);
		
		// do interception (take only the common values)
		do_merge_operation(MERGE_INTERSEPT, &dst_rgn, &dst_rgn_c, &dst_rgn, dst_preview, dst_preview_c, dst_preview, SRC_ORIGINAL);
		
		if (is_preview) {
			g_free(src_preview_c);
			g_free(dst_preview_c);
		}
		else {
			gimp_drawable_detach (drawable_c);
		}
	}
	else if (settings.operator == OPERATOR_THICKENING) {

		StructuringElement B1, B2;
		int drawable_c_id;
		GimpDrawable* drawable_c;
		GimpPixelRgn src_rgn_c, dst_rgn_c;
		guchar* src_preview_c = NULL, *dst_preview_c = NULL;
		
		int i, j;
		for(i = 0; i < STRELEM_DEFAULT_SIZE; i++) {
			for(j = 0; j < STRELEM_DEFAULT_SIZE; j++) {
				if (settings.element.matrix[i][j] == 1) B1.matrix[i][j] = 1;
				else B1.matrix[i][j] = 0;
			}
		}
		B1.size = settings.element.size;
		
		for(i = 0; i < STRELEM_DEFAULT_SIZE; i++) {
			for(j = 0; j < STRELEM_DEFAULT_SIZE; j++) {
				if (settings.element.matrix[i][j] == 0) B2.matrix[i][j] = 1;
				else B2.matrix[i][j] = 0;
			}
		}
		B2.size = settings.element.size;
		
		drawable_c_id = gimp_layer_new_from_drawable(drawable->drawable_id, gimp_drawable_get_image(drawable->drawable_id));
		drawable_c = gimp_drawable_get(drawable_c_id);
		prepare_image_buffers(drawable_c, &src_rgn_c, &dst_rgn_c, &src_preview_c, &dst_preview_c, sel_x, sel_y, sel_w, sel_h, is_preview);
		
		do_morph_operation(OPERATOR_EROSION, &src_rgn, &dst_rgn, src_preview, dst_preview, B1, SRC_ORIGINAL);
		do_morph_operation(OPERATOR_EROSION, &src_rgn, &dst_rgn_c, src_preview_c, dst_preview_c, B2, SRC_INVERSE);
		
		do_merge_operation(MERGE_INTERSEPT, &dst_rgn, &dst_rgn_c, &dst_rgn, dst_preview, dst_preview_c, dst_preview, SRC_ORIGINAL);
		do_merge_operation(MERGE_UNION, &src_rgn, &dst_rgn, &dst_rgn, src_preview, dst_preview, dst_preview, SRC_ORIGINAL);
		
		if (is_preview) {
			g_free(src_preview_c);
			g_free(dst_preview_c);
		}
		else {
			gimp_drawable_detach (drawable_c);
		}
	
	}
	else if (settings.operator == OPERATOR_THINNING) {
	
		StructuringElement B1, B2;
		int drawable_c_id;
		GimpDrawable* drawable_c;
		GimpPixelRgn src_rgn_c, dst_rgn_c;
		guchar* src_preview_c = NULL, *dst_preview_c = NULL;
		
		int i, j;
		for(i = 0; i < STRELEM_DEFAULT_SIZE; i++) {
			for(j = 0; j < STRELEM_DEFAULT_SIZE; j++) {
				if (settings.element.matrix[i][j] == 1) B1.matrix[i][j] = 1;
				else B1.matrix[i][j] = 0;
			}
		}
		B1.size = settings.element.size;
		
		for(i = 0; i < STRELEM_DEFAULT_SIZE; i++) {
			for(j = 0; j < STRELEM_DEFAULT_SIZE; j++) {
				if (settings.element.matrix[i][j] == 0) B2.matrix[i][j] = 1;
				else B2.matrix[i][j] = 0;
			}
		}
		B2.size = settings.element.size;
		
		drawable_c_id = gimp_layer_new_from_drawable(drawable->drawable_id, gimp_drawable_get_image(drawable->drawable_id));
		drawable_c = gimp_drawable_get(drawable_c_id);
		prepare_image_buffers(drawable_c, &src_rgn_c, &dst_rgn_c, &src_preview_c, &dst_preview_c, sel_x, sel_y, sel_w, sel_h, is_preview);
		
		do_morph_operation(OPERATOR_EROSION, &src_rgn, &dst_rgn, src_preview, dst_preview, B1, SRC_ORIGINAL);
		do_morph_operation(OPERATOR_EROSION, &src_rgn, &dst_rgn_c, src_preview_c, dst_preview_c, B2, SRC_INVERSE);
		
		do_merge_operation(MERGE_INTERSEPT, &dst_rgn, &dst_rgn_c, &dst_rgn, dst_preview, dst_preview_c, dst_preview, SRC_ORIGINAL);
		do_merge_operation(MERGE_DIFF, &src_rgn, &dst_rgn, &dst_rgn, src_preview, dst_preview, dst_preview, SRC_ORIGINAL);
		
		if (is_preview) {
			g_free(src_preview_c);
			g_free(dst_preview_c);
		}
		else {
			gimp_drawable_detach (drawable_c);
		}
	
	}
	else if (settings.operator == OPERATOR_SKELETON) {
		
		// skeletonization follows this algorithm:
		/*	
			skeleton = black_image();
			do
			{
				eroded = erode(img);
				opened = dilate(eroded);
				diff = img - opened;
				skeleton = skeleton U diff;
				img = eroded;
			} while (is_black(img) == FALSE);
		*/
		
		// we need some temporary buffers...
		// ...for erosions
		int drawable_erod_id;
		GimpDrawable* drawable_erod;
		GimpPixelRgn src_rgn_erod, dst_rgn_erod;
		guchar* src_preview_erod = NULL, *dst_preview_erod = NULL;
		
		drawable_erod_id = gimp_layer_new_from_drawable(drawable->drawable_id, gimp_drawable_get_image(drawable->drawable_id));
		drawable_erod = gimp_drawable_get(drawable_erod_id);
		prepare_image_buffers(drawable_erod, &src_rgn_erod, &dst_rgn_erod, &src_preview_erod, &dst_preview_erod, sel_x, sel_y, sel_w, sel_h, is_preview);
		if (!preview) {
			#if USE_2_7_API
				// in the new 2.7 API, the function 'gimp_image_add_layer' has been replaced by 'gimp_image_insert_layer'
				gimp_image_insert_layer (gimp_drawable_get_image (drawable_erod->drawable_id), drawable_erod_id, 0, 0);
			#else
				gimp_image_add_layer (gimp_drawable_get_image (drawable_erod->drawable_id), drawable_erod_id, 0);
			#endif
		}
	
		// ...for opening and difference
		int drawable_open_id;
		GimpDrawable* drawable_open;
		GimpPixelRgn src_rgn_open, dst_rgn_open;
		guchar* src_preview_open = NULL, *dst_preview_open = NULL;
		
		drawable_open_id = gimp_layer_new_from_drawable(drawable->drawable_id, gimp_drawable_get_image(drawable->drawable_id));
		drawable_open = gimp_drawable_get(drawable_open_id);
		prepare_image_buffers(drawable_open, &src_rgn_open, &dst_rgn_open, &src_preview_open, &dst_preview_open, sel_x, sel_y, sel_w, sel_h, is_preview);
		
		// the destination region will be the final skeleton. 
		// start filling it black...
		fill_black(&src_rgn, &dst_rgn, src_preview, dst_preview);
		
		do {
			// eroded = erosion(img) [must threshold 'img'!]
			do_morph_operation(OPERATOR_EROSION, &src_rgn_erod, &dst_rgn_erod, src_preview_erod, dst_preview_erod, settings.element, SRC_THRESHOLD);
			// open = dilate(eroded)
			do_morph_operation(OPERATOR_DILATION, &dst_rgn_erod, &dst_rgn_open, dst_preview_erod, dst_preview_open, settings.element, SRC_ORIGINAL);
			
			// diff = img - open [must threshold 'img'!]
			do_merge_operation(MERGE_DIFF, &src_rgn_erod, &dst_rgn_open, &dst_rgn_open, src_preview_erod, dst_preview_open, dst_preview_open, SRC_THRESHOLD);
			
			// skel = skel U diff
			do_merge_operation(MERGE_UNION, &dst_rgn, &dst_rgn_open, &dst_rgn, dst_preview, dst_preview_open, dst_preview, SRC_ORIGINAL);
			
			// must update the eroded drawable for the next iteration
			if (!is_preview) {
				gimp_drawable_flush (drawable_erod);
				gimp_drawable_merge_shadow (drawable_erod->drawable_id, FALSE);
				gimp_drawable_update (drawable_erod_id, dst_rgn.x, dst_rgn.y, dst_rgn.w, dst_rgn.h);
				prepare_image_buffers(drawable_erod, &src_rgn_erod, &dst_rgn_erod, &src_preview_erod, &dst_preview_erod, sel_x, sel_y, sel_w, sel_h, is_preview);
			}
			else {
				memcpy(src_preview_erod, dst_preview_erod, sel_w * sel_h * drawable->bpp);
			}
		}
		while (!is_black(&src_rgn_erod, src_preview_erod)); // algorithm ends when the eroded image becomes totally black
		
		// here: dst_rgn/dst_preview is the final skeleton
		
		if (is_preview) {
			g_free(src_preview_erod);
			g_free(dst_preview_erod);
			g_free(src_preview_open);
			g_free(dst_preview_open);
		}
		else {
			gimp_image_remove_layer (gimp_drawable_get_image (drawable_erod->drawable_id), drawable_erod_id);
			gimp_drawable_detach (drawable_erod);
			gimp_drawable_detach (drawable_open);
		}
	
	}
	else if (settings.operator == OPERATOR_WTOPHAT) {
		
		// white top-hat is the difference between the original image and its opening
		int drawable_o_id;
		GimpDrawable* drawable_o;
		GimpPixelRgn src_rgn_o, dst_rgn_o;
		guchar* src_preview_o = NULL, *dst_preview_o = NULL;
		
		drawable_o_id = gimp_layer_new_from_drawable(drawable->drawable_id, gimp_drawable_get_image(drawable->drawable_id));
		drawable_o = gimp_drawable_get(drawable_o_id);
		prepare_image_buffers(drawable_o, &src_rgn_o, &dst_rgn_o, &src_preview_o, &dst_preview_o, sel_x, sel_y, sel_w, sel_h, is_preview);
		
		// create opening
		do_morph_operation(OPERATOR_EROSION, &src_rgn, &dst_rgn_o, src_preview, dst_preview_o, settings.element, SRC_ORIGINAL);
		do_morph_operation(OPERATOR_DILATION, &dst_rgn_o, &dst_rgn, dst_preview_o, dst_preview, settings.element, SRC_ORIGINAL);
		
		// and subtract it to the original image
		do_merge_operation(MERGE_DIFF, &src_rgn, &dst_rgn, &dst_rgn, src_preview, dst_preview, dst_preview, SRC_ORIGINAL);
		
		if (is_preview) {
			g_free(src_preview_o);
			g_free(dst_preview_o);
		}
		else {
			gimp_drawable_detach (drawable_o);
		}
		
	}
	else if (settings.operator == OPERATOR_BTOPHAT) {
		
		// black top-hat is the difference between the closing and the original image		
		int drawable_c_id;
		GimpDrawable* drawable_c;
		GimpPixelRgn src_rgn_c, dst_rgn_c;
		guchar* src_preview_c = NULL, *dst_preview_c = NULL;
		
		drawable_c_id = gimp_layer_new_from_drawable(drawable->drawable_id, gimp_drawable_get_image(drawable->drawable_id));
		drawable_c = gimp_drawable_get(drawable_c_id);
		prepare_image_buffers(drawable_c, &src_rgn_c, &dst_rgn_c, &src_preview_c, &dst_preview_c, sel_x, sel_y, sel_w, sel_h, is_preview);
		
		do_morph_operation(OPERATOR_DILATION, &src_rgn, &dst_rgn_c, src_preview, dst_preview_c, settings.element, SRC_ORIGINAL);
		do_morph_operation(OPERATOR_EROSION, &dst_rgn_c, &dst_rgn, dst_preview_c, dst_preview, settings.element, SRC_ORIGINAL);
		
		do_merge_operation(MERGE_DIFF, &dst_rgn, &src_rgn, &dst_rgn, dst_preview, src_preview, dst_preview, SRC_ORIGINAL);
		
		if (is_preview) {
			g_free(src_preview_c);
			g_free(dst_preview_c);
		}
		else {
			gimp_drawable_detach (drawable_c);
		}
		
	}
	
	// end of the chosen operation, now save it back...
	
	if (is_preview) {
		// if preview, simply write to the preview object
		gimp_preview_draw_buffer (preview, dst_preview, dst_rgn.w * dst_rgn.bpp);
		g_free(src_preview);
		g_free(dst_preview);
	}
	else {
		// if direct manipulation, merge all the changes to the screen
		gimp_drawable_flush (drawable);
		gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
		gimp_drawable_update (drawable->drawable_id, dst_rgn.x, dst_rgn.y, dst_rgn.w, dst_rgn.h);
		
		// also finalize the progress bar and close the undo group
		gimp_progress_update(1.0);
		gimp_image_undo_group_end (gimp_drawable_get_image(drawable->drawable_id));
		gimp_drawable_detach (drawable);
	}
}

/* do_morph_operation()
 * 
 * Executes erosion or dilation using the given structuring element
 * 
 *	- MorphOperator op: the operator, it can be OPERATOR_EROSION or OPERATOR_DILATION
 *  - GimpPixelRgn* src: source region
 *  - GimpPixelRgn* dst: destination region
 *  - guchar* src_prev: source preview buffer (if any)
 *  - guchar* dst_prev: destination preview buffer
 *  - StructuringElement element: the structuring element
 *  - SourceTansformation srctransf: a "source preprocessing" option, it can be:
 *		- SRC_ORIGINAL (leaves source unchanged)
 *		- SRC_INVERSE (invert source's colors, used by Hit-or-Miss)
 *		- SRC_THRESHOLD (makes a threshold, if color < 127 => 0, else => 1, used by Skeletonization)
 */
static void do_morph_operation(
	MorphOperator op, 
	GimpPixelRgn *src, GimpPixelRgn *dst, 
	guchar* src_prev, guchar* dst_prev, 
	StructuringElement element,
	SourceTansformation srctransf
) {
	if (!(
		op == OPERATOR_EROSION || 
		op == OPERATOR_DILATION
	)) return; // this function works only in operations derived from erosion or dilation
	
	// setting actual structuring element size
	unsigned int final_elem_size;
	if (element.size == SIZE_3x3) final_elem_size = 3;
	if (element.size == SIZE_5x5) final_elem_size = 5;
	if (element.size == SIZE_7x7) final_elem_size = 7;
	if (element.size == SIZE_9x9) final_elem_size = 9;
	if (element.size == SIZE_11x11) final_elem_size = 11;
	
	float scale = (float)STRELEM_DEFAULT_SIZE / final_elem_size;
	int elem_center = ceil(final_elem_size / 2); // coordinates of the center element in the matrix
	unsigned int i, y, x;	
	
	GimpImageType image_type = gimp_drawable_type (src->drawable->drawable_id);
	gboolean is_preview = (src_prev != NULL || dst_prev != NULL);
	
	guchar row_buffer[src->w * src->bpp]; // will store the results, they are written row-by-row 
	guchar row_buffer_mask[final_elem_size][src->w * src->bpp]; // will store the input pixel in a "big" row (for masking)
	
	guchar this_pixel[src->bpp]; // the visited pixel
	guchar this_lum; // the luminosity of the visited pixel
	guchar best_pixel[src->bpp]; // the best pixel found to be copied on the center pixel
	unsigned int best_lum; // the luminosity of the best pixel
	
	// start looping on image
	for (y = src->y; y < src->y + src->h; y ++) {
		
		// simply update the progress bar every 50 rows
		if (!is_preview && y % 50 == 0) gimp_progress_update ((double)y / (src->y + src->h)); 
		
		// extracts input pixel for masking
		int row_offset;
		for (row_offset = -elem_center; row_offset <= elem_center; row_offset++) {
			
			int this_row = y + row_offset;
			if (this_row >= src->y && this_row < src->y + src->h) {
				
				// difference if in "preview" or "direct" mode: if preview, will read the inputs from the src_preview buffer
				// else, takes input pixel directly from the drawable
				if (is_preview) {
					memcpy(&row_buffer_mask[row_offset + elem_center], &src_prev[(this_row - src->y) * src->w * src->bpp], src->w * src->bpp);
				}
				else {
					gimp_pixel_rgn_get_row (
						src, 
						row_buffer_mask[row_offset + elem_center], 
						src->x, 
						this_row, 
						src->w
					);
				}
			}
			else {
				// case in which we are outside the image: the input row is filled with useless pixels
				for (i = 0; i < src->w * src->bpp; i++) {
					row_buffer_mask[row_offset + elem_center][i] = (op == OPERATOR_EROSION ? 255 : 0);
				}
			}
		}
		
		// start looping on each pixel in this row
		for (x = 0; x < src->w; x++) {
			
			best_lum = -1; // reset the best luminosity
			
			// start looping on neighbors of pixel x;y
			unsigned short mask_x, mask_y, neigh_x, neigh_y, mask_i, mask_j;
			for (mask_y = 0; mask_y < final_elem_size; mask_y++) {
				for (mask_x = 0; mask_x < final_elem_size; mask_x++) {
					
					// coordinates of this neighbor relative to row_buffer_mask
					neigh_x = (x + mask_x - elem_center) * src->bpp;
					neigh_y = mask_y;
					
					mask_i = floor(mask_y * scale);
					mask_j = floor(mask_x * scale);
					
					// this neighbor will be considered only if...
					if (
						// it's part of the structuring element (it's not a black spot)
						element.matrix[mask_i][mask_j] != 0 &&
						// and it's inside the image bounds
						neigh_x >= 0 &&
						neigh_x < src->w * src->bpp						
					){
						// get this pixel's values
						for(i = 0; i < src->bpp; i++) {
							if (srctransf == SRC_ORIGINAL || srctransf == SRC_THRESHOLD) this_pixel[i] = row_buffer_mask[neigh_y][neigh_x + i];
							else if (srctransf == SRC_INVERSE) this_pixel[i] = abs(row_buffer_mask[neigh_y][neigh_x + i] - 255);
						}
						
						// and calc its luminosity (different ways for different image types)
						if (image_type == GIMP_RGB_IMAGE || image_type == GIMP_RGBA_IMAGE) {
							this_lum = this_pixel[0] * 0.2126 + this_pixel[1] * 0.7152 + this_pixel[2] * 0.0722;
							if (srctransf == SRC_THRESHOLD) {
								this_lum = (this_lum < 127 ? 0 : 255);
								this_pixel[0] = this_pixel[1] = this_pixel[2] = this_lum;
							}
						}
						else {
							this_lum = this_pixel[0];
							if (srctransf == SRC_THRESHOLD) {
								this_lum = (this_lum < 127 ? 0 : 255);
								this_pixel[0] = this_lum;
							}
						}
						
						// now check if this luminosity is better than the best found so far
						if (
							(best_lum == -1) ||
							(op == OPERATOR_EROSION && this_lum < best_lum) ||	// erosion takes the darkest pixel
							(op == OPERATOR_DILATION && this_lum > best_lum) 	// dilation takes the brightest
						) {
							// if better, save it
							for(i = 0; i < src->bpp; i++) {
								best_pixel[i] = this_pixel[i];
							}
							best_lum = this_lum;
							
							// check if the very best value have been reached before the end
							// this should gain some speed...
							if (
								(op == OPERATOR_EROSION && best_lum == 0) ||
								(op == OPERATOR_DILATION && best_lum == 255)
							) {
								goto end_mask;
							} 
							
						}
					}
				}
			}
			
end_mask:
			
			// finished checking pixels in the mask, now set the center to the best value found
			if (best_lum == -1) {
				// error case: a valid pixel was not found. For example, scaling a small structuring element 
				// to small dimensions could produce a totally black matrix. Solution: pixels don't change.
				for(i = 0; i < src->bpp; i++) {
					// copy the original
					row_buffer[x * src->bpp + i] = row_buffer_mask[elem_center][x * src->bpp + i];
				}
			}
			else {
				for(i = 0; i < src->bpp; i++) {
					row_buffer[x * src->bpp + i] = best_pixel[i];
				}
			}
		}
		
		// Finished checking this row, save it now in the preview buffer (for the GimpPreview widget), or in the real drawable
		if (is_preview) {
			memcpy(&dst_prev[(y - src->y) * src->w * src->bpp], row_buffer, src->w * src->bpp);
		}
		else {
			gimp_pixel_rgn_set_row (dst, row_buffer, src->x, y, src->w);
		}
	}
}

/* do_merge_operation()
 * 
 * It saves an image that is a merge between the two inputs A and B. Merge can be 
 * - a "difference" between the two inputs (intended as abs(Ai - Bi)), where Ai is the ith pixel's value of A and Bi is the correspondent on B,
 * - an "union" (min(Ai - Bi, 255)),
 * - an "interseption", taking only the common pixels and setting the others to black.
 * 
 *	- MergeOperation op: the merge operation, it can be MERGE_DIFF, MERGE_UNION or MERGE_INTERSEPT
 *  - GimpPixelRgn* a: first input region
 *  - GimpPixelRgn* b: second input region
 *  - GimpPixelRgn* dst: destination region
 *  - guchar* a_prev: first input preview buffer (if any)
 *  - guchar* b_prev: second input preview buffer
 *  - guchar* dst_prev: destination preview buffer
 *  - SourceTansformation srctransf: a "source preprocessing" option applied to a/a_prev, see do_morph_operation()
 */
static void do_merge_operation(
	MergeOperation op, 
	GimpPixelRgn* a, GimpPixelRgn* b, GimpPixelRgn* dst, 
	guchar* a_prev, guchar* b_prev, 
	guchar* dst_prev,
	SourceTansformation srctransf
){
	gboolean is_preview = (a_prev != NULL || b_prev != NULL);
	guchar row_buffer_a[a->w * a->bpp];
	guchar row_buffer_b[b->w * b->bpp];
	
	GimpImageType image_type = gimp_drawable_type (a->drawable->drawable_id);
	
	unsigned int x, y, i;
	int ignore_alpha = gimp_drawable_has_alpha(a->drawable->drawable_id) ? 1 : 0;
	
	for (y = a->y; y < a->y + a->h; y ++) {
		
		if (!is_preview && y % 50 == 0) gimp_progress_update ((double)y / (a->y + a->h)); 
		
		if (is_preview) {
			memcpy(&row_buffer_a, &a_prev[(y - a->y) * a->w * a->bpp], a->w * a->bpp);
			memcpy(&row_buffer_b, &b_prev[(y - b->y) * b->w * b->bpp], b->w * b->bpp);
		}
		else {
			gimp_pixel_rgn_get_row (
				a, 
				row_buffer_a, 
				a->x, 
				y, 
				a->w
			);
			
			gimp_pixel_rgn_get_row (
				b, 
				row_buffer_b, 
				b->x, 
				y, 
				b->w
			);
		}
		
		for (x = 0; x < a->w; x++) {
		
			// preoprocessing on a/a_prev
			if (srctransf == SRC_THRESHOLD) {
				for(i = 0; i < a->bpp - ignore_alpha; i++) {
					if (image_type == GIMP_RGB_IMAGE || image_type == GIMP_RGBA_IMAGE) {
						unsigned char this_lum = row_buffer_a[x * a->bpp + 0] * 0.2126 + row_buffer_a[x * a->bpp + 1] * 0.7152 + row_buffer_a[x * a->bpp + 2] * 0.0722;
						row_buffer_a[x * a->bpp + 0] = row_buffer_a[x * a->bpp + 1] = row_buffer_a[x * a->bpp + 2] = (this_lum < 127 ? 0 : 255);
						break;
					}
					else {
						row_buffer_a[x * a->bpp + i] = row_buffer_a[x * a->bpp + i] < 127 ? 0 : 255;
					}
				}
			}
		
			// merge here, skipping alpha channel if present
			for(i = 0; i < a->bpp - ignore_alpha; i++) {
				if (op == MERGE_DIFF) {
					row_buffer_a[x * a->bpp + i] = abs(row_buffer_a[x * a->bpp + i] - row_buffer_b[x * a->bpp + i]);
				}
				else if (op == MERGE_UNION) {
					row_buffer_a[x * a->bpp + i] = min(row_buffer_a[x * a->bpp + i] + row_buffer_b[x * a->bpp + i], 255);
				}
				else if (op == MERGE_INTERSEPT) {
					// guchar inter_pixel[a->bpp]; ??
					if (row_buffer_a[x * a->bpp + i] != row_buffer_b[x * b->bpp + i]) {
						row_buffer_a[x * a->bpp + i] = 0;
					}
				}
			}
		}
		
		if (is_preview) {
			memcpy(&dst_prev[(y - a->y) * a->w * a->bpp], row_buffer_a, a->w * a->bpp);
		}
		else {
			gimp_pixel_rgn_set_row (dst, row_buffer_a, a->x, y, a->w);
		}
	}
}

static gboolean is_black(GimpPixelRgn* rgn, guchar* prev) 
{
	gboolean is_preview = (prev != NULL);
	guchar row_buffer[rgn->w * rgn->bpp];
	
	unsigned int x, y, i, tot_color;
	int ignore_alpha = gimp_drawable_has_alpha(rgn->drawable->drawable_id) ? 1 : 0;
	
	for (y = rgn->y; y < rgn->y + rgn->h; y ++) {
		
		if (!is_preview && y % 50 == 0) gimp_progress_update ((double)y / (rgn->y + rgn->h)); 
		
		if (is_preview) {
			memcpy(&row_buffer, &prev[(y - rgn->y) * rgn->w * rgn->bpp], rgn->w * rgn->bpp);
		}
		else {
			gimp_pixel_rgn_get_row (
				rgn, 
				row_buffer, 
				rgn->x, 
				y, 
				rgn->w
			);
		}
		
		for (x = 0; x < rgn->w; x++) {
			tot_color = 0;
			for(i = 0; i < rgn->bpp - ignore_alpha; i++) {
				tot_color += row_buffer[x * rgn->bpp + i];
			}
			if (tot_color != 0) return FALSE;
		}
	}
	return TRUE;
}

static void fill_black(GimpPixelRgn* src, GimpPixelRgn* dst, guchar* src_prev, guchar* dst_prev) 
{
	gboolean is_preview = (src_prev != NULL || dst_prev != NULL);
	guchar row_buffer[src->w * src->bpp];
	
	unsigned int x, y, i;
	int ignore_alpha = gimp_drawable_has_alpha(src->drawable->drawable_id) ? 1 : 0;
	
	for (y = src->y; y < src->y + src->h; y ++) {
		
		if (!is_preview && y % 50 == 0) gimp_progress_update ((double)y / (src->y + src->h)); 
		
		if (is_preview) {
			memcpy(&row_buffer, &src_prev[(y - src->y) * src->w * src->bpp], src->w * src->bpp);
		}
		else {
			gimp_pixel_rgn_get_row (
				src, 
				row_buffer, 
				src->x, 
				y, 
				src->w
			);
		}
		
		for (x = 0; x < src->w; x++) {
			for(i = 0; i < src->bpp - ignore_alpha; i++) {
				row_buffer[x * src->bpp + i] = 0;
			}
		}
		
		if (is_preview) {
			memcpy(&dst_prev[(y - src->y) * src->w * src->bpp], row_buffer, src->w * src->bpp);
		}
		else {
			gimp_pixel_rgn_set_row (dst, row_buffer, src->x, y, src->w);
		}
	}
}

/* prepare_image_buffers()
 * 
 * Inits all the buffers so they can be read and written by GIMP
 * 
 * - GimpDrawable* drawable: the input drawable
 * - GimpPixelRgn* src_rgn, GimpPixelRgn* dst_rgn: input and destination regions (to be initialized) 
 * - guchar** src_preview, guchar** dst_preview: input and destination preview buffers (to be initialized) 
 * - int sel_x, int sel_y, int sel_w, int sel_h: selection boundaries
 * - gboolean is_preview: initialize preview buffers?
 */
static void prepare_image_buffers(
	GimpDrawable* drawable,
	GimpPixelRgn* src_rgn, GimpPixelRgn* dst_rgn, 
	guchar** src_preview, guchar** dst_preview,
	int sel_x, int sel_y, int sel_w, int sel_h,
	gboolean is_preview
){
	gimp_pixel_rgn_init (
		src_rgn, 
		drawable,
		sel_x, sel_y, 
		sel_w, sel_h, 
		FALSE, FALSE
	);

	gimp_pixel_rgn_init (
		dst_rgn, 
		drawable,
		sel_x, sel_y, 
		sel_w, sel_h,
		!is_preview, TRUE
	);
	
	if (is_preview) {
		*src_preview = g_new(guchar, sel_w * sel_h * drawable->bpp);
		*dst_preview = g_new(guchar, sel_w * sel_h * drawable->bpp);
		
		gimp_pixel_rgn_get_rect (
			src_rgn,
			*src_preview,
			sel_x, sel_y,
			sel_w, sel_h
		);
		
		memcpy(*dst_preview, *src_preview, sel_w * sel_h * drawable->bpp);
	}
	else {
		*src_preview = NULL;
		*dst_preview = NULL;
	}
}
