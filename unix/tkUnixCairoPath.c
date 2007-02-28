/*
 * tkUnixCairoPath.c --
 *
 *	This file implements path drawing API's using the Cairo rendering engine.
 *
 *  TODO: implement text drawing using glyphs instead of the "toy" text API.
 *
 * Copyright (c) 2005-2007  Mats Bengtsson
 *
 * $Id$
 */

#include <cairo.h>
#include <cairo-xlib.h>
#include <tkUnixInt.h>
#include "tkPath.h"
#include "tkIntPath.h"

#define BlueDoubleFromXColorPtr(xc)   (double) (((xc)->pixel & 0xFF)) / 255.0
#define GreenDoubleFromXColorPtr(xc)  (double) ((((xc)->pixel >> 8) & 0xFF)) / 255.0
#define RedDoubleFromXColorPtr(xc)    (double) ((((xc)->pixel >> 16) & 0xFF)) / 255.0

extern int gUseAntiAlias;
extern int gSurfaceCopyPremultiplyAlpha;
extern Tcl_Interp *gInterp;

int kPathSmallEndian = 1;	/* Hardcoded. */

/* @@@ Need to use cairo_image_surface_create_for_data() here since prior to 1.2
 *     there doesn't exist any cairo_image_surface_get_data() accessor. 
 */
typedef struct PathSurfaceCairoRecord {
    unsigned char 	*data;
    cairo_format_t 	format;
    int 			width;
    int				height;
    int 			stride;		/* the number of bytes between the start of rows in the buffer */
} PathSurfaceCairoRecord;

/*
 * This is used as a place holder for platform dependent stuff between each call.
 */
typedef struct TkPathContext_ {
    cairo_t*	 			c;
    cairo_surface_t* 		surface;
    PathSurfaceCairoRecord*	record;		/* NULL except for memory surfaces. 
                                         * Skip when cairo 1.2 widely spread. */
} TkPathContext_;


void CairoSetFill(TkPathContext ctx, Tk_PathStyle *style)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_set_source_rgba(context->c,
            RedDoubleFromXColorPtr(style->fillColor),
            GreenDoubleFromXColorPtr(style->fillColor),
            BlueDoubleFromXColorPtr(style->fillColor),
            style->fillOpacity);
    cairo_set_fill_rule(context->c, 
            (style->fillRule == WindingRule) ? CAIRO_FILL_RULE_WINDING : CAIRO_FILL_RULE_EVEN_ODD);
}

TkPathContext TkPathInit(Tk_Window tkwin, Drawable d)
{
    cairo_t *c;
    cairo_surface_t *surface;
    TkPathContext_ *context = (TkPathContext_ *) ckalloc((unsigned) (sizeof(TkPathContext_)));
    surface = cairo_xlib_surface_create(Tk_Display(tkwin), d, Tk_Visual(tkwin), Tk_Width(tkwin), Tk_ReqHeight(tkwin));
    c = cairo_create(surface);
    context->c = c;
    context->surface = surface;
    context->record = NULL;
    return (TkPathContext) context;
}

TkPathContext TkPathInitSurface(int width, int height)
{
    cairo_t *c;
    cairo_surface_t *surface;
    unsigned char *data;
    int stride;
    
    /* @@@ Need to use cairo_image_surface_create_for_data() here since prior to 1.2
     *     there doesn't exist any cairo_image_surface_get_data() accessor. 
     */
    TkPathContext_ *context = (TkPathContext_ *) ckalloc((unsigned) (sizeof(TkPathContext_)));
    PathSurfaceCairoRecord *record = (PathSurfaceCairoRecord *) ckalloc((unsigned) (sizeof(PathSurfaceCairoRecord)));
    stride = 4*width;
    /* Round up to nearest multiple of 16 */
    stride = (stride + (16-1)) & ~(16-1);
    data = (unsigned char *) ckalloc(height*stride);
    memset(data, '\0', height*stride);
    surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, width, height, stride);
    record->data = data;
    record->format = CAIRO_FORMAT_ARGB32;
    record->width = width;
    record->height = height;
    record->stride = stride;
    c = cairo_create(surface);
    context->c = c;
    context->surface = surface;
    context->record = record;
    return (TkPathContext) context;
}

