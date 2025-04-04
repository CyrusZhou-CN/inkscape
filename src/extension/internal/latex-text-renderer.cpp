// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * Rendering LaTeX file (pdf/eps/ps+latex output)
 *
 * The idea stems from GNUPlot's epslatex terminal output :-)
 */
/*
 * Authors:
 *   Johan Engelen <goejendaagh@zonnet.nl>
 *   Miklos Erdelyi <erdelyim@gmail.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 2006-2011 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "latex-text-renderer.h"

#include <csignal>
#include <cerrno>

#include <glibmm/i18n.h>
#include <glibmm/regex.h>

#include "libnrtype/Layout-TNG.h"
#include <2geom/transforms.h>
#include <2geom/rect.h>

#include "colors/color.h"
#include "colors/manager.h"
#include "object/sp-item.h"
#include "object/sp-item-group.h"
#include "object/sp-root.h"
#include "object/sp-use.h"
#include "object/sp-text.h"
#include "object/sp-flowtext.h"
#include "object/sp-rect.h"
#include "style.h"

#include "text-editing.h"

#include "util/units.h"

#include "extension/output.h"
#include "extension/system.h"

#include "inkscape-version.h"
#include "io/sys.h"
#include "document.h"

namespace Inkscape {
namespace Extension {
namespace Internal {

/**
 * This method is called by the PDF, EPS and PS output extensions.
 * @param filename This should be the filename without '_tex' extension to which the tex code should be written. Output goes to <filename>_tex, note the underscore instead of period.
 */
bool
latex_render_document_text_to_file( SPDocument *doc, gchar const *filename,
                                    bool pdflatex)
{
    doc->ensureUpToDate();

    SPRoot *root = doc->getRoot();
    if (!root)
        return false;

    LaTeXTextRenderer renderer = LaTeXTextRenderer(pdflatex);

    if (renderer.setTargetFile(filename) && renderer.setupDocument(doc, root)) {
        renderer.renderItem(root);
        return true;
    }
    return false;
}

LaTeXTextRenderer::LaTeXTextRenderer(bool pdflatex)
  : _stream(nullptr),
    _filename(nullptr),
    _pdflatex(pdflatex),
    _omittext_state(EMPTY),
    _omittext_page(1)
{
    push_transform(Geom::identity());
}

LaTeXTextRenderer::~LaTeXTextRenderer()
{
    if (_stream) {
        writePostamble();

        fclose(_stream);
    }

    if (_filename) {
        g_free(_filename);
    }
}

/** This should create the output LaTeX file, and assign it to _stream.
 * @return Returns true when successful
 */
bool
LaTeXTextRenderer::setTargetFile(gchar const *filename) {
    if (filename != nullptr) {
        while (isspace(*filename)) filename += 1;

        _filename = g_path_get_basename(filename);

        gchar *filename_ext = g_strdup_printf("%s_tex", filename);
        Inkscape::IO::dump_fopen_call(filename_ext, "K");
        FILE *osf = Inkscape::IO::fopen_utf8name(filename_ext, "w+");
        if (!osf) {
            fprintf(stderr, "inkscape: fopen(%s): %s\n", filename_ext, strerror(errno));
            g_free(filename_ext);
            return false;
        }
        _stream = osf;
        g_free(filename_ext);
    }

    fprintf(_stream, "%%%% Creator: Inkscape %s, www.inkscape.org\n", Inkscape::version_string);
    fprintf(_stream, "%%%% PDF/EPS/PS + LaTeX output extension by Johan Engelen, 2010\n");
    fprintf(_stream, "%%%% Accompanies image file '%s' (pdf, eps, ps)\n", _filename);
    fprintf(_stream, "%%%%\n");
    /* flush this to test output stream as early as possible */
    if (fflush(_stream)) {
        if (ferror(_stream)) {
            g_warning("Error %d on LaTeX file output stream: %s", errno,
                    g_strerror(errno));
        }
        g_warning("Output to LaTeX file failed");
        /* fixme: should use pclose() for pipes */
        fclose(_stream);
        _stream = nullptr;
        fflush(stdout);
        return false;
    }

    writePreamble();

    return true;
}

