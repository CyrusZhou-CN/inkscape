// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * PDF parsing using libpoppler.
 *//*
 * Authors:
 * see git history
 * 
 * Derived from Gfx.h from poppler (?) which derives from Xpdf, Copyright 1996-2003 Glyph & Cog, LLC, which is under GPL2+.
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef PDF_PARSER_H
#define PDF_PARSER_H

#ifdef HAVE_CONFIG_H
# include "config.h"  // only include where actually required!
#endif

#include "poppler-transition-api.h"

#ifdef USE_GCC_PRAGMAS
#pragma interface
#endif

#include <2geom/affine.h>
#include <glib/poppler-features.h>
#include <map>
#include <memory>
#include <gdkmm/enums.h>
#include <poppler/Object.h>
#include <string>

#define Operator Operator_Gfx
#include <poppler/Gfx.h>
#undef Operator

namespace Inkscape::Extension::Internal {
class SvgBuilder;
} // namespace Inkscape::Extension::Internal

// TODO clean up and remove using:
using Inkscape::Extension::Internal::SvgBuilder;

class PDFDoc;
class Page;
class GooString;
class XRef;
class Stream;
class Parser;
class Dict;
class Function;
class OutputDev;
class GfxPattern;
class GfxShadingPattern;
class GfxFunctionShading;
class GfxAxialShading;
class GfxRadialShading;
class GfxGouraudTriangleShading;
class GfxPatchMeshShading;
struct GfxPatch;
class GfxState;
struct GfxColor;
class GfxColorSpace;
class GfxResources;
class PDFRectangle;
class CairoFontEngine;

class PdfParser;

//------------------------------------------------------------------------

#define maxOperatorArgs 33

struct PdfOperator {
    char name[4];
    int numArgs;
    TchkType tchk[maxOperatorArgs];
    void (PdfParser::*func)(Object args[], int numArgs);
};

#undef maxOperatorArgs

struct OpHistoryEntry {
    const char *name;       // operator's name
    GfxState *state;        // saved state, NULL if none
    GBool executed;         // whether the operator has been executed

    OpHistoryEntry *next;   // next entry on stack
    unsigned depth;         // total number of entries descending from this
};

//------------------------------------------------------------------------
// PdfParser
//------------------------------------------------------------------------

//------------------------------------------------------------------------
// constants
//------------------------------------------------------------------------

#define pdfFunctionShading  1
#define pdfAxialShading     2
#define pdfRadialShading    3
#define pdfGouraudTriangleShading  4
#define pdfPatchMeshShading 5
#define pdfNumShadingTypes 5

/**
 * PDF parsing module using libpoppler's facilities.
 */
class PdfParser {
public:

  // Constructor for regular output.
    PdfParser(std::shared_ptr<PDFDoc> pdf_doc, SvgBuilder *builderA, Page *page, _POPPLER_CONST PDFRectangle *cropBox);
    // Constructor for a sub-page object.
    PdfParser(XRef *xrefA, SvgBuilder *builderA, Dict *resDict, _POPPLER_CONST PDFRectangle *box);

    virtual ~PdfParser();

    // Interpret a stream or array of streams.
    void parse(Object *obj, GBool topLevel = gTrue);

    // Save graphics state.
    void saveState();

    // Restore graphics state.
    void restoreState();

    // Get the current graphics state object.
    GfxState *getState() { return state; }

    // Set the precision of approximation for specific shading fills.
    void setApproximationPrecision(int shadingType, double colorDelta, int maxDepth);
    void loadOptionalContentLayers(Dict *resources);
    void loadPatternColorProfiles(Dict *resources);
    void loadColorProfile();
    void loadColorSpaceProfile(GfxColorSpace *space, Object *obj);
    std::unique_ptr<GfxPattern> lookupPattern(Object *obj, GfxState *state);

    std::shared_ptr<CairoFontEngine> getFontEngine();

