// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LaTeX Printing
 *
 * Author:
 *  Michael Forbes <miforbes@mbhs.edu>
 *   Abhishek Sharma
 *
 * Copyright (C) 2004 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <2geom/pathvector.h>
#include <2geom/sbasis-to-bezier.h>
#include <2geom/curves.h>
#include <cerrno>
#include <csignal>
#include "util/units.h"
#include "helper/geom-curves.h"

#include "colors/color.h"
#include "extension/print.h"
#include "extension/system.h"
#include "inkscape-version.h"
#include "io/sys.h"
#include "latex-pstricks.h"
#include "style.h"
#include "document.h"
#include <cstring>

namespace Inkscape {
namespace Extension {
namespace Internal {

PrintLatex::PrintLatex ():
    _width(0),
    _height(0),
    _stream(nullptr)
{
}

PrintLatex::~PrintLatex ()
{
    if (_stream) fclose(_stream);
}

unsigned int PrintLatex::setup(Inkscape::Extension::Print * /*mod*/)
{
    return TRUE;
}

unsigned int PrintLatex::begin (Inkscape::Extension::Print *mod, SPDocument *doc)
{
    Inkscape::SVGOStringStream os;
    int res;
    FILE *osf = nullptr;
    const gchar * fn = nullptr;
    gsize bytesRead = 0;
    gsize bytesWritten = 0;
    GError* error = nullptr;

    os.setf(std::ios::fixed);
    fn = mod->get_param_string("destination");
    gchar* local_fn = g_filename_from_utf8( fn,
                                            -1,  &bytesRead,  &bytesWritten, &error);
    fn = local_fn;

    /* TODO: Replace the below fprintf's with something that does the right thing whether in
     * gui or batch mode (e.g. --print=blah).  Consider throwing an exception: currently one of
     * the callers (sp_print_document_to_file, "ret = mod->begin(doc)") wrongly ignores the
     * return code.
     */
    if (fn != nullptr) {
        while (isspace(*fn)) fn += 1;
        Inkscape::IO::dump_fopen_call(fn, "K");
        osf = Inkscape::IO::fopen_utf8name(fn, "w+");
        if (!osf) {
            fprintf(stderr, "inkscape: fopen(%s): %s\n", fn, strerror(errno));
            g_free(local_fn);
            return 0;
        }
        _stream = osf;
    }

    g_free(local_fn);

    res = fprintf(_stream, "%%LaTeX with PSTricks extensions\n");
    /* flush this to test output stream as early as possible */
    if (fflush(_stream)) {
        /*g_warning("caught error in sp_module_print_plain_begin");*/
        if (ferror(_stream)) {
            g_warning("Error %d on output stream: %s", errno,
                    g_strerror(errno));
        }
        g_warning("Printing failed");
        /* fixme: should use pclose() for pipes */
        fclose(_stream);
        _stream = nullptr;
        fflush(stdout);
        return 0;
    }

    // width and height in pt
    _width = doc->getWidth().value("pt");
    _height = doc->getHeight().value("pt");

    if (res >= 0) {

        os << "%%Creator: Inkscape " << Inkscape::version_string << "\n";
        os << "%%Please note this file requires PSTricks extensions\n";

        os << "\\psset{xunit=.5pt,yunit=.5pt,runit=.5pt}\n";
        // from now on we can output px, but they will be treated as pt

        os << "\\begin{pspicture}(" << doc->getWidth().value("px") << "," << doc->getHeight().value("px") << ")\n";
    }

    m_tr_stack.push( Geom::Scale(1, -1) * Geom::Translate(0, doc->getHeight().value("px")));  /// @fixme hardcoded doc2dt transform

    return fprintf(_stream, "%s", os.str().c_str());
}

unsigned int PrintLatex::finish(Inkscape::Extension::Print * /*mod*/)
{
    if (_stream) {
        fprintf(_stream, "\\end{pspicture}\n");

        // Flush stream to be sure.
        fflush(_stream);

        fclose(_stream);
        _stream = nullptr;
    }
    return 0;
}

unsigned int PrintLatex::bind(Inkscape::Extension::Print * /*mod*/, Geom::Affine const &transform, float /*opacity*/)
{
    if (!m_tr_stack.empty()) {
        Geom::Affine tr_top = m_tr_stack.top();
        m_tr_stack.push(transform * tr_top);
    } else {
        m_tr_stack.push(transform);
    }

    return 1;
}

unsigned int PrintLatex::release(Inkscape::Extension::Print * /*mod*/)
{
    m_tr_stack.pop();
    return 1;
}

unsigned int PrintLatex::fill(Inkscape::Extension::Print * /*mod*/,
                              Geom::PathVector const &pathv, Geom::Affine const &transform, SPStyle const *style,
                              Geom::OptRect const & /*pbox*/, Geom::OptRect const & /*dbox*/, Geom::OptRect const & /*bbox*/)
{
    if (!_stream) {
        return 0; // XXX: fixme, returning -1 as unsigned.
    }

    if (style->fill.isColor()) {
        Inkscape::SVGOStringStream os;
        float fill_opacity;

        os.setf(std::ios::fixed);

        fill_opacity=SP_SCALE24_TO_FLOAT(style->fill_opacity.value);
        auto rgb = *style->fill.getColor().converted(Colors::Space::Type::RGB);
        os << "{\n\\newrgbcolor{curcolor}{" << rgb[0] << " " << rgb[1] << " " << rgb[2] << "}\n";
        os << "\\pscustom[linestyle=none,fillstyle=solid,fillcolor=curcolor";
        if (fill_opacity!=1.0) {
            os << ",opacity="<<fill_opacity;
        }

        os << "]\n{\n";

        print_pathvector(os, pathv, transform);

        os << "}\n}\n";

        fprintf(_stream, "%s", os.str().c_str());
    }

    return 0;
}

unsigned int PrintLatex::stroke(Inkscape::Extension::Print * /*mod*/,
                                Geom::PathVector const &pathv, Geom::Affine const &transform, SPStyle const *style,
                                Geom::OptRect const & /*pbox*/, Geom::OptRect const & /*dbox*/, Geom::OptRect const & /*bbox*/)
{
    if (!_stream) {
        return 0; // XXX: fixme, returning -1 as unsigned.
    }

    if (style->stroke.isColor()) {
        Inkscape::SVGOStringStream os;
        Geom::Affine tr_stack = m_tr_stack.top();
        double const scale = tr_stack.descrim();
        os.setf(std::ios::fixed);

        double stroke_opacity = style->stroke_opacity;
        auto rgb = *style->stroke.getColor().converted(Colors::Space::Type::RGB);
        os << "{\n\\newrgbcolor{curcolor}{" << rgb[0] << " " << rgb[1] << " " << rgb[2] << "}\n";

        os << "\\pscustom[linewidth=" << style->stroke_width.computed*scale<< ",linecolor=curcolor";

        if (stroke_opacity!=1.0) {
            os<<",strokeopacity="<<stroke_opacity;
        }

        if (style->stroke_dasharray.set &&  !style->stroke_dasharray.values.empty()) {
            os << ",linestyle=dashed,dash=";
            for (unsigned i = 0; i < style->stroke_dasharray.values.size(); i++) {
                if ((i)) {
                    os << " ";
                }
                os << style->stroke_dasharray.values[i].value;
            }
        }

        os <<"]\n{\n";

        print_pathvector(os, pathv, transform);

        os << "}\n}\n";

        fprintf(_stream, "%s", os.str().c_str());
    }

    return 0;
}

// FIXME: why is 'transform' argument not used?
void
PrintLatex::print_pathvector(SVGOStringStream &os, Geom::PathVector const &pathv_in, const Geom::Affine & /*transform*/)
{
    if (pathv_in.empty())
        return;

//    Geom::Affine tf=transform;   // why was this here?
    Geom::Affine tf_stack=m_tr_stack.top(); // and why is transform argument not used?
    Geom::PathVector pathv = pathv_in * tf_stack; // generates new path, which is a bit slow, but this doesn't have to be performance optimized

    os << "\\newpath\n";

    for(const auto & it : pathv) {

        os << "\\moveto(" << it.initialPoint()[Geom::X] << "," << it.initialPoint()[Geom::Y] << ")\n";

        for(Geom::Path::const_iterator cit = it.begin(); cit != it.end_open(); ++cit) {
            print_2geomcurve(os, *cit);
        }

        if (it.closed()) {
            os << "\\closepath\n";
        }

    }
}

void
PrintLatex::print_2geomcurve(SVGOStringStream &os, Geom::Curve const &c)
{
    using Geom::X;
    using Geom::Y;

    if( is_straight_curve(c) )
    {
        os << "\\lineto(" << c.finalPoint()[X] << "," << c.finalPoint()[Y] << ")\n";
    }
    else if(Geom::CubicBezier const *cubic_bezier = dynamic_cast<Geom::CubicBezier const*>(&c)) {
        std::vector<Geom::Point> points = cubic_bezier->controlPoints();
        os << "\\curveto(" << points[1][X] << "," << points[1][Y] << ")("
                           << points[2][X] << "," << points[2][Y] << ")("
                           << points[3][X] << "," << points[3][Y] << ")\n";
    }
    else {
        //this case handles sbasis as well as all other curve types
        Geom::Path sbasis_path = Geom::cubicbezierpath_from_sbasis(c.toSBasis(), 0.1);

        for(const auto & iter : sbasis_path) {
            print_2geomcurve(os, iter);
        }
    }
}

bool
PrintLatex::textToPath(Inkscape::Extension::Print * ext)
{
    return ext->get_param_bool("textToPath");
}

#include "clear-n_.h"

void PrintLatex::init()
{
    /* SVG in */
    // clang-format off
    Inkscape::Extension::build_from_mem(
        "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">\n"
            "<name>" N_("LaTeX Print") "</name>\n"
            "<id>" SP_MODULE_KEY_PRINT_LATEX "</id>\n"
            "<param gui-hidden=\"true\" name=\"destination\" type=\"string\"></param>\n"
            "<param gui-hidden=\"true\" name=\"textToPath\" type=\"bool\">true</param>\n"
            "<print/>\n"
        "</inkscape-extension>", std::make_unique<PrintLatex>());
    // clang-format on
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