static char const preamble[] =
"%% To include the image in your LaTeX document, write\n"
"%%   \\input{<filename>.pdf_tex}\n"
"%%  instead of\n"
"%%   \\includegraphics{<filename>.pdf}\n"
"%% To scale the image, write\n"
"%%   \\def\\svgwidth{<desired width>}\n"
"%%   \\input{<filename>.pdf_tex}\n"
"%%  instead of\n"
"%%   \\includegraphics[width=<desired width>]{<filename>.pdf}\n"
"%%\n"
"%% Images with a different path to the parent latex file can\n"
"%% be accessed with the `import' package (which may need to be\n"
"%% installed) using\n"
"%%   \\usepackage{import}\n"
"%% in the preamble, and then including the image with\n"
"%%   \\import{<path to file>}{<filename>.pdf_tex}\n"
"%% Alternatively, one can specify\n"
"%%   \\graphicspath{{<path to file>/}}\n"
"%% \n"
"%% For more information, please see info/svg-inkscape on CTAN:\n"
"%%   http://tug.ctan.org/tex-archive/info/svg-inkscape\n"
"%%\n"
"\\begingroup%\n"
"  \\makeatletter%\n"
"  \\providecommand\\color[2][]{%\n"
"    \\errmessage{(Inkscape) Color is used for the text in Inkscape, but the package \'color.sty\' is not loaded}%\n"
"    \\renewcommand\\color[2][]{}%\n"
"  }%\n"
"  \\providecommand\\transparent[1]{%\n"
"    \\errmessage{(Inkscape) Transparency is used (non-zero) for the text in Inkscape, but the package \'transparent.sty\' is not loaded}%\n"
"    \\renewcommand\\transparent[1]{}%\n"
"  }%\n"
"  \\providecommand\\rotatebox[2]{#2}%\n"
"  \\newcommand*\\fsize{\\dimexpr\\f@size pt\\relax}%\n"
"  \\newcommand*\\lineheight[1]{\\fontsize{\\fsize}{#1\\fsize}\\selectfont}%\n";

static char const postamble[] =
"  \\end{picture}%\n"
"\\endgroup%\n";

void
LaTeXTextRenderer::writePreamble()
{
    fprintf(_stream, "%s", preamble);
}
void
LaTeXTextRenderer::writePostamble()
{
    fprintf(_stream, "%s", postamble);
}

void LaTeXTextRenderer::sp_group_render(SPGroup *group)
{
	std::vector<SPObject*> l = (group->childList(false));
    for(auto x : l){
        auto item = cast<SPItem>(x);
        if (item) {
            renderItem(item);
        }
    }
}

void LaTeXTextRenderer::sp_use_render(SPUse *use)
{
    bool translated = false;

    if ((use->x._set && use->x.computed != 0) || (use->y._set && use->y.computed != 0)) {
        Geom::Affine tp(Geom::Translate(use->x.computed, use->y.computed));
        push_transform(tp);
        translated = true;
    }

    auto childItem = use->child;
    if (childItem) {
        renderItem(childItem);
    }

    if (translated) {
        pop_transform();
    }
}

