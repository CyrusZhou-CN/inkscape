// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PNG file format utilities
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Whoever wrote this example in libpng documentation
 *   Peter Bostrom
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 1999-2002 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */


#include <2geom/rect.h>
#include <2geom/transforms.h>

#include <png.h>

#include "document.h"
#include "png-write.h"
#include "rdf.h"

#include "display/cairo-utils.h"
#include "display/drawing-context.h"
#include "display/drawing.h"

#include "io/sys.h"

#include "object/sp-defs.h"
#include "object/sp-item.h"
#include "object/sp-root.h"

#include "ui/interface.h"
#include <glibmm/convert.h>

/* This is an example of how to use libpng to read and write PNG files.
 * The file libpng.txt is much more verbose then this.  If you have not
 * read it, do so first.  This was designed to be a starting point of an
 * implementation.  This is not officially part of libpng, and therefore
 * does not require a copyright notice.
 *
 * This file does not currently compile, because it is missing certain
 * parts, like allocating memory to hold an image.  You will have to
 * supply these parts to get it to compile.  For an example of a minimal
 * working PNG reader/writer, see pngtest.c, included in this distribution.
 */

struct SPEBP {
    unsigned long int width, height, sheight;
    guint32 background;
    Inkscape::Drawing *drawing; // it is assumed that all unneeded items are hidden
    guchar *px;
    unsigned (*status)(float, void *);
    void *data;
};

/* write a png file */

struct SPPNGBD {
    guchar const *px;
    int rowstride;
};

/**
 * A simple wrapper to list png_text.
 */
class PngTextList {
public:
    PngTextList() : count(0), textItems(nullptr) {}
    ~PngTextList();

    void add(gchar const* key, gchar const* text);
    gint getCount() {return count;}
    png_text* getPtext() {return textItems;}

private:
    gint count;
    png_text* textItems;
};

PngTextList::~PngTextList() {
    for (gint i = 0; i < count; i++) {
        if (textItems[i].key) {
            g_free(textItems[i].key);
        }
        if (textItems[i].text) {
            g_free(textItems[i].text);
        }
    }
}

void PngTextList::add(gchar const* key, gchar const* text)
{
    if (count < 0) {
        count = 0;
        textItems = nullptr;
    }
    png_text* tmp = (count > 0) ? g_try_renew(png_text, textItems, count + 1): g_try_new(png_text, 1);
    if (tmp) {
        textItems = tmp;
        count++;

        png_text* item = &(textItems[count - 1]);
        item->compression = PNG_TEXT_COMPRESSION_NONE;
        item->key = g_strdup(key);
        item->text = g_strdup(text);
        item->text_length = 0;
#ifdef PNG_iTXt_SUPPORTED
        item->itxt_length = 0;
        item->lang = nullptr;
        item->lang_key = nullptr;
#endif // PNG_iTXt_SUPPORTED
    } else {
        g_warning("Unable to allocate array for %d PNG text data.", count);
        textItems = nullptr;
        count = 0;
    }
}

/**
 * Write to PNG.
 * 
 * @arg filename Filename and path. Value is in UTF8 encoding.
 */