void TkPathPushTMatrix(TkPathContext ctx, TMatrix *m)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_matrix_t matrix;
    if (m == NULL) {
        return;
    }
    cairo_matrix_init(&matrix, m->a, m->b, m->c, m->d, m->tx, m->ty);
    cairo_transform(context->c, &matrix);
}

void TkPathSaveState(TkPathContext ctx)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_save(context->c);
}

void TkPathRestoreState(TkPathContext ctx)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_restore(context->c);
}

void TkPathBeginPath(TkPathContext ctx, Tk_PathStyle *style)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_new_path(context->c);
}

void TkPathMoveTo(TkPathContext ctx, double x, double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_move_to(context->c, x, y);
}

void TkPathLineTo(TkPathContext ctx, double x, double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_line_to(context->c, x, y);
}

void TkPathQuadBezier(TkPathContext ctx, double ctrlX, double ctrlY, double x, double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    double cx, cy;
    double x31, y31, x32, y32;
    
    cairo_get_current_point(context->c, &cx, &cy);

    // conversion of quadratic bezier curve to cubic bezier curve: (mozilla/svg)
    /* Unchecked! Must be an approximation! */
    x31 = cx + (ctrlX - cx) * 2 / 3;
    y31 = cy + (ctrlY - cy) * 2 / 3;
    x32 = ctrlX + (x - ctrlX) / 3;
    y32 = ctrlY + (y - ctrlY) / 3;

    cairo_curve_to(context->c, x31, y31, x32, y32, x, y);
}

void TkPathCurveTo(TkPathContext ctx, double x1, double y1, 
        double x2, double y2, double x, double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_curve_to(context->c, x1, y1, x2, y2, x, y);
}

void TkPathArcTo(TkPathContext ctx,
        double rx, double ry, 
        double phiDegrees, 	/* The rotation angle in degrees! */
        char largeArcFlag, char sweepFlag, double x, double y)
{
    TkPathArcToUsingBezier(ctx, rx, ry, phiDegrees, largeArcFlag, sweepFlag, x, y);
}

void
TkPathRect(TkPathContext ctx, double x, double y, double width, double height)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_rectangle(context->c, x, y, width, height);
}

void
TkPathOval(TkPathContext ctx, double cx, double cy, double rx, double ry)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if (rx == ry) {
        cairo_move_to(context->c, cx+rx, cy);
        cairo_arc(context->c, cx, cy, rx, 0.0, 2*M_PI);
        cairo_close_path(context->c);
    } else {
        cairo_save(context->c);
        cairo_translate(context->c, cx, cy);
        cairo_scale(context->c, rx, ry);
        cairo_move_to(context->c, 1.0, 0.0);
        cairo_arc(context->c, 0.0, 0.0, 1.0, 0.0, 2*M_PI);
        cairo_close_path(context->c);
        cairo_restore(context->c);
    }
}