void LaTeXTextRenderer::sp_text_render(SPText *textobj)
{
    // Nothing to do here... (so don't emit an empty box)
    // Also avoids falling out of sync with the CairoRenderer (which won't render anything in this case either)
    if (textobj->layout.getActualLength() == 0)
        return;

    // Only PDFLaTeX supports importing a single page of a graphics file,
    // so only PDF backend gets interleaved text/graphics
    if (_pdflatex && _omittext_state ==  GRAPHIC_ON_TOP)
        _omittext_state = NEW_PAGE_ON_GRAPHIC;

    SPStyle *style = textobj->style;

    // get position and alignment
    // Align vertically on the baseline of the font (retrieved from the anchor point)
    // Align horizontally on anchorpoint
    gchar const *alignment = nullptr;
    gchar const *aligntabular = nullptr;
    switch (style->text_anchor.computed) {
    case SP_CSS_TEXT_ANCHOR_START:
        alignment = "[lt]";
        aligntabular = "{l}";
        break;
    case SP_CSS_TEXT_ANCHOR_END:
        alignment = "[rt]";
        aligntabular = "{r}";
        break;
    case SP_CSS_TEXT_ANCHOR_MIDDLE:
    default:
        alignment = "[t]";
        aligntabular = "{c}";
        break;
    }

    Geom::Point anchor;
    const auto baseline_anchor_point = textobj->layout.baselineAnchorPoint();
    if (baseline_anchor_point) {
        anchor = (*baseline_anchor_point) * transform();
    } else {
        g_warning("LaTeXTextRenderer::sp_text_render: baselineAnchorPoint unset, text position will be wrong. Please report the issue.");
    }

    Inkscape::Colors::Color color(0x0);
    if (style->fill.set && style->fill.isColor()) {
        color = style->fill.getColor();
        color.addOpacity(style->fill_opacity);
    } else if (style->stroke.set && style->stroke.isColor()) {
        color = style->stroke.getColor();
        color.addOpacity(style->stroke_opacity);
    }
    color.addOpacity(style->opacity);

    // get rotation
    Geom::Affine i2doc = textobj->i2doc_affine();
    Geom::Affine wotransl = i2doc.withoutTranslation();
    double degrees = -180/M_PI * Geom::atan2(wotransl.xAxis());
    bool has_rotation = !Geom::are_near(degrees,0.);

    // get line-height
    float line_height;
    if (style->line_height.unit == SP_CSS_UNIT_NONE) {
        // unitless 'line-height' (use as-is, computed value is relative value)
        line_height = style->line_height.computed;
    } else {
        // 'line-height' with unit (make relative, computed value is absolute value)
        line_height = style->line_height.computed / style->font_size.computed;
    }

    // write to LaTeX
    Inkscape::SVGOStringStream os;
    os.setf(std::ios::fixed); // don't use scientific notation

    os << "    \\put(" << anchor[Geom::X] << "," << anchor[Geom::Y] << "){";
    color.convert(Colors::Space::Type::RGB);
    os << "\\color[rgb]{" << color[0] << "," << color[1] << "," << color[2] << "}";
    if (_pdflatex && color.getOpacity() < 1.0) {
        os << "\\transparent{" << color.getOpacity() << "}";
    }
    if (has_rotation) {
        os << "\\rotatebox{" << degrees << "}{";
    }
    os << "\\makebox(0,0)" << alignment << "{";
    if (line_height != 1) {
        os << "\\lineheight{" << line_height << "}";
    }
    os << "\\smash{";
    os << "\\begin{tabular}[t]" << aligntabular;

        // Walk through all spans in the text object.
        // Write span strings to LaTeX, associated with font weight and style.
        Inkscape::Text::Layout const &layout = *(te_get_layout (textobj));
        for (Inkscape::Text::Layout::iterator li = layout.begin(), le = layout.end();
             li != le; li.nextStartOfSpan())
        {
            Inkscape::Text::Layout::iterator ln = li; 
            ln.nextStartOfSpan();
            Glib::ustring uspanstr = sp_te_get_string_multiline (textobj, li, ln);

            // escape ampersands
            uspanstr = Glib::Regex::create("&")->replace_literal(uspanstr, 0, "\\&", (Glib::Regex::MatchFlags)0);
            // escape percent
            uspanstr = Glib::Regex::create("%")->replace_literal(uspanstr, 0, "\\%", (Glib::Regex::MatchFlags)0);

            const gchar *spanstr = uspanstr.c_str();
            if (!spanstr) {
                continue;
            }

            bool is_bold = false, is_italic = false, is_oblique = false, is_superscript = false, is_subscript = false;

            // newline character only -> don't attempt to add style (will break compilation in LaTeX)
            if (g_strcmp0(spanstr, "\n")) {
                SPStyle const &spanstyle = *(sp_te_style_at_position (textobj, li));
                if (spanstyle.font_weight.computed == SP_CSS_FONT_WEIGHT_500 ||
                    spanstyle.font_weight.computed == SP_CSS_FONT_WEIGHT_600 ||
                    spanstyle.font_weight.computed == SP_CSS_FONT_WEIGHT_700 ||
                    spanstyle.font_weight.computed == SP_CSS_FONT_WEIGHT_800 ||
                    spanstyle.font_weight.computed == SP_CSS_FONT_WEIGHT_900 ||
                    spanstyle.font_weight.computed == SP_CSS_FONT_WEIGHT_BOLD ||
                    spanstyle.font_weight.computed == SP_CSS_FONT_WEIGHT_BOLDER) 
                {
                    is_bold = true;
                    os << "\\textbf{";
                }
                if (spanstyle.font_style.computed == SP_CSS_FONT_STYLE_ITALIC) 
                {
                    is_italic = true;
                    os << "\\textit{";
                }
                if (spanstyle.font_style.computed == SP_CSS_FONT_STYLE_OBLIQUE) 
                {
                    is_oblique = true;
                    os << "\\textsl{";  // this is an accurate choice if the LaTeX chosen font matches the font in Inkscape. Gives bad results when it is not so...
                }
                if (spanstyle.baseline_shift.computed > 0) { // superscript
                    is_superscript = true;
                    os << "\\textsuperscript{";
                }
                if (spanstyle.baseline_shift.computed < 0) {
                    is_subscript = true;
                    os << "\\textsubscript{";
                }
            }

            // replace carriage return with double slash
            gchar ** splitstr = g_strsplit(spanstr, "\n", 2);
            os << splitstr[0];
            if (g_strv_length(splitstr) > 1)
            {
                os << "\\\\";
            }
            g_strfreev(splitstr);

            if (is_subscript) { os << "}"; } // subscript end
            if (is_superscript) { os << "}"; } // superscript end
            if (is_oblique) { os << "}"; } // oblique end
            if (is_italic) { os << "}"; } // italic end
            if (is_bold) { os << "}"; } // bold end
        }

    os << "\\end{tabular}"; // tabular end
    os << "}"; // smash end
    if (has_rotation) { os << "}"; } // rotatebox end
    os << "}"; //makebox end
    os << "}%\n"; // put end

    fprintf(_stream, "%s", os.str().c_str());
}