static bool
sp_png_write_rgba_striped(SPDocument *doc,
                          gchar const *filename, unsigned long int width, unsigned long int height, double xdpi, double ydpi,
                          int (* get_rows)(guchar const **rows, void **to_free, int row, int num_rows, void *data, int color_type, int bit_depth),
                          void *data, bool interlace, int color_type, int bit_depth, int zlib)
{
    g_return_val_if_fail(filename != nullptr, false);
    g_return_val_if_fail(data != nullptr, false);

    struct SPEBP *ebp = (struct SPEBP *) data;
    FILE *fp;
    png_structp png_ptr;
    png_infop info_ptr;
    png_color_8 sig_bit;
    png_uint_32 r;

    /* open the file */

    Inkscape::IO::dump_fopen_call(filename, "M");
    fp = Inkscape::IO::fopen_utf8name(filename, "wb");
    if(fp == nullptr) return false;

    /* Create and initialize the png_struct with the desired error handler
     * functions.  If you want to use the default stderr and longjump method,
     * you can supply NULL for the last three parameters.  We also check that
     * the library version is compatible with the one used at compile time,
     * in case we are using dynamically linked libraries.  REQUIRED.
     */
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);

    if (png_ptr == nullptr) {
        fclose(fp);
        return false;
    }

    /* Allocate/initialize the image information data.  REQUIRED */
    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == nullptr) {
        fclose(fp);
        png_destroy_write_struct(&png_ptr, nullptr);
        return false;
    }

    /* Set error handling.  REQUIRED if you aren't supplying your own
     * error handling functions in the png_create_write_struct() call.
     */
    if (setjmp(png_jmpbuf(png_ptr))) {
        // If we get here, we had a problem reading the file
        fclose(fp);
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return false;
    }

    /* set up the output control if you are using standard C streams */
    png_init_io(png_ptr, fp);

    /* Set the image information here.  Width and height are up to 2^31,
     * bit_depth is one of 1, 2, 4, 8, or 16, but valid values also depend on
     * the color_type selected. color_type is one of PNG_COLOR_TYPE_GRAY,
     * PNG_COLOR_TYPE_GRAY_ALPHA, PNG_COLOR_TYPE_PALETTE, PNG_COLOR_TYPE_RGB,
     * or PNG_COLOR_TYPE_RGB_ALPHA.  interlace is either PNG_INTERLACE_NONE or
     * PNG_INTERLACE_ADAM7, and the compression_type and filter_type MUST
     * currently be PNG_COMPRESSION_TYPE_BASE and PNG_FILTER_TYPE_BASE. REQUIRED
     */

    png_set_compression_level(png_ptr, zlib);

    png_set_IHDR(png_ptr, info_ptr,
                 width,
                 height,
                 bit_depth,
                 color_type,
                 interlace ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE,
                 PNG_FILTER_TYPE_BASE);

    if ((color_type&2) && bit_depth == 16) {
        // otherwise, if we are dealing with a color image then
        sig_bit.red = 8;
        sig_bit.green = 8;
        sig_bit.blue = 8;
        // if the image has an alpha channel then
        if (color_type&4)
            sig_bit.alpha = 8;
        png_set_sBIT(png_ptr, info_ptr, &sig_bit);
    }

    PngTextList textList;

    textList.add("Software", "www.inkscape.org"); // Made by Inkscape comment
    {
        const gchar* pngToDc[] = {"Title", "title",
                               "Author", "creator",
                               "Description", "description",
                               //"Copyright", "",
                               "Creation Time", "date",
                               //"Disclaimer", "",
                               //"Warning", "",
                               "Source", "source"
                               //"Comment", ""
        };
        for (size_t i = 0; i < G_N_ELEMENTS(pngToDc); i += 2) {
            struct rdf_work_entity_t * entity = rdf_find_entity ( pngToDc[i + 1] );
            if (entity) {
                gchar const* data = rdf_get_work_entity(doc, entity);
                if (data && *data) {
                    textList.add(pngToDc[i], data);
                }
            } else {
                g_warning("Unable to find entity [%s]", pngToDc[i + 1]);
            }
        }


        struct rdf_license_t *license =  rdf_get_license(doc, true);
        if (license) {
            if (license->name && license->uri) {
                gchar* tmp = g_strdup_printf("%s %s", license->name, license->uri);
                textList.add("Copyright", tmp);
                g_free(tmp);
            } else if (license->name) {
                textList.add("Copyright", license->name);
            } else if (license->uri) {
                textList.add("Copyright", license->uri);
            }
        }
    }
    if (textList.getCount() > 0) {
        png_set_text(png_ptr, info_ptr, textList.getPtext(), textList.getCount());
    }

    /* other optional chunks like cHRM, bKGD, tRNS, tIME, oFFs, pHYs, */
    /* note that if sRGB is present the cHRM chunk must be ignored
     * on read and must be written in accordance with the sRGB profile */
    if(xdpi < 0.0254 ) xdpi = 0.0255;
    if(ydpi < 0.0254 ) ydpi = 0.0255;

    png_set_pHYs(png_ptr, info_ptr, unsigned(xdpi / 0.0254 ), unsigned(ydpi / 0.0254 ), PNG_RESOLUTION_METER);

    /* Write the file header information.  REQUIRED */
    png_write_info(png_ptr, info_ptr);

    /* Once we write out the header, the compression type on the text
     * chunks gets changed to PNG_TEXT_COMPRESSION_NONE_WR or
     * PNG_TEXT_COMPRESSION_zTXt_WR, so it doesn't get written out again
     * at the end.
     */

    /* set up the transformations you want.  Note that these are
     * all optional.  Only call them if you want them.
     */

    /* --- CUT --- */

    /* The easiest way to write the image (you may have a different memory
     * layout, however, so choose what fits your needs best).  You need to
     * use the first method if you aren't handling interlacing yourself.
     */

    png_bytep* row_pointers = new png_bytep[ebp->sheight];
    int number_of_passes = interlace ? png_set_interlace_handling(png_ptr) : 1;

    for(int i=0;i<number_of_passes; ++i){
        r = 0;
        while (r < static_cast<png_uint_32>(height)) {
            void *to_free;
            int n = get_rows((unsigned char const **) row_pointers, &to_free, r, height-r, data, color_type, bit_depth);
            if (!n) break;
            png_write_rows(png_ptr, row_pointers, n);
            g_free(to_free);
            r += n;
        }
    }

    delete[] row_pointers;

    /* You can write optional chunks like tEXt, zTXt, and tIME at the end
     * as well.
     */

    /* It is REQUIRED to call this to finish writing the rest of the file */
    png_write_end(png_ptr, info_ptr);

    /* if you allocated any text comments, free them here */

    /* clean up after the write, and free any memory allocated */
    png_destroy_write_struct(&png_ptr, &info_ptr);

    /* close the file */
    fclose(fp);

    /* that's it */
    return true;
}