void
TkPathImage(TkPathContext ctx, Tk_Image image, Tk_PhotoHandle photo, 
        double x, double y, double width, double height)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    Tk_PhotoImageBlock block;
    cairo_surface_t *surface;
    cairo_format_t format;
    unsigned char *data = NULL;
    unsigned char *ptr = NULL;
    unsigned char *srcPtr, *dstPtr;
    int srcR, srcG, srcB, srcA;		/* The source pixel offsets. */
    int dstR, dstG, dstB, dstA;		/* The destination pixel offsets. */
    int size, pitch;
    int iwidth, iheight;
    int i, j;

    /* Return value? */
    Tk_PhotoGetImage(photo, &block);
    size = block.pitch * block.height;
    iwidth = block.width;
    iheight = block.height;
    pitch = block.pitch;
    if (width == 0.0) {
        width = (double) iwidth;
    }
    if (height == 0.0) {
        height = (double) iheight;
    }
    
    /*
     * @format: the format of pixels in the buffer
     * @width: the width of the image to be stored in the buffer
     * @height: the eight of the image to be stored in the buffer
     * @stride: the number of bytes between the start of rows
     *   in the buffer. Having this be specified separate from @width
     *   allows for padding at the end of rows, or for writing
     *   to a subportion of a larger image.
     */
     
    /**
     * cairo_format_t
     * @CAIRO_FORMAT_ARGB32: each pixel is a 32-bit quantity, with
     *   alpha in the upper 8 bits, then red, then green, then blue.
     *   The 32-bit quantities are stored native-endian. Pre-multiplied
     *   alpha is used. (That is, 50% transparent red is 0x80800000,
     *   not 0x80ff0000.)
     */
    if (block.pixelSize*8 == 32) {
        format = CAIRO_FORMAT_ARGB32;
        
        /* The offset array contains the offsets from the address of a 
         * pixel to the addresses of the bytes containing the red, green, 
         * blue and alpha (transparency) components.
         *
         * We need to copy pixel data from the source using the photo offsets
         * to cairos ARGB format which is in *native* endian order; Switch!
         */
        srcR = block.offset[0];
        srcG = block.offset[1]; 
        srcB = block.offset[2];
        srcA = block.offset[3];
        dstR = 1;
        dstG = 2;
        dstB = 3;
        dstA = 0;
        if (kPathSmallEndian) {
            dstR = 3-dstR, dstG = 3-dstG, dstB = 3-dstB, dstA = 3-dstA;
        }
        if ((srcR == dstR) && (srcG == dstG) && (srcB == dstB) && (srcA == dstA)) {
            ptr = (unsigned char *) block.pixelPtr;
        } else {
            data = (unsigned char *) ckalloc(pitch*iheight);
            ptr = data;
            
            for (i = 0; i < iheight; i++) {
                srcPtr = block.pixelPtr + i*pitch;
                dstPtr = ptr + i*pitch;
                for (j = 0; j < iwidth; j++) {
                    *(dstPtr+dstR) = *(srcPtr+srcR);
                    *(dstPtr+dstG) = *(srcPtr+srcG);
                    *(dstPtr+dstB) = *(srcPtr+srcB);
                    *(dstPtr+dstA) = *(srcPtr+srcA);
                    srcPtr += 4;
                    dstPtr += 4;
                }
            }
        }
    } else if (block.pixelSize*8 == 24) {
        /* Could do something about this? */
        return;
    } else {
        return;
    }
    surface = cairo_image_surface_create_for_data(
            ptr,
            format, 
            (int) width, (int) height, 
            pitch);		/* stride */
    cairo_set_source_surface(context->c, surface, x, y);
    cairo_paint(context->c);
    cairo_surface_destroy(surface);
    if (data) {
        ckfree((char *)data);
    }
}

void TkPathClosePath(TkPathContext ctx)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_close_path(context->c);
}

int
TkPathTextConfig(Tcl_Interp *interp, Tk_PathTextStyle *textStylePtr, char *utf8, void **customPtr)
{
    return TCL_OK;
}

void
TkPathTextDraw(TkPathContext ctx, Tk_PathStyle *style, Tk_PathTextStyle *textStylePtr, 
        double x, double y, char *utf8, void *custom)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    
    cairo_select_font_face(context->c, textStylePtr->fontFamily, 
            CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(context->c, textStylePtr->fontSize);
    cairo_move_to(context->c, x, y);
    if ((style->fillColor != NULL) && (style->strokeColor != NULL)) {
        cairo_text_path(context->c, utf8);
        TkPathFillAndStroke(ctx, style);
    } else if (style->fillColor != NULL) {
    
        /* This is the normal way to draw text which is likely faster. */
        CairoSetFill(ctx, style);
        cairo_show_text(context->c, utf8);
    } else if (style->strokeColor != NULL) {
        cairo_text_path(context->c, utf8);
        TkPathStroke(ctx, style);
    }
}