void LaTeXTextRenderer::sp_flowtext_render(SPFlowtext *flowtext)
{
/*
Flowtext is possible by using a minipage! :)
Flowing in rectangle is possible, not in arb shape.
*/

    // Only PDFLaTeX supports importing a single page of a graphics file,
    // so only PDF backend gets interleaved text/graphics
    if (_pdflatex && _omittext_state ==  GRAPHIC_ON_TOP)
        _omittext_state = NEW_PAGE_ON_GRAPHIC;

    SPStyle *style = flowtext->style;

    SPItem *frame_item = flowtext->get_frame(nullptr);
    auto frame = cast<SPRect>(frame_item);
    if (!frame_item || !frame) {
        g_warning("LaTeX export: non-rectangular flowed text shapes are not supported, skipping text.");
        return; // don't know how to handle non-rect frames yet. is quite uncommon for latex users i think
    }

	// We will transform the coordinates
    Geom::Rect framebox = frame->getRect();

    // get position and alignment
    // Align on topleft corner.
    gchar const *alignment = "[lt]";
    gchar const *justification = "";
    switch (flowtext->layout.paragraphAlignment(flowtext->layout.begin())) {
    case Inkscape::Text::Layout::LEFT:
        justification = "\\raggedright ";
        break;
    case Inkscape::Text::Layout::RIGHT:
        justification = "\\raggedleft ";
        break;
    case Inkscape::Text::Layout::CENTER:
        justification = "\\centering ";
    case Inkscape::Text::Layout::FULL:
    default:
        // no need to add LaTeX code for standard justified output :)
        break;
    }

	// The topleft Corner was calculated after rotating the text which results in a wrong Coordinate.
	// Now, the topleft Corner is rotated after calculating it
    Geom::Point pos(framebox.corner(0) * transform()); //topleft corner

    // determine color and transparency (for now, use rgb color model as it is most native to Inkscape)
    bool has_color = false; // if the item has no color set, don't force black color
    bool has_transparency = false;
    // TODO: how to handle ICC colors?
    // give priority to fill color
    guint32 rgba = 0;
    float opacity = SP_SCALE24_TO_FLOAT(style->opacity.value);
    if (style->fill.set && style->fill.isColor()) {
        has_color = true;
        rgba = style->fill.getColor().toRGBA();
        opacity *= SP_SCALE24_TO_FLOAT(style->fill_opacity.value);
    } else if (style->stroke.set && style->stroke.isColor()) {
        has_color = true;
        rgba = style->stroke.getColor().toRGBA();
        opacity *= SP_SCALE24_TO_FLOAT(style->stroke_opacity.value);
    }
    if (opacity < 1.0) {
        has_transparency = true;
    }

    // get rotation
    Geom::Affine i2doc = flowtext->i2doc_affine();
    Geom::Affine wotransl = i2doc.withoutTranslation();
    double degrees = -180/M_PI * Geom::atan2(wotransl.xAxis());
    bool has_rotation = !Geom::are_near(degrees,0.);

    // write to LaTeX
    Inkscape::SVGOStringStream os;
    os.setf(std::ios::fixed); // don't use scientific notation

    os << "    \\put(" << pos[Geom::X] << "," << pos[Geom::Y] << "){";
    if (has_color) {
        os << "\\color[rgb]{" << SP_RGBA32_R_F(rgba) << "," << SP_RGBA32_G_F(rgba) << "," << SP_RGBA32_B_F(rgba) << "}";
    }
    if (_pdflatex && has_transparency) {
        os << "\\transparent{" << opacity << "}";
    }
    if (has_rotation) {
        os << "\\rotatebox{" << degrees << "}{";
    }
    os << "\\makebox(0,0)" << alignment << "{";

	// Scale the x width correctly
    os << "\\begin{minipage}{" << framebox.width() * transform().expansionX() << "\\unitlength}";
    os << justification;

        // Walk through all spans in the text object.
        // Write span strings to LaTeX, associated with font weight and style.
        Inkscape::Text::Layout const &layout = *(te_get_layout(flowtext));
        for (Inkscape::Text::Layout::iterator li = layout.begin(), le = layout.end(); 
             li != le; li.nextStartOfSpan())
        {
            SPStyle const &spanstyle = *(sp_te_style_at_position(flowtext, li));
            bool is_bold = false, is_italic = false, is_oblique = false, is_superscript = false, is_subscript = false;

            if (spanstyle.font_weight.computed == SP_CSS_FONT_WEIGHT_500 ||
                spanstyle.font_weight.computed == SP_CSS_FONT_WEIGHT_600 ||
                spanstyle.font_weight.computed == SP_CSS_FONT_WEIGHT_700 ||
                spanstyle.font_weight.computed == SP_CSS_FONT_WEIGHT_800 ||
                spanstyle.font_weight.computed == SP_CSS_FONT_WEIGHT_900 ||
                spanstyle.font_weight.computed == SP_CSS_FONT_WEIGHT_BOLD ||
                spanstyle.font_weight.computed == SP_CSS_FONT_WEIGHT_BOLDER) 
            {
                is_bold = true;
                os << "\\textbf{";
            }
            if (spanstyle.font_style.computed == SP_CSS_FONT_STYLE_ITALIC) 
            {
                is_italic = true;
                os << "\\textit{";
            }
            if (spanstyle.font_style.computed == SP_CSS_FONT_STYLE_OBLIQUE) 
            {
                is_oblique = true;
                os << "\\textsl{";  // this is an accurate choice if the LaTeX chosen font matches the font in Inkscape. Gives bad results when it is not so...
            }
            if (spanstyle.baseline_shift.computed > 0) { // superscript
                is_superscript = true;
                os << "\\textsuperscript{";
            }
            if (spanstyle.baseline_shift.computed < 0) {
                is_subscript = true;
                os << "\\textsubscript{";
            }

            Inkscape::Text::Layout::iterator ln = li; 
            ln.nextStartOfSpan();
            Glib::ustring uspanstr = sp_te_get_string_multiline(flowtext, li, ln);
            const gchar *spanstr = uspanstr.c_str();
            if (!spanstr) {
                continue;
            }
            // replace carriage return with double slash
            gchar ** splitstr = g_strsplit(spanstr, "\n", -1);
            gchar *spanstr_new = g_strjoinv("\\\\ ", splitstr);
            os << spanstr_new;
            g_strfreev(splitstr);
            g_free(spanstr_new);

            if (is_subscript) { os << "}"; } // subscript end
            if (is_superscript) { os << "}"; } // superscript end
            if (is_oblique) { os << "}"; } // oblique end
            if (is_italic) { os << "}"; } // italic end
            if (is_bold) { os << "}"; } // bold end
        }

    os << "\\end{minipage}";
    if (has_rotation) {
        os << "}"; // rotatebox end
    }
    os << "}"; //makebox end
    os << "}%\n"; // put end

    fprintf(_stream, "%s", os.str().c_str());
}

