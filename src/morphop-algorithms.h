#ifndef __MORPHOP_ALGORITHMS_H__
#define __MORPHOP_ALGORITHMS_H__

#include <gtk/gtk.h>
#include <libgimp/gimpui.h>

#define STRELEM_DEFAULT_SIZE 7

typedef enum {
	OPERATOR_EROSION = 0,
	OPERATOR_DILATION,
	OPERATOR_OPENING,
	OPERATOR_CLOSING,
	OPERATOR_BOUNDEXTR,
	OPERATOR_GRADIENT,
	OPERATOR_HITORMISS,
	OPERATOR_SKELETON,
	OPERATOR_THICKENING,
	OPERATOR_THINNING,
	OPERATOR_WTOPHAT,
	OPERATOR_BTOPHAT,
	
	OPERATOR_END
} MorphOperator;

typedef enum {
	SRC_ORIGINAL = 0,
	SRC_INVERSE,
	SRC_THRESHOLD,
	
	SRC_END
} SourceTansformation;

typedef enum {
	MERGE_DIFF = 0,
	MERGE_UNION,
	MERGE_INTERSEPT,
	
	MERGE_END
} MergeOperation;

typedef enum {
	SIZE_3x3 = 0,
	SIZE_5x5,
	SIZE_7x7,
	SIZE_9x9,
	SIZE_11x11,
	
	SIZE_END
} ElementSize;


typedef struct {
	signed char matrix[STRELEM_DEFAULT_SIZE][STRELEM_DEFAULT_SIZE];
	ElementSize size;
} StructuringElement;

typedef struct {
	MorphOperator operator;
	StructuringElement element;
} MorphOpSettings;

void start_operation(GimpDrawable*, GimpPreview*, MorphOpSettings);

#endif