void
TkPathTextFree(Tk_PathTextStyle *textStylePtr, void *custom)
{
    /* Empty. */
}

PathRect
TkPathTextMeasureBbox(Tk_PathTextStyle *textStylePtr, char *utf8, void *custom)
{
    cairo_t *c;
    cairo_surface_t *surface;
    cairo_text_extents_t extents;
    PathRect r;

    /* @@@ Not very happy about this but it seems that there is no way to 
     *     measure text without having a surface (drawable) in cairo.
     */
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 10, 10);
    c = cairo_create(surface);
    cairo_select_font_face(c, textStylePtr->fontFamily, 
            CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(c, textStylePtr->fontSize);

    cairo_text_extents(c, utf8, &extents);
    r.x1 = 0.0;
    r.y1 = extents.y_bearing;		// will usually be negative.
    r.x2 = extents.x_bearing + extents.width;
    r.y2 = extents.y_bearing + extents.height; 
    cairo_destroy(c);
    cairo_surface_destroy(surface);
    return r;
}

void    	
TkPathSurfaceErase(TkPathContext ctx, double dx, double dy, double dwidth, double dheight)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    unsigned char *data, *dst;
    int i, j;
    int x, y, width, height;
    int xend, yend;
    int stride;
    
    /* Had to do it directly on the bits. Assuming CAIRO_FORMAT_ARGB32 
     * cairos ARGB format is in *native* endian order; Switch!
     * Be careful not to address the bitmap outside its limits. */
    data = context->record->data;
    stride = context->record->stride;
    x = (int) (dx + 0.5);
    y = (int) (dy + 0.5);
    width = (int) (dwidth + 0.5);
    height = (int) (dheight + 0.5);
    x = MAX(0, MIN(context->record->width, x));
    y = MAX(0, MIN(context->record->height, y));
    width = MAX(0, width);
    height = MAX(0, height);
    xend = MIN(x + width, context->record->width);
    yend = MIN(y + height, context->record->height);
        
    for (i = y; i < yend; i++) {
        dst = data + i*stride + 4*x;
        for (j = x; j < xend; j++, dst += 4) {
            *dst = 0x00;
            *(dst+1) = 0x00;
            *(dst+2) = 0x00;
            *(dst+3) = 0x00;
        }
    }
}

void
TkPathSurfaceToPhoto(TkPathContext ctx, Tk_PhotoHandle photo)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_surface_t *surface = context->surface;
    Tk_PhotoImageBlock block;
    unsigned char *data;
    unsigned char *pixel;
    int width, height;
    int stride;					/* Bytes per row. */
    
    width = cairo_image_surface_get_width(surface);
    height = cairo_image_surface_get_height(surface);
    data = context->record->data;
    stride = context->record->stride;
    
    Tk_PhotoGetImage(photo, &block);    
    pixel = (unsigned char *) ckalloc(height*stride);

    if (gSurfaceCopyPremultiplyAlpha) {
        if (kPathSmallEndian) {
            PathCopyBitsPremultipliedAlphaBGRA(data, pixel, width, height, stride);
        } else {
            PathCopyBitsPremultipliedAlphaARGB(data, pixel, width, height, stride);
        }
    } else {
        if (kPathSmallEndian) {
            PathCopyBitsBGRA(data, pixel, width, height, stride);
        } else {
            PathCopyBitsARGB(data, pixel, width, height, stride);
        }
    }
    block.pixelPtr = pixel;
    block.width = width;
    block.height = height;
    block.pitch = stride;
    block.pixelSize = 4;
    block.offset[0] = 0;
    block.offset[1] = 1;
    block.offset[2] = 2;
    block.offset[3] = 3;
    Tk_PhotoPutBlock(photo, &block, 0, 0, width, height, TK_PHOTO_COMPOSITE_OVERLAY);
}