    // Build all annotations provided in layer annotation - page_num
    void build_annots(const Object &annot, int page_num);

private:
    std::shared_ptr<PDFDoc> _pdf_doc;
    std::shared_ptr<CairoFontEngine> _font_engine;

    XRef *xref;          // the xref table for this PDF file
    SvgBuilder *builder; // SVG generator
    GBool subPage;       // is this a sub-page object?
    GBool printCommands; // print the drawing commands (for debugging)
    GfxResources *res;   // resource stack

    GfxState *state;         // current graphics state
    GBool fontChanged;       // set if font or text matrix has changed
    GfxClipType clip;        // do a clip?
    int ignoreUndef;         // current BX/EX nesting level
    Geom::Affine baseMatrix; // default matrix for most recent
                             //   page/form/pattern
    int formDepth;

    Parser *parser; // parser for page content stream(s)

    static PdfOperator opTab[]; // table of operators

    int colorDeltas[pdfNumShadingTypes];
    // max deltas allowed in any color component
    // for the approximation of shading fills
    int maxDepths[pdfNumShadingTypes]; // max recursive depths

    OpHistoryEntry *operatorHistory; // list containing the last N operators

    //! Caches color spaces by name
    std::map<std::string, std::unique_ptr<GfxColorSpace>> colorSpacesCache;

    std::unique_ptr<GfxColorSpace> lookupColorSpaceCopy(Object &);

    void setDefaultApproximationPrecision(); // init color deltas
    void pushOperator(const char *name);
    OpHistoryEntry *popOperator();
    const char *getPreviousOperator(unsigned int look_back = 1); // returns the nth previous operator's name

    void go(GBool topLevel);
    void execOp(Object *cmd, Object args[], int numArgs);
    PdfOperator *findOp(const char *name);
    GBool checkArg(Object *arg, TchkType type);
    int getPos();

    void opOptionalContentGroup(Object args[], int numArgs);

    // graphics state operators
    void opSave(Object args[], int numArgs);
    void opRestore(Object args[], int numArgs);
    void opConcat(Object args[], int numArgs);
    void opSetDash(Object args[], int numArgs);
    void opSetFlat(Object args[], int numArgs);
    void opSetLineJoin(Object args[], int numArgs);
    void opSetLineCap(Object args[], int numArgs);
    void opSetMiterLimit(Object args[], int numArgs);
    void opSetLineWidth(Object args[], int numArgs);
    void opSetExtGState(Object args[], int numArgs);
    void doSoftMask(Object *str, GBool alpha, GfxColorSpace *blendingColorSpace, GBool isolated, GBool knockout,
                    Function *transferFunc, GfxColor *backdropColor);
    void opSetRenderingIntent(Object args[], int numArgs);

    // color operators
    void opSetFillGray(Object args[], int numArgs);
    void opSetStrokeGray(Object args[], int numArgs);
    void opSetFillCMYKColor(Object args[], int numArgs);
    void opSetStrokeCMYKColor(Object args[], int numArgs);
    void opSetFillRGBColor(Object args[], int numArgs);
    void opSetStrokeRGBColor(Object args[], int numArgs);
    void opSetFillColorSpace(Object args[], int numArgs);
    void opSetStrokeColorSpace(Object args[], int numArgs);
    void opSetFillColor(Object args[], int numArgs);
    void opSetStrokeColor(Object args[], int numArgs);
    void opSetFillColorN(Object args[], int numArgs);
    void opSetStrokeColorN(Object args[], int numArgs);

    // path segment operators
    void opMoveTo(Object args[], int numArgs);
    void opLineTo(Object args[], int numArgs);
    void opCurveTo(Object args[], int numArgs);
    void opCurveTo1(Object args[], int numArgs);
    void opCurveTo2(Object args[], int numArgs);
    void opRectangle(Object args[], int numArgs);
    void opClosePath(Object args[], int numArgs);