void LaTeXTextRenderer::sp_root_render(SPRoot *root)
{
    push_transform(root->c2p);
    sp_group_render(root);
    pop_transform();
}

void
LaTeXTextRenderer::sp_item_invoke_render(SPItem *item)
{
    // Check item's visibility
    if (item->isHidden()) {
        return;
    }

    auto root = cast<SPRoot>(item);
    if (root) {
        return sp_root_render(root);
    }
    auto group = cast<SPGroup>(item);
    if (group) {
        return sp_group_render(group);
    }
    auto use = cast<SPUse>(item);
    if (use) {
        return sp_use_render(use);
    }
    auto text = cast<SPText>(item);
    if (text) {
        return sp_text_render(text);
    }
    auto flowtext = cast<SPFlowtext>(item);
    if (flowtext) {
        return sp_flowtext_render(flowtext);
    }
    // Only PDFLaTeX supports importing a single page of a graphics file,
    // so only PDF backend gets interleaved text/graphics
    if (_pdflatex && (_omittext_state == EMPTY || _omittext_state == NEW_PAGE_ON_GRAPHIC)) {
        writeGraphicPage();
    }
    _omittext_state = GRAPHIC_ON_TOP;
}

void
LaTeXTextRenderer::renderItem(SPItem *item)
{
    push_transform(item->transform);
    sp_item_invoke_render(item);
    pop_transform();
}