void TkPathClipToPath(TkPathContext ctx, int fillRule)
{
    /* Clipping to path is done by default. */
    /* Note: cairo_clip does not consume the current path */
    //cairo_clip(context->c);
}

void TkPathReleaseClipToPath(TkPathContext ctx)
{
    //cairo_reset_clip(context->c);
}

void TkPathStroke(TkPathContext ctx, Tk_PathStyle *style)
{       
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    Tk_Dash *dash;

    cairo_set_source_rgba(context->c,             
            RedDoubleFromXColorPtr(style->strokeColor),
            GreenDoubleFromXColorPtr(style->strokeColor),
            BlueDoubleFromXColorPtr(style->strokeColor), 
            style->strokeOpacity);
    cairo_set_line_width(context->c, style->strokeWidth);

    switch (style->capStyle) {
        case CapNotLast:
        case CapButt:
            cairo_set_line_cap(context->c, CAIRO_LINE_CAP_BUTT);
            break;
        case CapRound:
            cairo_set_line_cap(context->c, CAIRO_LINE_CAP_ROUND);
            break;
        default:
            cairo_set_line_cap(context->c, CAIRO_LINE_CAP_SQUARE);
            break;
    }
    switch (style->joinStyle) {
        case JoinMiter: 
            cairo_set_line_join(context->c, CAIRO_LINE_JOIN_MITER);
            break;
        case JoinRound:
            cairo_set_line_join(context->c, CAIRO_LINE_JOIN_ROUND);
            break;
        default:
            cairo_set_line_join(context->c, CAIRO_LINE_JOIN_BEVEL);
            break;
    }
    cairo_set_miter_limit(context->c, style->miterLimit);

    dash = &(style->dash);
    if ((dash != NULL) && (dash->number != 0)) {
        int	i, len;
        float 	*array;
    
        PathParseDashToArray(dash, style->strokeWidth, &len, &array);
        if (len > 0) {
            double *dashes = (double *) ckalloc(len*sizeof(double));

            for (i = 0; i < len; i++) {
                dashes[i] = array[i];
            }
            cairo_set_dash(context->c, dashes, len, style->offset);
            ckfree((char *) dashes);
            ckfree((char *) array);
        }
    }

    cairo_stroke(context->c);
}

void TkPathFill(TkPathContext ctx, Tk_PathStyle *style)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CairoSetFill(ctx, style);
    cairo_fill(context->c);
}

void TkPathFillAndStroke(TkPathContext ctx, Tk_PathStyle *style)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CairoSetFill(ctx, style);
    cairo_fill_preserve(context->c);
    TkPathStroke(ctx, style);
}

void TkPathEndPath(TkPathContext ctx)
{
    /* Empty ??? */
}

void TkPathFree(TkPathContext ctx)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_destroy(context->c);
    cairo_surface_destroy(context->surface);
    if (context->record) {
        ckfree((char *) context->record->data);
        ckfree((char *) context->record);
    }
    ckfree((char *) context);
}

int TkPathDrawingDestroysPath(void)
{
    return 1;
}

int		
TkPathPixelAlign(void)
{
    return 0;
}

int TkPathGetCurrentPosition(TkPathContext ctx, PathPoint *pt)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_get_current_point(context->c, &(pt->x), &(pt->y));
    return TCL_OK;
}

int TkPathBoundingBox(TkPathContext ctx, PathRect *rPtr)
{
    return TCL_ERROR;
}

static int GetCairoExtend(int method)
{
    cairo_extend_t extend;

    switch (method) {
        case kPathGradientMethodPad: 
            extend = CAIRO_EXTEND_NONE;
            break;
        case kPathGradientMethodRepeat:
            extend = CAIRO_EXTEND_REPEAT;
            break;
        case kPathGradientMethodReflect:
            extend = CAIRO_EXTEND_REFLECT;
            break;
        default:
            extend = CAIRO_EXTEND_NONE;
            break;
    }
    return extend;
}