    // path painting operators
    void opEndPath(Object args[], int numArgs);
    void opStroke(Object args[], int numArgs);
    void opCloseStroke(Object args[], int numArgs);
    void opFill(Object args[], int numArgs);
    void opEOFill(Object args[], int numArgs);
    void opFillStroke(Object args[], int numArgs);
    void opCloseFillStroke(Object args[], int numArgs);
    void opEOFillStroke(Object args[], int numArgs);
    void opCloseEOFillStroke(Object args[], int numArgs);
    void doFillAndStroke(GBool eoFill);
    void doPatternFillFallback(GBool eoFill);
    void doPatternStrokeFallback();
    void doShadingPatternFillFallback(GfxShadingPattern *sPat, GBool stroke, GBool eoFill);
    void opShFill(Object args[], int numArgs);
    void doFunctionShFill(GfxFunctionShading *shading);
    void doFunctionShFill1(GfxFunctionShading *shading, double x0, double y0, double x1, double y1, GfxColor *colors,
                           int depth);
    void doGouraudTriangleShFill(GfxGouraudTriangleShading *shading);
    void gouraudFillTriangle(double x0, double y0, GfxColor *color0, double x1, double y1, GfxColor *color1, double x2,
                             double y2, GfxColor *color2, int nComps, int depth);
    void doPatchMeshShFill(GfxPatchMeshShading *shading);
    void fillPatch(_POPPLER_CONST GfxPatch *patch, int nComps, int depth);
    void doEndPath();

    // path clipping operators
    void opClip(Object args[], int numArgs);
    void opEOClip(Object args[], int numArgs);

    // text object operators
    void opBeginText(Object args[], int numArgs);
    void opEndText(Object args[], int numArgs);

    // text state operators
    void opSetCharSpacing(Object args[], int numArgs);
    void opSetFont(Object args[], int numArgs);
    void opSetTextLeading(Object args[], int numArgs);
    void opSetTextRender(Object args[], int numArgs);
    void opSetTextRise(Object args[], int numArgs);
    void opSetWordSpacing(Object args[], int numArgs);
    void opSetHorizScaling(Object args[], int numArgs);

    // text positioning operators
    void opTextMove(Object args[], int numArgs);
    void opTextMoveSet(Object args[], int numArgs);
    void opSetTextMatrix(Object args[], int numArgs);
    void opTextNextLine(Object args[], int numArgs);

    // text string operators
    void doUpdateFont();
    void opShowText(Object args[], int numArgs);
    void opMoveShowText(Object args[], int numArgs);
    void opMoveSetShowText(Object args[], int numArgs);
    void opShowSpaceText(Object args[], int numArgs);
#if POPPLER_CHECK_VERSION(0,64,0)
  void doShowText(const GooString *s);
#else
  void doShowText(GooString *s);
#endif
  

  // XObject operators
  void opXObject(Object args[], int numArgs);
  void doImage(Object *ref, Stream *str, GBool inlineImg);
  void doForm(Object *str, double *offset = nullptr);
  void doForm1(Object *str, Dict *resDict, double *matrix, double *bbox,
	       GBool transpGroup = gFalse, GBool softMask = gFalse,
	       GfxColorSpace *blendingColorSpace = nullptr,
	       GBool isolated = gFalse, GBool knockout = gFalse,
	       GBool alpha = gFalse, Function *transferFunc = nullptr,
	       GfxColor *backdropColor = nullptr);

  // in-line image operators
  void opBeginImage(Object args[], int numArgs);
  Stream *buildImageStream();
  void opImageData(Object args[], int numArgs);
  void opEndImage(Object args[], int numArgs);

  // type 3 font operators
  void opSetCharWidth(Object args[], int numArgs);
  void opSetCacheDevice(Object args[], int numArgs);

  // compatibility operators
  void opBeginIgnoreUndef(Object args[], int numArgs);
  void opEndIgnoreUndef(Object args[], int numArgs);

  // marked content operators
  void opBeginMarkedContent(Object args[], int numArgs);
  void opEndMarkedContent(Object args[], int numArgs);
  void opMarkPoint(Object args[], int numArgs);

  void pushResources(Dict *resDict);
  void popResources();
};

#endif /* PDF_PARSER_H */

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