/**
 *
 */
static int
sp_export_get_rows(guchar const **rows, void **to_free, int row, int num_rows, void *data, int color_type, int bit_depth)
{
    struct SPEBP *ebp = (struct SPEBP *) data;

    if (ebp->status) {
        if (!ebp->status((float) row / ebp->height, ebp->data)) return 0;
    }

    num_rows = MIN(num_rows, static_cast<int>(ebp->sheight));
    num_rows = MIN(num_rows, static_cast<int>(ebp->height - row));

    /* Set area of interest */
    // bbox is now set to the entire image to prevent discontinuities
    // in the image when blur is used (the borders may still be a bit
    // off, but that's less noticeable).
    Geom::IntRect bbox = Geom::IntRect::from_xywh(0, row, ebp->width, num_rows);

    /* Update to renderable state */
    ebp->drawing->update(bbox);

    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ebp->width);
    unsigned char *px = g_new(guchar, num_rows * stride);

    cairo_surface_t *s = cairo_image_surface_create_for_data(
        px, CAIRO_FORMAT_ARGB32, ebp->width, num_rows, stride);
    Inkscape::DrawingContext dc(s, bbox.min());
    dc.setSource(ebp->background);
    dc.setOperator(CAIRO_OPERATOR_SOURCE);
    dc.paint();
    dc.setOperator(CAIRO_OPERATOR_OVER);

    /* Render */
    ebp->drawing->render(dc, bbox, 0);
    cairo_surface_destroy(s);

    // PNG stores data as unpremultiplied big-endian RGBA, which means
    // it's identical to the GdkPixbuf format.
    convert_pixels_argb32_to_pixbuf(px, ebp->width, num_rows, stride,
                                    /* RGBA to ARGB with A=0 */ ebp->background >> 8);
    
    // If a custom bit depth or color type is asked, then convert rgb to grayscale, etc.
    const guchar* new_data = pixbuf_to_png(rows, px, num_rows, ebp->width, stride, color_type, bit_depth);
    *to_free = (void*) new_data;
    free(px);

    return num_rows;
}

ExportResult sp_export_png_file(SPDocument *doc, gchar const *filename,
                                double x0, double y0, double x1, double y1,
                                unsigned long int width, unsigned long int height, double xdpi, double ydpi,
                                unsigned long bgcolor,
                                unsigned int (*status) (float, void *),
                                void *data, bool force_overwrite,
                                const std::vector<SPItem const *> &items_only, bool interlace, int color_type, int bit_depth, int zlib, int antialiasing)
{
    return sp_export_png_file(doc, filename, Geom::Rect(Geom::Point(x0,y0),Geom::Point(x1,y1)),
                              width, height, xdpi, ydpi, bgcolor, status, data, force_overwrite, items_only, interlace, color_type, bit_depth, zlib, antialiasing);
}