void TkPathPaintLinearGradient(TkPathContext ctx, PathRect *bbox, LinearGradientFill *fillPtr, int fillRule)
{    
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    int					i;
    int					nstops;
    PathRect 			*tPtr;		/* The transition line. */
    GradientStop 		*stop;
    cairo_pattern_t 	*pattern;
    GradientStopArray 	*stopArrPtr;

    stopArrPtr = fillPtr->stopArrPtr;    
    tPtr = fillPtr->transitionPtr;
    nstops = stopArrPtr->nstops;

    /*
     * The current path is consumed by filling.
     * Need therfore to save the current context and restore after.
     */
    cairo_save(context->c);

    pattern = cairo_pattern_create_linear(tPtr->x1, tPtr->y1, tPtr->x2, tPtr->y2);

    /*
     * We need to do like this since this is how SVG defines gradient drawing
     * in case the transition vector is in relative coordinates.
     */
    if (fillPtr->units == kPathGradientUnitsBoundingBox) {
        cairo_translate(context->c, bbox->x1, bbox->y1);
        cairo_scale(context->c, bbox->x2 - bbox->x1, bbox->y2 - bbox->y1);
    }

    for (i = 0; i < nstops; i++) {
        stop = stopArrPtr->stops[i];
        cairo_pattern_add_color_stop_rgba(pattern, stop->offset, 
                RedDoubleFromXColorPtr(stop->color),
                GreenDoubleFromXColorPtr(stop->color),
                BlueDoubleFromXColorPtr(stop->color),
                stop->opacity);
    }
    cairo_set_source(context->c, pattern);
    cairo_set_fill_rule(context->c, 
            (fillRule == WindingRule) ? CAIRO_FILL_RULE_WINDING : CAIRO_FILL_RULE_EVEN_ODD);
    cairo_pattern_set_extend(pattern, GetCairoExtend(fillPtr->method));
    cairo_fill(context->c);
    
    cairo_pattern_destroy(pattern);
    cairo_restore(context->c);
}
            
void
TkPathPaintRadialGradient(TkPathContext ctx, PathRect *bbox, RadialGradientFill *fillPtr, int fillRule)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    int					i;
    int					nstops;
    GradientStop 		*stop;
    cairo_pattern_t 	*pattern;
    GradientStopArray 	*stopArrPtr;
    RadialTransition    *tPtr;

    stopArrPtr = fillPtr->stopArrPtr;    
    nstops = stopArrPtr->nstops;
    tPtr = fillPtr->radialPtr;

    /*
     * The current path is consumed by filling.
     * Need therfore to save the current context and restore after.
     */
    cairo_save(context->c);
    pattern = cairo_pattern_create_radial(
            tPtr->focalX, tPtr->focalY, 0.0,
            tPtr->centerX, tPtr->centerY, tPtr->radius);

    cairo_translate(context->c, bbox->x1, bbox->y1);
    cairo_scale(context->c, bbox->x2 - bbox->x1, bbox->y2 - bbox->y1);

    for (i = 0; i < nstops; i++) {
        stop = stopArrPtr->stops[i];
        cairo_pattern_add_color_stop_rgba(pattern, stop->offset, 
                RedDoubleFromXColorPtr(stop->color),
                GreenDoubleFromXColorPtr(stop->color),
                BlueDoubleFromXColorPtr(stop->color),
                stop->opacity);
    }
    cairo_set_source(context->c, pattern);
    cairo_set_fill_rule(context->c, 
            (fillRule == WindingRule) ? CAIRO_FILL_RULE_WINDING : CAIRO_FILL_RULE_EVEN_ODD);
    cairo_pattern_set_extend(pattern, GetCairoExtend(fillPtr->method));
    cairo_fill(context->c);
    
    cairo_pattern_destroy(pattern);
    cairo_restore(context->c);
}