void
LaTeXTextRenderer::writeGraphicPage() {
    Inkscape::SVGOStringStream os;
    os.setf(std::ios::fixed); // no scientific notation

    // strip pathname, as it is probably desired. Having a specific path in the TeX file is not convenient.
    if (_pdflatex)
        os << "    \\put(0,0){\\includegraphics[width=\\unitlength,page=" << _omittext_page++ << "]{" << _filename << "}}%\n";
    else
        os << "    \\put(0,0){\\includegraphics[width=\\unitlength]{" << _filename << "}}%\n";

    fprintf(_stream, "%s", os.str().c_str());
}

bool
LaTeXTextRenderer::setupDocument(SPDocument *doc, SPItem *base)
{
    if (!base) {
        base = doc->getRoot();
    }

    Geom::Rect d = Geom::Rect::from_xywh(Geom::Point(0,0), doc->getDimensions());

    // scale all coordinates, such that the width of the image is 1, this is convenient for scaling the image in LaTeX
    double scale = 1/(d.width());
    double _width = d.width() * scale;
    double _height = d.height() * scale;
    push_transform(Geom::Translate(-d.corner(3)) * Geom::Scale(scale, -scale));

    // write the info to LaTeX
    Inkscape::SVGOStringStream os;
    os.setf(std::ios::fixed); // no scientific notation

    // scaling of the image when including it in LaTeX
    os << "  \\ifx\\svgwidth\\undefined%\n";
    os << "    \\setlength{\\unitlength}{" << Inkscape::Util::Quantity::convert(d.width(), "px", "pt") << "bp}%\n"; // note: 'bp' is the Postscript pt unit in LaTeX, see LP bug #792384
    os << "    \\ifx\\svgscale\\undefined%\n";
    os << "      \\relax%\n";
    os << "    \\else%\n";
    os << "      \\setlength{\\unitlength}{\\unitlength * \\real{\\svgscale}}%\n";
    os << "    \\fi%\n";
    os << "  \\else%\n";
    os << "    \\setlength{\\unitlength}{\\svgwidth}%\n";
    os << "  \\fi%\n";
    os << "  \\global\\let\\svgwidth\\undefined%\n";
    os << "  \\global\\let\\svgscale\\undefined%\n";
    os << "  \\makeatother%\n";

    os << "  \\begin{picture}(" << _width << "," << _height << ")%\n";

    // set \baselineskip equal to fontsize (the closest we can seem to get to CSS "line-height: 1;")
    // and remove column spacing from tabular
    os << "    \\lineheight{1}%\n";
    os << "    \\setlength\\tabcolsep{0pt}%\n";

    fprintf(_stream, "%s", os.str().c_str());

    if (!_pdflatex)
        writeGraphicPage();

    return true;
}

Geom::Affine const &
LaTeXTextRenderer::transform()
{
    return _transform_stack.top();
}

void
LaTeXTextRenderer::push_transform(Geom::Affine const &tr)
{
    if(!_transform_stack.empty()){
        Geom::Affine tr_top = _transform_stack.top();
        _transform_stack.push(tr * tr_top);
    } else {
        _transform_stack.push(tr);
    }
}

void
LaTeXTextRenderer::pop_transform()
{
    _transform_stack.pop();
}

}  /* namespace Internal */
}  /* namespace Extension */
}  /* namespace Inkscape */

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