/**
 * Export an area to a PNG file
 *
 * @param area Area in document coordinates
 * @param filename Filename and path. Value is UTF8 encoded.
 */
ExportResult sp_export_png_file(SPDocument *doc, gchar const *filename,
                                Geom::Rect const &area,
                                unsigned long width, unsigned long height, double xdpi, double ydpi,
                                unsigned long bgcolor,
                                unsigned (*status)(float, void *),
                                void *data, bool force_overwrite,
                                const std::vector<SPItem const *> &items_only, bool interlace, int color_type, int bit_depth, int zlib, int antialiasing)
{
    g_return_val_if_fail(doc != nullptr, EXPORT_ERROR);
    g_return_val_if_fail(filename != nullptr, EXPORT_ERROR);
    g_return_val_if_fail(width >= 1, EXPORT_ERROR);
    g_return_val_if_fail(height >= 1, EXPORT_ERROR);
    g_return_val_if_fail(!area.hasZeroArea(), EXPORT_ERROR);

    if (!force_overwrite && !sp_ui_overwrite_file(Glib::filename_from_utf8(filename))) {
        // aborted overwrite
	return EXPORT_ABORTED;
    }

    doc->ensureUpToDate();

    /* Calculate translation by transforming to document coordinates (flipping Y)*/
    Geom::Point translation = -area.min();

    /*  This calculation is only valid when assumed that (x0,y0)= area.corner(0) and (x1,y1) = area.corner(2)
     * 1) a[0] * x0 + a[2] * y1 + a[4] = 0.0
     * 2) a[1] * x0 + a[3] * y1 + a[5] = 0.0
     * 3) a[0] * x1 + a[2] * y1 + a[4] = width
     * 4) a[1] * x0 + a[3] * y0 + a[5] = height
     * 5) a[1] = 0.0;
     * 6) a[2] = 0.0;
     *
     * (1,3) a[0] * x1 - a[0] * x0 = width
     * a[0] = width / (x1 - x0)
     * (2,4) a[3] * y0 - a[3] * y1 = height
     * a[3] = height / (y0 - y1)
     * (1) a[4] = -a[0] * x0
     * (2) a[5] = -a[3] * y1
     */

    Geom::Affine const affine(Geom::Translate(translation)
                            * Geom::Scale(width / area.width(),
                                        height / area.height()));

    struct SPEBP ebp;
    ebp.width  = width;
    ebp.height = height;
    ebp.background = bgcolor;

    /* Create new drawing */
    Inkscape::Drawing drawing;
    unsigned const dkey = SPItem::display_key_new(1);
    drawing.setRoot(doc->getRoot()->invoke_show(drawing, dkey, SP_ITEM_SHOW_DISPLAY));
    drawing.root()->setTransform(affine);
    drawing.setExact(); // export with maximum blur rendering quality
    drawing.setAntialiasingOverride(static_cast<Inkscape::Antialiasing>(antialiasing));

    ebp.drawing = &drawing;

    // We show all and then hide all items we don't want, instead of showing only requested items,
    // because that would not work if the shown item references something in defs
    if (!items_only.empty()) {
        doc->getRoot()->invoke_hide_except(dkey, items_only);
    }

    ebp.status = status;
    ebp.data   = data;

    bool write_status = false;;

    ebp.sheight = 64;
    ebp.px = g_try_new(guchar, 4 * ebp.sheight * width);

    if (ebp.px) {
        write_status = sp_png_write_rgba_striped(doc, filename, width, height, xdpi, ydpi, sp_export_get_rows, &ebp, interlace, color_type, bit_depth, zlib);
        g_free(ebp.px);
    }

    // Hide items, this releases arenaitem
    doc->getRoot()->invoke_hide(dkey);

    return write_status ? EXPORT_OK : EXPORT_ERROR;
}


/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
