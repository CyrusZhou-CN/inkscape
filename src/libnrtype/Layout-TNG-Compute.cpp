// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Inkscape::Text::Layout::Calculator - text layout engine meaty bits
 *
 * Authors:
 *   Richard Hughes <cyreve@users.sf.net>
 *
 * Copyright (C) 2005 Richard Hughes
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <iomanip>

#include "Layout-TNG.h"
#include "style.h"
#include "font-instance.h"
#include "font-factory.h"
#include "svg/svg-length.h"
#include "object/sp-object.h"
#include "object/sp-flowdiv.h"
#include "Layout-TNG-Scanline-Maker.h"
#include <limits>
#include "livarot/Shape.h"

namespace Inkscape {
namespace Text {

//#define DEBUG_LAYOUT_TNG_COMPUTE
//#define DEBUG_GLYPH

//#define IFTRACE(_code) _code
#define IFTRACE(_code)

#define TRACE(_args) IFTRACE(g_print _args)

/** \brief private to Layout. Does the real work of text flowing.

This class does a standard greedy paragraph wrapping algorithm.

Very high-level overview:

<pre>
foreach(paragraph) {
  call pango_itemize() (_buildPangoItemizationForPara())
  break into spans, without dealing with wrapping (_buildSpansForPara())
  foreach(line in flow shape) {
    foreach(chunk in flow shape) {   (in _buildChunksInScanRun())
      // this inner loop in _measureUnbrokenSpan()
      if the line height changed discard the line and start again
      keep adding characters until we run out of space in the chunk, then back up to the last word boundary
      (do sensible things if there is no previous word break)
    }
    push all the glyphs, chars, spans, chunks and line to output (not completely trivial because we must draw rtl in character order) (in _outputLine())
  }
  push the paragraph (in calculate())
}
</pre>

...and all of that needs to work vertically too, and with all the little details that make life annoying
*/
class Layout::Calculator
{
    class SpanPosition;
    friend class SpanPosition;
    Layout &_flow;
    ScanlineMaker *_scanline_maker;
    unsigned _current_shape_index;     /// index into Layout::_input_wrap_shapes
    PangoContext *_pango_context;
    Direction _block_progression;

    /**
      * For y= attributes in tspan elements et al, we do the adjustment by moving each
      * glyph individually by this number. The spec means that this is maintained across
      * paragraphs.
      *
      * To do non-flow text layout, only the first "y" attribute is normally used. If there is only one
      * "y" attribute in a <tspan> other than the first <tspan>, it is ignored. This allows Inkscape to
      * insert a new line anywhere. On output, the Inkscape determined "y" is written out so other SVG
      * viewers know where to place the <tspans>.
      */
    double _y_offset;

    /** to stop pango from hinting its output, the font factory creates all fonts very large.
    All numbers returned from pango have to be divided by this number \em and divided by
    PANGO_SCALE. See FontFactory::FontFactory(). */
    double _font_factory_size_multiplier;

    /** Temporary storage associated with each item in Layout::_input_stream. */
    struct InputItemInfo {
        bool in_sub_flow;
        Layout *sub_flow;    // this is only set for the first input item in a sub-flow

        InputItemInfo() : in_sub_flow(false), sub_flow(nullptr) {}

        /* fixme: I don't like the fact that InputItemInfo etc. use the default copy constructor and
         * operator= (and thus don't involve incrementing reference counts), yet they provide a free method
         * that does delete or Unref.
         *
         * I suggest using the garbage collector to manage deletion.
         */
        void free()
        {
            if (sub_flow) {
                delete sub_flow;
                sub_flow = nullptr;
            }
        }
    };

    /** Temporary storage associated with each item returned by the call to
        pango_itemize(). */
    struct PangoItemInfo {
        PangoItem *item;
        std::shared_ptr<FontInstance> font;

        PangoItemInfo() : item(nullptr) {}

        /* fixme: I don't like the fact that InputItemInfo etc. use the default copy constructor and
         * operator= (and thus don't involve incrementing reference counts), yet they provide a free method
         * that does delete or Unref.
         *
         * I suggest using the garbage collector to manage deletion.
         */
        void free()
        {
            if (item) {
                pango_item_free(item);
                item = nullptr;
            }
        }
    };

    /** These spans have approximately the same definition as that used for
      * Layout::Span (constant font, direction, etc), except that they are from
      * before we have located the line breaks, so bear no relation to chunks.
      * They are guaranteed to be in at most one PangoItem (spans with no text in
      * them will not have an associated PangoItem), exactly one input object and
      * will only have one change of x, y, dx, dy or rotate attribute, which will
      * be at the beginning. An UnbrokenSpan can cross a chunk boundary, c.f.
      * BrokenSpan.
      */
    struct UnbrokenSpan {
        PangoGlyphString *glyph_string;
        int pango_item_index;           /// index into _para.pango_items, or -1 if this is style only
        unsigned input_index;           /// index into Layout::_input_stream
        Glib::ustring::const_iterator input_stream_first_character;
        double font_size;
        FontMetrics line_height;         /// This is not the CSS line-height attribute!
        double line_height_multiplier;  /// calculated from the font-height css property
        double baseline_shift;          /// calculated from the baseline-shift css property
        SPCSSTextOrientation text_orientation;
        unsigned text_bytes;
        unsigned char_index_in_para;    /// the index of the first character in this span in the paragraph, for looking up char_attributes
        SVGLength x, y, dx, dy, rotate;  // these are reoriented copies of the <tspan> attributes. We change span when we encounter one.

        UnbrokenSpan() : glyph_string(nullptr) {}
        void free()
        {
            if (glyph_string)
                pango_glyph_string_free(glyph_string);
            glyph_string = nullptr;
        }
    };


    /** Used to provide storage for anything that applies to the current
    paragraph only. Since we're only processing one paragraph at a time,
    there's only one instantiation of this struct, on the stack of
    calculate(). */
    struct ParagraphInfo {
        Glib::ustring text;
        unsigned first_input_index;      ///< Index into Layout::_input_stream.
        Direction direction;
        Alignment alignment;
        std::vector<InputItemInfo> input_items;
        std::vector<PangoItemInfo> pango_items;
        std::vector<PangoLogAttr> char_attributes;    ///< For every character in the paragraph.
        std::vector<UnbrokenSpan> unbroken_spans;

        template<typename T> static void free_sequence(T &seq)
        {
            for (typename T::iterator it(seq.begin()); it != seq.end(); ++it) {
                it->free();
            }
            seq.clear();
        }

        void free()
        {
            text = "";
            free_sequence(input_items);
            free_sequence(pango_items);
            free_sequence(unbroken_spans);
        }
    };


    /**
      * A useful little iterator for moving char-by-char across spans.
      */
    struct UnbrokenSpanPosition {
        std::vector<UnbrokenSpan>::iterator iter_span;
        unsigned char_byte;
        unsigned char_index;

        void increment();   ///< Step forward by one character.

        inline bool operator== (UnbrokenSpanPosition const &other) const
            {return char_byte == other.char_byte && iter_span == other.iter_span;}
        inline bool operator!= (UnbrokenSpanPosition const &other) const
            {return char_byte != other.char_byte || iter_span != other.iter_span;}
    };

    /**
      * The line breaking algorithm will convert each UnbrokenSpan into one
      * or more of these. A BrokenSpan will never cross a chunk boundary,
      * c.f. UnbrokenSpan.
      */
    struct BrokenSpan {
        UnbrokenSpanPosition start;
        UnbrokenSpanPosition end;    // the end of this will always be the same as the start of the next
        unsigned start_glyph_index;
        unsigned end_glyph_index;
        double width;
        unsigned whitespace_count;
        bool ends_with_whitespace;
        double each_whitespace_width;
        double letter_spacing; // Save so we can subtract from width at end of line (for center justification)
        double word_spacing;
        void setZero();
    };

    /** The definition of a chunk used here is the same as that used in Layout:
    A collection of contiguous broken spans on the same line. (One chunk per line
    unless shape splits line into several sections... then one chunk per section. */
    struct ChunkInfo {
        std::vector<BrokenSpan> broken_spans;
        double scanrun_width;
        double text_width;       ///< Total width used by the text (excluding justification).
        double x;
        int whitespace_count;
    };

    void _buildPangoItemizationForPara(ParagraphInfo *para) const;
    static double _computeFontLineHeight( SPStyle const *style ); // Returns line_height_multiplier
    unsigned _buildSpansForPara(ParagraphInfo *para) const;
    bool _goToNextWrapShape();
    void _createFirstScanlineMaker();

    bool _findChunksForLine(ParagraphInfo const &para,
                            UnbrokenSpanPosition *start_span_pos,
                            std::vector<ChunkInfo> *chunk_info,
                            FontMetrics *line_box_height,
                            FontMetrics const *strut_height);

    bool _buildChunksInScanRun(ParagraphInfo const &para,
                               UnbrokenSpanPosition const &start_span_pos,
                               ScanlineMaker::ScanRun const &scan_run,
                               std::vector<ChunkInfo> *chunk_info,
                               FontMetrics *line_height) const;

    bool _measureUnbrokenSpan(ParagraphInfo const &para,
                              BrokenSpan *span,
                              BrokenSpan *last_break_span,
                              BrokenSpan *last_emergency_break_span,
                              double maximum_width) const;

    double _getChunkLeftWithAlignment(ParagraphInfo const &para,
                                      std::vector<ChunkInfo>::const_iterator it_chunk,
                                      double *add_to_each_whitespace) const;

    void _outputLine(ParagraphInfo const &para,
                     FontMetrics const &line_height,
                     std::vector<ChunkInfo> const &chunk_info,
                     bool hidden);

    static inline PangoLogAttr const &_charAttributes(ParagraphInfo const &para,
                                                      UnbrokenSpanPosition const &span_pos)
    {
        return para.char_attributes[span_pos.iter_span->char_index_in_para + span_pos.char_index];
    }

#ifdef DEBUG_LAYOUT_TNG_COMPUTE
    static void dumpPangoItemsOut(ParagraphInfo *para);
    static void dumpUnbrokenSpans(ParagraphInfo *para);
#endif //DEBUG_LAYOUT_TNG_COMPUTE

public:
    Calculator(Layout *text_flow)
        : _flow(*text_flow) {}

    bool calculate();
};


/**
 *  Computes the width of a single UnbrokenSpan (pointed to by span->start.iter_span)
 *  and outputs its vital statistics into the other fields of \a span.
 *  Measuring will stop if maximum_width is reached and in that case the
 *  function will return false. In other cases where a line break must be
 *  done immediately the function will also return false. On return
 *  \a last_break_span will contain the vital statistics for the span only
 *  up to the last line breaking change. If there are no line breaking
 *  characters in the span then \a last_break_span will not be altered.
 *  Similarly, \a last_emergency_break_span will contain the vital
 *  statistics for the span up to the last inter-character boundary,
 *  or will be unaltered if there is none.
 *
 *  An unbroken span corresponds to at most one PangoItem
 */
bool Layout::Calculator::_measureUnbrokenSpan(ParagraphInfo const &para,
                                              BrokenSpan *span,
                                              BrokenSpan *last_break_span,
                                              BrokenSpan *last_emergency_break_span,
                                              double maximum_width) const
{
    TRACE(("      start _measureUnbrokenSpan %g\n", maximum_width));
    span->setZero();

    if (span->start.iter_span->dx._set && span->start.char_byte == 0){
        if(para.direction == RIGHT_TO_LEFT){
            span->width -= span->start.iter_span->dx.computed;
        } else {
            span->width += span->start.iter_span->dx.computed;
        }
    }

    if (span->start.iter_span->pango_item_index == -1) {
        // if this is a style-only span there's no text in it
        // so we don't need to do very much at all
        span->end.iter_span++;
        return true;
    }

    if (_flow._input_stream[span->start.iter_span->input_index]->Type() == CONTROL_CODE) {

        InputStreamControlCode const *control_code = static_cast<InputStreamControlCode const *>(_flow._input_stream[span->start.iter_span->input_index]);

        if (control_code->code == SHAPE_BREAK || control_code->code == PARAGRAPH_BREAK) {
            *last_emergency_break_span = *last_break_span = *span;
            return false;
        }

        if (control_code->code == ARBITRARY_GAP) { // Not used!
            if (span->width + control_code->width > maximum_width)
                return false;
            TRACE(("        fitted control code, width = %f\n", control_code->width));
            span->width += control_code->width;
            span->end.increment();
        }
        return true;
    }

    if (_flow._input_stream[span->start.iter_span->input_index]->Type() != TEXT_SOURCE)
        return true;  // never happens

    InputStreamTextSource const *text_source = static_cast<InputStreamTextSource const *>(_flow._input_stream[span->start.iter_span->input_index]);

    if (_directions_are_orthogonal(_block_progression, text_source->styleGetBlockProgression())) {
        // TODO: block-progression altered in the middle
        // Measure the precomputed flow from para.input_items
        span->end.iter_span++;  // for now, skip to the next span
        return true;
    }

    // a normal span going with a normal block-progression
    double font_size_multiplier = span->start.iter_span->font_size / (PANGO_SCALE * _font_factory_size_multiplier);
    double soft_hyphen_glyph_width = 0.0;
    bool soft_hyphen_in_word = false;
    bool is_soft_hyphen = false;
    IFTRACE(int char_count = 0);

    // if we're not at the start of the span we need to pre-init glyph_index
    span->start_glyph_index = 0;
    while (span->start_glyph_index < (unsigned)span->start.iter_span->glyph_string->num_glyphs
           && span->start.iter_span->glyph_string->log_clusters[span->start_glyph_index] < (int)span->start.char_byte)
        span->start_glyph_index++;
    span->end_glyph_index = span->start_glyph_index;

    // go char-by-char summing the width, while keeping track of the previous break point
    do {
        PangoLogAttr const &char_attributes = _charAttributes(para, span->end);

        // guint32 c = *Glib::ustring::const_iterator(span->end.iter_span->input_stream_first_character.base() + span->end.char_byte);
        // std::cout << "        char_byte: " << span->end.char_byte
        //           << "  char_index: " << span->end.char_index
        //           << "  c: " << c << " " << char(c==10 ? '␤' : c)
        //           << "  line: "      << std::boolalpha << char_attributes.is_line_break
        //           << "  mandatory: " << std::boolalpha << char_attributes.is_mandatory_break // Note, break is before character!
        //           << "  char: "      << std::boolalpha << char_attributes.is_char_break
        //           << std::endl;

        if (char_attributes.is_mandatory_break && span->end != span->start) {
            TRACE(("      is_mandatory_break  ************\n"));
            *last_emergency_break_span = *last_break_span = *span;
            TRACE(("        span %ld end of para; width = %f chars = %d\n", span->start.iter_span - para.unbroken_spans.begin(), span->width, char_count));
            return false;
        }

        if (char_attributes.is_line_break) {
            TRACE(("        is_line_break  ************\n"));
            // a suitable position to break at, record where we are
            *last_emergency_break_span = *last_break_span = *span;
            if (soft_hyphen_in_word) {
                // if there was a previous soft hyphen we're not going to need it any more so we can remove it
                span->width -= soft_hyphen_glyph_width;
                if (!is_soft_hyphen)
                    soft_hyphen_in_word = false;
            }
        } else if (char_attributes.is_char_break) {
            *last_emergency_break_span = *span;
        }
        // todo: break between chars if necessary (ie no word breaks present) when doing rectangular flowing

        // sum the glyph widths, letter spacing, word spacing, and textLength adjustment to get the character width
        double char_width = 0.0;
        while (span->end_glyph_index < (unsigned)span->end.iter_span->glyph_string->num_glyphs
               && span->end.iter_span->glyph_string->log_clusters[span->end_glyph_index] <= (int)span->end.char_byte) {

            PangoGlyphInfo *info = &(span->end.iter_span->glyph_string->glyphs[span->end_glyph_index]);
            double glyph_width    = font_size_multiplier * info->geometry.width;

            // Advance does not include kerning but Pango gives wrong advances for vertical text
            // with upright orientation (pre 1.44.0).
            auto font = para.pango_items[span->end.iter_span->pango_item_index].font;
            double font_size = span->start.iter_span->font_size;
          //double glyph_h_advance = font_size * font->Advance(info->glyph, false);
            double glyph_v_advance = font_size * font->Advance(info->glyph, true );

            if (_block_progression == LEFT_TO_RIGHT || _block_progression == RIGHT_TO_LEFT) {
                // Vertical text

                if( text_source->style->text_orientation.computed == SP_CSS_TEXT_ORIENTATION_SIDEWAYS ||
                    (text_source->style->text_orientation.computed == SP_CSS_TEXT_ORIENTATION_MIXED &&
                     para.pango_items[span->end.iter_span->pango_item_index].item->analysis.gravity == PANGO_GRAVITY_SOUTH) ) {
                    // Sideways orientation
                    char_width += glyph_width;
                } else {
                    // Upright orientation
                    guint32 c = *Glib::ustring::const_iterator(span->end.iter_span->input_stream_first_character.base() + span->end.char_byte);
                    if (g_unichar_type (c) != G_UNICODE_NON_SPACING_MARK) {
                        // Non-spacing marks should not contribute to width. Fonts may not report the correct advance, especially if the 'vmtx' table is missing.
                        if (pango_version_check(1,44,0) != nullptr) {
                            // Pango >= 1.44.0
                            char_width += glyph_width;
                        } else {
                            // Pango < 1.44.0  glyph_width returned is horizontal width, not vertical.
                            char_width += glyph_v_advance;
                        }
                    }
                }
            } else {
                // Horizontal text
                char_width += glyph_width;
            }
            span->end_glyph_index++;
        }

        if (char_attributes.is_cursor_position)
            char_width += text_source->style->letter_spacing.computed * _flow.getTextLengthMultiplierDue();
        if (char_attributes.is_white)
            char_width += text_source->style->word_spacing.computed * _flow.getTextLengthMultiplierDue();
        char_width += _flow.getTextLengthIncrementDue();
        span->width += char_width;
        IFTRACE(char_count++);

        if (char_attributes.is_white) {
            span->whitespace_count++;
            span->each_whitespace_width = char_width;
        }
        span->ends_with_whitespace = char_attributes.is_white;

        is_soft_hyphen = (UNICODE_SOFT_HYPHEN == *Glib::ustring::const_iterator(span->end.iter_span->input_stream_first_character.base() + span->end.char_byte));
        if (is_soft_hyphen)
            soft_hyphen_glyph_width = char_width;

        // Go to next character (resets end.char_byte to zero if at end)
        span->end.increment();

        // Width should not include letter_spacing (or word_spacing) after last letter at end of line.
        // word_spacing is attached to white space that is already removed from line end (?)
        double test_width = span->width - text_source->style->letter_spacing.computed;

        // Save letter_spacing and word_spacing for subtraction later if span is last span in line.
        span->letter_spacing = text_source->style->letter_spacing.computed;
        span->word_spacing   = text_source->style->word_spacing.computed;

        if (test_width > maximum_width && !char_attributes.is_white) { // whitespaces don't matter, we can put as many as we want at eol
            TRACE(("        span %ld exceeded scanrun; width = %f chars = %d\n", span->start.iter_span - para.unbroken_spans.begin(), span->width, char_count));
            return false;
        }

    } while (span->end.char_byte != 0);  // while we haven't wrapped to the next span

    TRACE(("        fitted span %ld width = %f chars = %d\n", span->start.iter_span - para.unbroken_spans.begin(), span->width, char_count));
    TRACE(("      end _measureUnbrokenSpan %g\n", maximum_width));
    return true;
}

/* *********************************************************************************************************/
//                             Per-line functions (output)

/** Uses the paragraph alignment and the chunk information to work out
 *  where the actual left of the final chunk must be. Also sets
 *  \a add_to_each_whitespace to be the amount of x to add at each
 *  whitespace character to make full justification work.
 */
double Layout::Calculator::_getChunkLeftWithAlignment(ParagraphInfo const &para,
                                                      std::vector<ChunkInfo>::const_iterator it_chunk,
                                                      double *add_to_each_whitespace) const
{
    *add_to_each_whitespace = 0.0;
    if (_flow._input_wrap_shapes.empty()) {
        switch (para.alignment) {
            case FULL:
            case LEFT:
            default:
                return it_chunk->x;
            case RIGHT:
                return it_chunk->x - it_chunk->text_width;
            case CENTER:
                return it_chunk->x - it_chunk->text_width/ 2;
        }
    }

    switch (para.alignment) {
        case FULL:
            // Don't justify the last line chunk in the span
            if (!it_chunk->broken_spans.empty() && it_chunk->broken_spans.back().end.iter_span != para.unbroken_spans.end()) {

                // Don't justify a single word or a line that ends with a manual line break.
                PangoLogAttr const &char_attributes = _charAttributes(para, it_chunk->broken_spans.back().end);
                if (it_chunk->whitespace_count && !char_attributes.is_mandatory_break) {

                    // Set the amount of extra space between each word to a fraction
                    // of the remaining line space to justify this line.
                    *add_to_each_whitespace = (it_chunk->scanrun_width - it_chunk->text_width) / it_chunk->whitespace_count;
                }
            }
            return it_chunk->x;
        case LEFT:
        default:
            return it_chunk->x;
        case RIGHT:
            return it_chunk->x + it_chunk->scanrun_width - it_chunk->text_width;
        case CENTER:
            return it_chunk->x + (it_chunk->scanrun_width - it_chunk->text_width) / 2;
    }
}

/**
 * Once we've got here we have finished making changes to the line
 * and are ready to output the final result to #_flow.
 * This method takes its input parameters and does that.
 */
void Layout::Calculator::_outputLine(ParagraphInfo const &para,
                                     FontMetrics const &line_height,
                                     std::vector<ChunkInfo> const &chunk_info,
                                     bool hidden)
{
    TRACE(("  Start _outputLine: ascent %f, descent %f, top of box %f\n", line_height.ascent, line_height.descent, _scanline_maker->yCoordinate() ));
    if (chunk_info.empty()) {
        TRACE(("    line too short to fit anything on it, go to next\n"));
        return;
    }

    // we've finished fiddling about with ascents and descents: create the output
    TRACE(("    found line fit; creating output\n"));
    Layout::Line new_line;
    new_line.in_paragraph = _flow._paragraphs.size() - 1;
    new_line.baseline_y = _scanline_maker->yCoordinate();
    new_line.hidden = hidden;

    // The y coordinate is at the beginning edge of the line box (top for horizontal text, left
    // edge for vertical lr text, right edge for vertical rl text. We align, by default to the
    // alphabetic baseline for horizontal text and the central baseline for vertical text.
    if( _block_progression == RIGHT_TO_LEFT ) {
        // Vertical text, use em box center as baseline
        new_line.baseline_y -= 0.5 * line_height.emSize();
    } else if ( _block_progression == LEFT_TO_RIGHT ) {
        // Vertical text, use em box center as baseline
        new_line.baseline_y += 0.5 * line_height.emSize();
    } else {
        new_line.baseline_y += line_height.getTypoAscent();
    }


    TRACE(("    initial new_line.baseline_y: %f\n", new_line.baseline_y ));

    new_line.in_shape = _current_shape_index;
    _flow._lines.push_back(new_line);

    for (std::vector<ChunkInfo>::const_iterator it_chunk = chunk_info.begin() ; it_chunk != chunk_info.end() ; it_chunk++) {
        double add_to_each_whitespace;
        // add the chunk to the list
        Layout::Chunk new_chunk;
        new_chunk.in_line = _flow._lines.size() - 1;

        TRACE(("    New chunk: in_line: %d\n", new_chunk.in_line));
        if (hidden) {
            new_chunk.left_x = it_chunk->x; // Don't align. We'll place below last shape.
        } else {
            new_chunk.left_x = _getChunkLeftWithAlignment(para, it_chunk, &add_to_each_whitespace);
        }

        // we may also have y move orders to deal with here (dx, dy and rotate are done per span)

        // Comment updated:  23 July 2010:
        // We must handle two cases:
        //
        //   1. Inkscape SVG where the first line is placed by the read-in "y" value and the
        //      rest are determined by 'font-size' and 'line-height' (and not by any
        //      y-kerning). <tspan>s in this case are marked by sodipodi:role="line". This
        //      allows new lines to be inserted in the middle of a <text> object. On output,
        //      new "y" values are calculated for each <tspan> that represents a new line. Line
        //      spacing is already handled by the calling routine.
        //
        //   2. Plain SVG where each <text> or <tspan> is placed by its own "x" and "y" values.
        //      Note that in this case Inkscape treats each <text> object with any included
        //      <tspan>s as a single line of text. This can be confusing in the code below.

        if (!it_chunk->broken_spans.empty()                               // Not empty paragraph
            && it_chunk->broken_spans.front().start.char_byte == 0 ) {    // Beginning of unbroken span

            // If empty or new line (sodipode:role="line")
            if( _flow._characters.empty() ||
                _flow._characters.back().chunk(&_flow).in_line != _flow._lines.size() - 1) {

                // This is the Inkscape SVG case.
                //
                // If <tspan> "y" attribute is set, use it (initial "y" attributes in
                // <tspans> other than the first have already been stripped for <tspans>
                // marked with role="line", see sp-text.cpp: SPText::_buildLayoutInput).
                // NOTE: for vertical text, "y" is the user-space "x" value.
                if( it_chunk->broken_spans.front().start.iter_span->y._set ) {

                    // Use set "y" attribute for baseline
                    new_line.baseline_y = it_chunk->broken_spans.front().start.iter_span->y.computed;

                    TRACE(("      chunk new_line.baseline_y: %f\n", new_line.baseline_y ));

                    // Save baseline
                    _flow._lines.back().baseline_y = new_line.baseline_y;

                    // Calculate new top of box... given specified baseline.
                    double top_of_line_box = new_line.baseline_y;
                    if( _block_progression == RIGHT_TO_LEFT ) {
                        // Vertical text, use em box center as baseline
                        top_of_line_box += 0.5 * line_height.emSize();
                    } else if (_block_progression == LEFT_TO_RIGHT ) {
                        // Vertical text, use em box center as baseline
                        top_of_line_box -= 0.5 * line_height.emSize();
                    } else {
                        top_of_line_box -= line_height.getTypoAscent();
                    }
                    TRACE(("      y attribute set, next line top_of_line_box: %f\n", top_of_line_box ));
                    // Set the initial y coordinate of the for this line (see above).
                    _scanline_maker->setNewYCoordinate(top_of_line_box);
                }

                // Reset relative y_offset ("dy" attribute is relative but should be reset at
                // the beginning of each line since each line will have a new "y" written out.)
                _y_offset = 0.0;

            } else {

                // This is the plain SVG case
                //
                // "x" and "y" are used to place text, simulating lines as necessary
                if( it_chunk->broken_spans.front().start.iter_span->y._set ) {
                    _y_offset = it_chunk->broken_spans.front().start.iter_span->y.computed - new_line.baseline_y;
                }
            }
        }
        _flow._chunks.push_back(new_chunk);

        double current_x;
        double direction_sign;
        Direction previous_direction = para.direction;
        double counter_directional_width_remaining = 0.0;
        float glyph_rotate = 0.0;
        if (para.direction == LEFT_TO_RIGHT) {
            direction_sign = +1.0;
            current_x = 0.0;
        } else {
            direction_sign = -1.0;
            if (para.alignment == FULL && !_flow._input_wrap_shapes.empty()){
                current_x = it_chunk->scanrun_width;
            }
            else {
                current_x = it_chunk->text_width;
            }
        }

        // Loop over broken spans; a broken span is part of no more than one PangoItem.
        for (std::vector<BrokenSpan>::const_iterator it_span = it_chunk->broken_spans.begin() ; it_span != it_chunk->broken_spans.end() ; it_span++) {

            // begin adding spans to the list
            UnbrokenSpan const &unbroken_span = *it_span->start.iter_span;
            double x_in_span_last = 0.0;  // set at the END when a new cluster starts
            double x_in_span      = 0.0;  // set from the preceding at the START when a new cluster starts.

            // for (int i = 0; i < unbroken_span.glyph_string->num_glyphs; ++i) {
            //     std::cout << "Unbroken span: " << unbroken_span.glyph_string->glyphs[i].glyph << std::endl;
            // }

            if (it_span->start.char_byte == 0) {
                // Start of an unbroken span, we might have dx, dy or rotate still to process
                // (x and y are done per chunk)
                if (unbroken_span.dx._set) current_x += unbroken_span.dx.computed;
                if (unbroken_span.dy._set) _y_offset += unbroken_span.dy.computed;
                if (unbroken_span.rotate._set) glyph_rotate = unbroken_span.rotate.computed * (M_PI/180);
            }

            if (_flow._input_stream[unbroken_span.input_index]->Type() == TEXT_SOURCE
                && unbroken_span.pango_item_index == -1) {
                // style only, nothing to output
                continue;
            }

            Layout::Span new_span;

            new_span.in_chunk = _flow._chunks.size() - 1;
            new_span.line_height = unbroken_span.line_height;
            new_span.in_input_stream_item = unbroken_span.input_index;
            new_span.baseline_shift = 0.0;
            new_span.block_progression = _block_progression;
            new_span.text_orientation = unbroken_span.text_orientation;
            if ((_flow._input_stream[unbroken_span.input_index]->Type() == TEXT_SOURCE) && (new_span.font = para.pango_items[unbroken_span.pango_item_index].font)) {
                new_span.font_size = unbroken_span.font_size;
                new_span.direction = para.pango_items[unbroken_span.pango_item_index].item->analysis.level & 1 ? RIGHT_TO_LEFT : LEFT_TO_RIGHT;
                new_span.input_stream_first_character = Glib::ustring::const_iterator(unbroken_span.input_stream_first_character.base() + it_span->start.char_byte);
            } else {  // a control code
                new_span.font = nullptr;
                new_span.font_size = new_span.line_height.emSize();
                new_span.direction = para.direction;
            }

            if (new_span.direction == para.direction) {
                current_x -= counter_directional_width_remaining;
                counter_directional_width_remaining = 0.0;
            } else if (new_span.direction != previous_direction) {
                // measure width of spans we need to switch round
                counter_directional_width_remaining = 0.0;
                std::vector<BrokenSpan>::const_iterator it_following_span;
                for (it_following_span = it_span ; it_following_span != it_chunk->broken_spans.end() ; it_following_span++) {
                    if (it_following_span->start.iter_span->pango_item_index == -1) break;
                    Layout::Direction following_span_progression = static_cast<InputStreamTextSource const *>(_flow._input_stream[it_following_span->start.iter_span->input_index])->styleGetBlockProgression();
                    if (!Layout::_directions_are_orthogonal(following_span_progression, _block_progression)) {
                        if (new_span.direction != (para.pango_items[it_following_span->start.iter_span->pango_item_index].item->analysis.level & 1 ? RIGHT_TO_LEFT : LEFT_TO_RIGHT)) break;
                    }
                    counter_directional_width_remaining += direction_sign * (it_following_span->width + it_following_span->whitespace_count * add_to_each_whitespace);
                }
                current_x += counter_directional_width_remaining;
                counter_directional_width_remaining = 0.0;    // we want to go increasingly negative
            }
            new_span.x_start = current_x;
            new_span.y_offset = _y_offset;  // Offset from baseline due to 'y' and 'dy' attributes (used to simulate multiline text).

            if (_flow._input_stream[unbroken_span.input_index]->Type() == TEXT_SOURCE) {
                // the span is set up, push the glyphs and chars

                InputStreamTextSource const *text_source = static_cast<InputStreamTextSource const *>(_flow._input_stream[unbroken_span.input_index]);
                Glib::ustring::const_iterator iter_source_text = Glib::ustring::const_iterator(unbroken_span.input_stream_first_character.base() + it_span->start.char_byte) ;
                unsigned char_index_in_unbroken_span = it_span->start.char_index;
                double   font_size_multiplier        = new_span.font_size / (PANGO_SCALE * _font_factory_size_multiplier);
                int      log_cluster_size_glyphs     = 0;   // Number of glyphs in this log_cluster
                int      log_cluster_size_chars      = 0;   // Number of characters in this log_cluster 
                unsigned end_byte                    = 0;

                // Get some pointers (constant for an unbroken span).
                auto font = para.pango_items[unbroken_span.pango_item_index].font;
                PangoItem *pango_item = para.pango_items[unbroken_span.pango_item_index].item;

                // Loop over glyphs in span
                double x_offset_cluster = 0.0; // Handle wrong glyph positioning post-1.44 Pango.
                double x_offset_center  = 0.0; // Handle wrong glyph positioning in pre-1.44 Pango.
                double x_offset_advance = 0.0; // Handle wrong advance in pre-1.44 Pango.

#ifdef DEBUG_GLYPH
                std::cerr << "\nGlyphs in span: x_start: " << new_span.x_start << " y_offset: " << new_span.y_offset
                          << "  PangoItem flags: " << (int)pango_item->analysis.flags << " Gravity: " << (int)pango_item->analysis.gravity << std::endl;
                std::cerr << "  Unicode  Glyph  h_advance  v_advance  width  cluster    orientation   new_glyph         delta"     << std::endl;
                std::cerr << "   (hex)     No.                                start                   x       y       x       y"   << std::endl;
                std::cerr << "  -------------------------------------------------------------------------------------------------" << std::endl;
#endif

                for (unsigned glyph_index = it_span->start_glyph_index ; glyph_index < it_span->end_glyph_index ; glyph_index++) {

                    unsigned char_byte              = iter_source_text.base() - unbroken_span.input_stream_first_character.base();
                    bool     newcluster             = false;
                    if (unbroken_span.glyph_string->glyphs[glyph_index].attr.is_cluster_start) {
                        newcluster = true;
                        x_in_span = x_in_span_last;
                    }

                    if (unbroken_span.glyph_string->log_clusters[glyph_index] < (int)unbroken_span.text_bytes
                        && *iter_source_text == UNICODE_SOFT_HYPHEN
                        && glyph_index + 1 != it_span->end_glyph_index) {
                        // if we're looking at a soft hyphen and it's not the last glyph in the
                        // chunk we don't draw the glyph but we still need to add to _characters
                        Layout::Character new_character;
                        new_character.the_char = *iter_source_text;
                        new_character.in_span = _flow._spans.size();     // the span hasn't been added yet, so no -1
                        new_character.char_attributes = para.char_attributes[unbroken_span.char_index_in_para + char_index_in_unbroken_span];
                        new_character.in_glyph = -1;
                        _flow._characters.push_back(new_character);
                        iter_source_text++;
                        char_index_in_unbroken_span++;
                        while (glyph_index < (unsigned)unbroken_span.glyph_string->num_glyphs
                               && unbroken_span.glyph_string->log_clusters[glyph_index] == (int)char_byte)
                            glyph_index++;
                        glyph_index--;
                        continue;
                    }

                    // create the Layout::Glyph
                    PangoGlyphInfo *unbroken_span_glyph_info = &unbroken_span.glyph_string->glyphs[glyph_index];
                    double glyph_width = font_size_multiplier * unbroken_span_glyph_info->geometry.width;

                    Layout::Glyph new_glyph;
                    new_glyph.glyph = unbroken_span_glyph_info->glyph;
                    new_glyph.in_character = _flow._characters.size();
                    new_glyph.rotation = glyph_rotate;
                    new_glyph.orientation = ORIENTATION_UPRIGHT; // Only effects vertical text
                    new_glyph.hidden = hidden; // SVG 2 overflow

                    // Advance does not include kerning but Pango <= 1.43 gives wrong advances for verical upright text.
                    double glyph_h_advance = new_span.font_size * font->Advance(new_glyph.glyph, false);
                    double glyph_v_advance = new_span.font_size * font->Advance(new_glyph.glyph, true );

#ifdef DEBUG_GLYPH

                    bool is_cluster_start = unbroken_span_glyph_info->attr.is_cluster_start;
                    std::cerr << "  " << std::hex << std::setw(6) << *iter_source_text << std::dec
                              << "  " << std::setw(6) << new_glyph.glyph
                              << std::fixed << std::showpoint << std::setprecision(2)
                              << "   " << std::setw(6) << glyph_h_advance
                              << "   " << std::setw(6) << glyph_v_advance
                              << "   " << std::setw(6) << glyph_width
                              << "   " << std::setw(6) << std::boolalpha << is_cluster_start; // << std::endl;
#endif

                    // We may have scaled font size to fit textLength; now, if
                    // @lengthAdjust=spacingAndGlyphs, this scaling must be only horizontal,
                    // not vertical, so we unscale it back vertically during output
                    if (_flow.lengthAdjust == Inkscape::Text::Layout::LENGTHADJUST_SPACINGANDGLYPHS)
                        new_glyph.vertical_scale = 1.0 / _flow.getTextLengthMultiplierDue();
                    else
                        new_glyph.vertical_scale = 1.0;

                    // Position glyph --------------------
                    new_glyph.x = current_x;
                    new_glyph.y =_y_offset;
                    new_glyph.advance = glyph_width;

                    if (*iter_source_text == '\n') {
                        // Line feeds should take zero space but they are given 'space' width.
                        new_glyph.advance = 0.0;
                    }

                    // y-coordinate is flipped between vertical and horizontal text...
                    // delta_y is common offset but applied with opposite sign
                    double delta_x = unbroken_span_glyph_info->geometry.x_offset * font_size_multiplier;
                    double delta_y = unbroken_span_glyph_info->geometry.y_offset * font_size_multiplier - unbroken_span.baseline_shift;
                    SPCSSBaseline dominant_baseline = _flow._blockBaseline();

                    if (_block_progression == LEFT_TO_RIGHT || _block_progression == RIGHT_TO_LEFT) {
                        // Vertical text

                        // Default dominant baseline is determined by overall block (i.e. <text>) 'text-orientation' value.
                        if( _flow._blockTextOrientation() != SP_CSS_TEXT_ORIENTATION_SIDEWAYS ) {
                            if( dominant_baseline == SP_CSS_BASELINE_AUTO ) dominant_baseline = SP_CSS_BASELINE_CENTRAL;
                        } else {
                            if( dominant_baseline == SP_CSS_BASELINE_AUTO ) dominant_baseline = SP_CSS_BASELINE_ALPHABETIC;
                        }

                        // TODO: Should also check 'glyph_orientation_vertical' if 'text-orientation' is unset...
                        if( new_span.text_orientation == SP_CSS_TEXT_ORIENTATION_SIDEWAYS ||
                            (new_span.text_orientation == SP_CSS_TEXT_ORIENTATION_MIXED && pango_item->analysis.gravity == PANGO_GRAVITY_SOUTH) ) {

                            // Sideways orientation (Latin characters, CJK punctuation), 90deg rotation done at output stage.

#ifdef DEBUG_GLYPH
                            std::cerr << "       Sideways"
                                      << "  " << std::setw(6) << new_glyph.x
                                      << "  " << std::setw(6) << new_glyph.y
                                      << "  " << std::setw(6) << delta_x
                                      << "  " << std::setw(6) << delta_y
                                      << std::endl;
#endif

                            new_glyph.orientation = ORIENTATION_SIDEWAYS;

                            new_glyph.x += delta_x;
                            new_glyph.y -= delta_y;

                            // Multiplying by font-size could cause slight differences in
                            // positioning for different baselines if the font size varies within a
                            // line of text (e.g. sub-scripts and super-scripts).
                            new_glyph.y -= new_span.font_size * font->GetBaselines()[ dominant_baseline ];

                        } else {
                            // Upright orientation

                            auto hb_font = pango_font_get_hb_font(font->get_font());

#ifdef DEBUG_GLYPH
                            std::cerr << "        Upright"
                                      << "  " << std::setw(6) << new_glyph.x
                                      << "  " << std::setw(6) << new_glyph.y
                                      << "  " << std::setw(6) << delta_x
                                      << "  " << std::setw(6) << delta_y;
                            char glyph_name[32];
                            hb_font_get_glyph_name(hb_font, new_glyph.glyph, glyph_name, sizeof (glyph_name));
                            std::cerr << "  " << (glyph_name ? glyph_name : "");
                            std::cerr << std::endl;
#endif

                            if (pango_version_check(1,44,0) != nullptr) {
                                // Pango < 1.44.0 (pre HarfBuzz)
                                new_glyph.x += delta_x;
                                new_glyph.y -= delta_y;

                                double shift = 0;
                                double scale_factor = PANGO_SCALE * _font_factory_size_multiplier;
                                if (!font->has_vertical()) {

                                    // If there are no vertical metrics, glyphs are vertically
                                    // centered before base anchor to mark anchor distance is
                                    // calculated by shaper. We must undo this!
                                    PangoRectangle ink_rect;
                                    PangoRectangle logical_rect;
                                    pango_font_get_glyph_extents (font->get_font(),
                                                                  new_glyph.glyph,
                                                                  &ink_rect,
                                                                  &logical_rect);

                                    // Shift required to move centered glyph back to proper position
                                    // relative to baseline.
                                    shift =
                                        font->GetTypoAscent() +
                                        ink_rect.y / scale_factor + // negative
                                        (ink_rect.height / scale_factor / 2.0) -
                                        0.5;
                                }

                                // Advance is wrong (horizontal width used instead of vertical)...
                                if (g_unichar_type(*iter_source_text) != G_UNICODE_NON_SPACING_MARK) {

                                    x_offset_advance = new_glyph.advance - glyph_v_advance;
                                    new_glyph.advance = glyph_v_advance;

                                    x_offset_center = shift;
                                } else {
                                    // Is non-spacing mark!
                                    if (!font->has_vertical()) {

                                        // If font lacks vertical metrics, all glyphs have em-box advance
                                        // but non-spacing marks should have zero advance.
                                        new_glyph.advance = 0;

                                        // Correct for base anchor to mark anchor shift.
                                        new_glyph.x += (x_offset_center - shift) * new_span.font_size;
                                    }

                                    // Correct for advance error.
                                    new_glyph.x += x_offset_advance;
                                }

                                // Need to shift by horizontal to vertical origin (as we don't load glyph
                                // with vertical metrics).
                                new_glyph.x += font->GetTypoAscent() * new_span.font_size;
                                new_glyph.y -= glyph_h_advance/2.0;

                            } else if (pango_version_check(1,48,1) != nullptr) {
                                // 1.44.0 <= Pango < 1.48.1 (minus sign error, mismatch between Cairo/Harfbuzz glyph
                                // placement)
                                new_glyph.x += (glyph_width - delta_x);
                                new_glyph.y -= delta_y;
                            } else if (pango_version_check(1,48,4) != nullptr) {
                                // 1.48.1 <= Pango < 1.48.4 (minus sign fix, partial fix for Cairo/Harfbuzz mismatch,
                                // but bad mark positioning)
                                new_glyph.x += delta_x;
                                new_glyph.y -= delta_y;

                                // Need to shift by horizontal to vertical origin. Recall Pango lays out vertical text
                                // as horizontal text then rotates by 90 degress so y_origin -> x, x_origin -> -y.
                                hb_position_t x_origin = 0.0;
                                hb_position_t y_origin = 0.0;
                                hb_font_get_glyph_v_origin(hb_font, new_glyph.glyph, &x_origin, &y_origin);
                                new_glyph.x += y_origin * font_size_multiplier;
                                new_glyph.y -= x_origin * font_size_multiplier;
                            } else {
                                // 1.48.4 <= Pango (good mark positioning)
                                new_glyph.x += delta_x;
                                new_glyph.y -= delta_y;
                            }

                            // If a font has no vertical metrics, HarfBuzz using OpenType functions
                            // (which Pango uses by default from 1.44.0) to position glyphs so that
                            // the top of their "ink rectangle" is at the top of the "em-box". This
                            // section of code moves each cluster (base glyph with marks) down to
                            // match fonts with vertical metrics.
                            hb_font_extents_t hb_font_extents_not_used;
                            if (!hb_font_get_v_extents(hb_font, &hb_font_extents_not_used)) {
                                // Font does not have vertical metrics!

                                if (g_unichar_type(*iter_source_text) !=
                                    G_UNICODE_NON_SPACING_MARK) { // Probably should include other marks!
                                    hb_glyph_extents_t glyph_extents;
                                    if (hb_font_get_glyph_extents(hb_font, new_glyph.glyph, &glyph_extents)) {

                                        // double baseline_adjust =
                                        //     font_instance->get_baseline(BASELINE_TEXT_BEFORE_EDGE) -
                                        //     font_instance->get_baseline(BASELINE_ALPHABETIC);
                                        // std::cout << "baseline_adjust: " << baseline_adjust << std::endl;
                                        double baseline_adjust = new_span.line_height.ascent / new_span.font_size;
                                        int hb_x_scale = 0;
                                        int hb_y_scale = 0;
                                        hb_font_get_scale(hb_font, &hb_x_scale, &hb_y_scale);
                                        x_offset_cluster =
                                            ((glyph_extents.y_bearing / (double)hb_y_scale) - baseline_adjust) *
                                            new_span.font_size;
                                    } else {
                                        x_offset_cluster = 0.0; // Failed to find extents.
                                    }
                                } else {
                                    // Is non-spacing mark!

                                    // Many fonts report a non-zero vertical advance for marks, especially if the 'vmtx'
                                    // table is missing.
                                    new_glyph.advance = 0;
                                }

                                new_glyph.x -= x_offset_cluster;
                            }

                        }
                    } else {
                        // Horizontal text

#ifdef DEBUG_GLYPH
                        std::cerr << "     Horizontal"
                                  << "  " << std::setw(6) << new_glyph.x
                                  << "  " << std::setw(6) << new_glyph.y
                                  << "  " << std::setw(6) << delta_x
                                  << "  " << std::setw(6) << delta_y
                                  << std::endl;
#endif

                        if( dominant_baseline == SP_CSS_BASELINE_AUTO ) dominant_baseline = SP_CSS_BASELINE_ALPHABETIC;

                        new_glyph.x += delta_x;
                        new_glyph.y += delta_y;

                        new_glyph.y += new_span.font_size * font->GetBaselines()[ dominant_baseline ];
                    }

                    // Correct for right to left text
                    if (new_span.direction == RIGHT_TO_LEFT) {

                        // The following commented out code is from 2005. Subtracting cluster width gives wrong placement if more
                        // than one glyph has a horizontal advance. See GitHub issue 469. I leave the old code here in case switching to
                        // subtracting only the glyph width causes unforseen bugs.

                        // // pango wanted to give us glyphs in visual order but we refused, so we need to work
                        // // out where the cluster start is ourselves

                        // // Add up widths of remaining glyphs in span.
                        // double cluster_width = 0.0;
                        // std::cout << "  glyph_index: " << glyph_index << " end_glyph_index: " << it_span->end_glyph_index << std::endl;
                        // for (unsigned rtl_index = glyph_index; rtl_index < it_span->end_glyph_index ; rtl_index++) {
                        //     if (unbroken_span.glyph_string->glyphs[rtl_index].attr.is_cluster_start && rtl_index != glyph_index) {
                        //         break;
                        //     }
                        //     cluster_width += font_size_multiplier * unbroken_span.glyph_string->glyphs[rtl_index].geometry.width;
                        // }
                        // new_glyph.x -= cluster_width;

                        new_glyph.x -= font_size_multiplier * unbroken_span.glyph_string->glyphs[glyph_index].geometry.width;
                    }

                    // Store glyph data
                    _flow._glyphs.push_back(new_glyph);

                    // Create the Layout::Character(s)
                    if (newcluster) {
                        newcluster = false;

                        // Figure out how many glyphs are in the log_cluster.
                        log_cluster_size_glyphs = 0;
                        for (; log_cluster_size_glyphs + glyph_index < it_span->end_glyph_index; log_cluster_size_glyphs++){
                           if(unbroken_span.glyph_string->log_clusters[glyph_index                          ] !=
                              unbroken_span.glyph_string->log_clusters[glyph_index + log_cluster_size_glyphs]) break;
                        }

                        // Find where the text ends for this log_cluster.
                        end_byte = it_span->start.iter_span->text_bytes;  // Upper limit
                        for(int next_glyph_index = glyph_index+1; next_glyph_index < unbroken_span.glyph_string->num_glyphs; next_glyph_index++){
                            if(unbroken_span.glyph_string->glyphs[next_glyph_index].attr.is_cluster_start){
                                end_byte = unbroken_span.glyph_string->log_clusters[next_glyph_index];
                                break;
                            }
                        }

                        // Figure out how many characters are in the log_cluster.
                        log_cluster_size_chars  = 0;
                        Glib::ustring::const_iterator lclist = iter_source_text;
                        unsigned lcb = char_byte;
                        while (lcb < end_byte){
                            log_cluster_size_chars++;
                            lclist++;
                            lcb = lclist.base() - unbroken_span.input_stream_first_character.base();
                        }
                    }

                    double advance_width = new_glyph.advance;
                    while (char_byte < end_byte) {

                        /* Hack to survive ligatures:  in log_cluster keep the number of available chars >= number of glyphs remaining.
                           When there are no ligatures these two sizes are always the same.
                        */
                        if (log_cluster_size_chars < log_cluster_size_glyphs) {
                           log_cluster_size_glyphs--;
                           break;
                        }

                        // Store character info
                        Layout::Character new_character;
                        new_character.the_char = *iter_source_text;
                        new_character.in_span = _flow._spans.size();
                        new_character.x = x_in_span;
                        new_character.char_attributes = para.char_attributes[unbroken_span.char_index_in_para + char_index_in_unbroken_span];
                        new_character.in_glyph = (hidden ? -1 : _flow._glyphs.size() - 1);
                        _flow._characters.push_back(new_character);

                        // Letter/word spacing and justification
                        if (new_character.char_attributes.is_white)
                            advance_width += text_source->style->word_spacing.computed * _flow.getTextLengthMultiplierDue() + add_to_each_whitespace;    // justification
                        if (new_character.char_attributes.is_cursor_position)
                            advance_width += text_source->style->letter_spacing.computed * _flow.getTextLengthMultiplierDue();
                        advance_width += _flow.getTextLengthIncrementDue();

                        // Update counters
                        iter_source_text++;
                        char_index_in_unbroken_span++;
                        char_byte = iter_source_text.base() - unbroken_span.input_stream_first_character.base();
                        log_cluster_size_chars--;
                    }

                    // Update x position variables
                    advance_width *= direction_sign;
                    if (new_span.direction != para.direction) {
                        counter_directional_width_remaining -= advance_width;
                        current_x -= advance_width;
                        x_in_span_last -= advance_width;
                    } else {
                        current_x += advance_width;
                        x_in_span_last += advance_width;
                    }
                } // Loop over glyphs in span

            } else if (_flow._input_stream[unbroken_span.input_index]->Type() == CONTROL_CODE) {
                current_x += static_cast<InputStreamControlCode const *>(_flow._input_stream[unbroken_span.input_index])->width;
            }

            new_span.x_end = new_span.x_start + x_in_span_last;
            _flow._spans.push_back(new_span);
            previous_direction = new_span.direction;
        }
        // end adding spans to the list, on to the next chunk...
    }
    TRACE(("  End _outputLine\n"));
}

/**
 * Initialises the ScanlineMaker for the first shape in the flow,
 * or the infinite version if we're not doing wrapping.
 */
void Layout::Calculator::_createFirstScanlineMaker()
{
    _current_shape_index = 0;
    InputStreamTextSource const *text_source = static_cast<InputStreamTextSource const *>(_flow._input_stream.front());
    if (_flow._input_wrap_shapes.empty()) {
        // create the special no-wrapping infinite scanline maker
        double initial_x = 0, initial_y = 0;
        if (!text_source->x.empty()) {
            initial_x = text_source->x.front().computed;
        }
        if (!text_source->y.empty()) {
            initial_y = text_source->y.front().computed;
        }
        _scanline_maker = new InfiniteScanlineMaker(initial_x, initial_y, _block_progression);
        TRACE(("  wrapping disabled\n"));
    }
    else {
        _scanline_maker =
            new ShapeScanlineMaker(_flow._input_wrap_shapes[_current_shape_index].shape.get(), _block_progression);
        TRACE(("  begin wrap shape 0\n"));

        // 'inline-size' uses an infinitely high (wide) shape. We must set initial y. (We only need to do it here as there is only one shape.)
        if (_flow.wrap_mode == WRAP_INLINE_SIZE) {
            _block_progression = _flow._blockProgression();
            if( _block_progression == RIGHT_TO_LEFT ||
                _block_progression == LEFT_TO_RIGHT ) {
                // Vertical text, CJK
                if (!text_source->x.empty()) {
                    double initial_x = text_source->x.front().computed;
                    _scanline_maker->setNewYCoordinate(initial_x);
                } else {
                    std::cerr << "Layout::Calculator::_createFirstScanlineMaker: no x value with 'inline-size'!" << std::endl;
                    _scanline_maker->setNewYCoordinate(0);
                }
            } else {
                // Horizontal text
                if (!text_source->y.empty()) {
                    double initial_y = text_source->y.front().computed;
                    _scanline_maker->setNewYCoordinate(initial_y);
                } else {
                    std::cerr << "Layout::Calculator::_createFirstScanlineMaker: no y value with 'inline-size'!" << std::endl;
                    _scanline_maker->setNewYCoordinate(0);
                }
            }
        }
    }
}

void Layout::Calculator::UnbrokenSpanPosition::increment()
{
    gchar const *text_base = &*iter_span->input_stream_first_character.base();
    char_byte = g_utf8_next_char(text_base + char_byte) - text_base;
    char_index++;
    if (char_byte == iter_span->text_bytes) {
        iter_span++;
        char_index = char_byte = 0;
    }
}

void Layout::Calculator::BrokenSpan::setZero()
{
    end = start;
    width = 0.0;
    whitespace_count = 0;
    end_glyph_index = start_glyph_index = 0;
    ends_with_whitespace = false;
    each_whitespace_width = 0.0;
    letter_spacing = 0.0;
    word_spacing = 0.0;
}

///**
// * For sections of text with a block-progression different to the rest
// * of the flow, the best thing to do is to detect them in advance and
// * create child TextFlow objects with just the rotated text. In the
// * parent we then effectively use ARBITRARY_GAP fields during the
// * flowing (because we don't allow wrapping when the block-progression
// * changes) and copy the actual text in during the output phase.
// *
// * NB: this code not enabled yet.
// */
//void Layout::Calculator::_initialiseInputItems(ParagraphInfo *para) const
//{
//    Direction prev_block_progression = _block_progression;
//    int run_start_input_index = para->first_input_index;
//
//    para->free_sequence(para->input_items);
//    for(int input_index = para->first_input_index ; input_index < (int)_flow._input_stream.size() ; input_index++) {
//        InputItemInfo input_item;
//
//        input_item.in_sub_flow = false;
//        input_item.sub_flow = NULL;
//        if (_flow._input_stream[input_index]->Type() == CONTROL_CODE) {
//            Layout::InputStreamControlCode const *control_code = static_cast<Layout::InputStreamControlCode const *>(_flow._input_stream[input_index]);
//            if (   control_code->code == SHAPE_BREAK
//                   || control_code->code == PARAGRAPH_BREAK)
//                break;                                    // stop at the end of the paragraph
//            // all other control codes we'll pick up later
//
//        } else if (_flow._input_stream[input_index]->Type() == TEXT_SOURCE) {
//            Layout::InputStreamTextSource *text_source = static_cast<Layout::InputStreamTextSource *>(_flow._input_stream[input_index]);
//            Direction this_block_progression = text_source->styleGetBlockProgression();
//            if (this_block_progression != prev_block_progression) {
//                if (prev_block_progression != _block_progression) {
//                    // need to back up so that control codes belong outside the block-progression change
//                    int run_end_input_index = input_index - 1;
//                    while (run_end_input_index > run_start_input_index
//                           && _flow._input_stream[run_end_input_index]->Type() != TEXT_SOURCE)
//                        run_end_input_index--;
//                    // now create the sub-flow
//                    input_item.sub_flow = new Layout;
//                    for (int sub_input_index = run_start_input_index ; sub_input_index <= run_end_input_index ; sub_input_index++) {
//                        input_item.in_sub_flow = true;
//                        if (_flow._input_stream[sub_input_index]->Type() == CONTROL_CODE) {
//                            Layout::InputStreamControlCode const *control_code = static_cast<Layout::InputStreamControlCode const *>(_flow._input_stream[sub_input_index]);
//                            input_item.sub_flow->appendControlCode(control_code->code, control_code->source, control_code->width, control_code->ascent, control_code->descent);
//                        } else if (_flow._input_stream[sub_input_index]->Type() == TEXT_SOURCE) {
//                            Layout::InputStreamTextSource *text_source = static_cast<Layout::InputStreamTextSource *>(_flow._input_stream[sub_input_index]);
//                            input_item.sub_flow->appendText(*text_source->text, text_source->style, text_source->source, NULL, 0, text_source->text_begin, text_source->text_end);
//                            Layout::InputStreamTextSource *sub_flow_text_source = static_cast<Layout::InputStreamTextSource *>(input_item.sub_flow->_input_stream.back());
//                            sub_flow_text_source->x = text_source->x;    // this is easier than going via optionalattrs for the appendText() call
//                            sub_flow_text_source->y = text_source->y;    // should these actually be allowed anyway? You'll almost never get the results you expect
//                            sub_flow_text_source->dx = text_source->dx;  // (not that it's very clear what you should expect, anyway)
//                            sub_flow_text_source->dy = text_source->dy;
//                            sub_flow_text_source->rotate = text_source->rotate;
//                        }
//                    }
//                    input_item.sub_flow->calculateFlow();
//                }
//                run_start_input_index = input_index;
//            }
//            prev_block_progression = this_block_progression;
//        }
//        para->input_items.push_back(input_item);
//    }
//}

/**
 * Take all the text from \a _para.first_input_index to the end of the
 * paragraph and stitch it together so that pango_itemize() can be called on
 * the whole thing.
 *
 * Input: para.first_input_index.
 * Output: para.direction, para.pango_items, para.char_attributes.
 * Returns: the number of spans created by pango_itemize
 */
void  Layout::Calculator::_buildPangoItemizationForPara(ParagraphInfo *para) const
{
    TRACE(("pango version string: %s\n", pango_version_string() ));
    TRACE((" ... compiled for font features\n"));

    TRACE(("itemizing para, first input %d\n", para->first_input_index));

    PangoAttrList *attributes_list = pango_attr_list_new();
    for (unsigned input_index = para->first_input_index ; input_index < _flow._input_stream.size() ; input_index++) {
        if (_flow._input_stream[input_index]->Type() == CONTROL_CODE) {
            Layout::InputStreamControlCode const *control_code = static_cast<Layout::InputStreamControlCode const *>(_flow._input_stream[input_index]);
            if (   control_code->code == SHAPE_BREAK
                   || control_code->code == PARAGRAPH_BREAK)
                break;                                    // stop at the end of the paragraph
            // all other control codes we'll pick up later

        } else if (_flow._input_stream[input_index]->Type() == TEXT_SOURCE) {
            Layout::InputStreamTextSource *text_source = static_cast<Layout::InputStreamTextSource *>(_flow._input_stream[input_index]);

            // create the FontInstance
            auto font = text_source->styleGetFontInstance();
            if (!font) {
                continue;  // bad news: we'll have to ignore all this text because we know of no font to render it
            }

            PangoAttribute *attribute_font_description = pango_attr_font_desc_new(font->get_descr());
            attribute_font_description->start_index = para->text.bytes();

            PangoAttribute *attribute_font_features =
                pango_attr_font_features_new( text_source->style->getFontFeatureString().c_str());
            attribute_font_features->start_index = para->text.bytes();
            para->text.append(&*text_source->text_begin.base(), text_source->text_length);     // build the combined text

            attribute_font_description->end_index = para->text.bytes();
            pango_attr_list_insert(attributes_list, attribute_font_description);

            attribute_font_features->end_index = para->text.bytes();
            pango_attr_list_insert(attributes_list, attribute_font_features);

            // Set language
            SPObject * object = text_source->source;
            if (!object->lang.empty()) {
                PangoLanguage* language = pango_language_from_string(object->lang.c_str());
                PangoAttribute *attribute_language = pango_attr_language_new( language );
                pango_attr_list_insert(attributes_list, attribute_language);
            }
        }
    }

    TRACE(("whole para: \"%s\"\n", para->text.data()));
//    TRACE(("%d input sources used\n", input_index - para->first_input_index));

    // Pango Itemize
    GList *pango_items_glist = nullptr;
    para->direction = LEFT_TO_RIGHT; // CSS default
    if (_flow._input_stream[para->first_input_index]->Type() == TEXT_SOURCE) {
        Layout::InputStreamTextSource const *text_source = static_cast<Layout::InputStreamTextSource *>(_flow._input_stream[para->first_input_index]);

        para->direction =                (text_source->style->direction.computed == SP_CSS_DIRECTION_LTR) ? LEFT_TO_RIGHT : RIGHT_TO_LEFT;
        PangoDirection pango_direction = (text_source->style->direction.computed == SP_CSS_DIRECTION_LTR) ? PANGO_DIRECTION_LTR : PANGO_DIRECTION_RTL;
        pango_items_glist = pango_itemize_with_base_dir(_pango_context, pango_direction, para->text.data(), 0, para->text.bytes(), attributes_list, nullptr);
    }

    if( pango_items_glist == nullptr ) {
        // Type wasn't TEXT_SOURCE or direction was not set.
        pango_items_glist = pango_itemize(_pango_context, para->text.data(), 0, para->text.bytes(), attributes_list, nullptr);
    }

    pango_attr_list_unref(attributes_list);

    // convert the GList to our vector<> and make the FontInstance for each PangoItem at the same time
    para->pango_items.reserve(g_list_length(pango_items_glist));
    TRACE(("para itemizes to %d sections\n", g_list_length(pango_items_glist)));
    for (GList *current_pango_item = pango_items_glist ; current_pango_item != nullptr ; current_pango_item = current_pango_item->next) {
        PangoItemInfo new_item;
        new_item.item = (PangoItem*)current_pango_item->data;
        PangoFontDescription *font_description = pango_font_describe(new_item.item->analysis.font);
        new_item.font = FontFactory::get().Face(font_description);
        pango_font_description_free(font_description);   // Face() makes a copy
        para->pango_items.push_back(new_item);
    }
    g_list_free(pango_items_glist);

    // and get the character attributes on everything
    para->char_attributes.resize(para->text.length() + 1);
    pango_get_log_attrs(para->text.data(), para->text.bytes(), -1, nullptr, &*para->char_attributes.begin(), para->char_attributes.size());

    // Fix for Pango 1.49 which changes the end of a paragraph to a mandatory break.
    // This breaks Inkscape's multiline text (i.e. sodipodi:role line).
    para->char_attributes[para->text.length()].is_mandatory_break = 0;

    TRACE(("end para itemize, direction = %d\n", para->direction));
}

/**
 * Finds the value of line_height_multiplier given the 'line-height' property. The result of
 * multiplying \a l by \a line_height_multiplier is the inline box height as specified in css2
 * section 10.8.  http://www.w3.org/TR/CSS2/visudet.html#line-height
 *
 * The 'computed' value of 'line-height' does not have a consistent meaning. We need to find the
 * 'used' value and divide that by the font size.
 */
double Layout::Calculator::_computeFontLineHeight( SPStyle const *style )
{
    // This is a bit backwards... we should be returning the absolute height
    // but as the code expects line_height_multiplier we return that.
    if (style->line_height.normal) {
        return (LINE_HEIGHT_NORMAL);
    } else if (style->line_height.unit == SP_CSS_UNIT_NONE) {
        // Special case per CSS, computed value is multiplier
        return style->line_height.computed;
    } else {
        // Normal case, computed value is absolute height. Turn it into multiplier.
        return style->line_height.computed / style->font_size.computed;
    }
}

bool compareGlyphWidth(const PangoGlyphInfo &a, const PangoGlyphInfo &b)
{
    bool retval = false;
    if ( b.geometry.width == 0 && (a.geometry.width > 0))retval = true;
    return (retval);
}


/**
 * Split the paragraph into spans. Also call pango_shape() on them.
 *
 * Input: para->first_input_index, para->pango_items
 * Output: para->spans
 * Returns: the index of the beginning of the following paragraph in _flow._input_stream
 */
unsigned Layout::Calculator::_buildSpansForPara(ParagraphInfo *para) const
{
    unsigned pango_item_index = 0;
    unsigned char_index_in_para = 0;
    unsigned byte_index_in_para = 0;
    unsigned input_index;
    unsigned para_text_index = 0;

    TRACE(("build spans\n"));
    para->free_sequence(para->unbroken_spans);

    for(input_index = para->first_input_index ; input_index < _flow._input_stream.size() ; input_index++) {
        if (_flow._input_stream[input_index]->Type() == CONTROL_CODE) {
            Layout::InputStreamControlCode const *control_code = static_cast<Layout::InputStreamControlCode const *>(_flow._input_stream[input_index]);

            if (   control_code->code == SHAPE_BREAK
                   || control_code->code == PARAGRAPH_BREAK) {

                // Add span to be used to calculate line spacing of blank lines.
                UnbrokenSpan new_span;
                new_span.pango_item_index    = -1;
                new_span.input_index         = input_index;

                // No pango object, so find font and line height ourselves.
                SPObject * object = control_code->source;
                if (object) {
                    SPStyle * style = object->style;
                    // This is a workaround for Inkscape 0.92 SVG1.2 flowed text output, it is technically
                    // incorrect to ignore the style of an empty paragraph, but so many legacy documents
                    // depend on this functionality that fixing it causes real issues.
                    if (is<SPFlowpara>(object)) {
                        style = object->parent->style;
                    }
                    if (style) {
                        new_span.font_size = style->font_size.computed * _flow.getTextLengthMultiplierDue();
                        auto font = FontFactory::get().FaceFromStyle(style);
                        new_span.line_height_multiplier = _computeFontLineHeight(style);
                        new_span.line_height.set(font.get());
                        new_span.line_height *= new_span.font_size;
                    }
                }
                new_span.text_bytes          = 0;
                new_span.char_index_in_para  = char_index_in_para;
                para->unbroken_spans.push_back(new_span);
                TRACE(("add empty span for break %lu\n", para->unbroken_spans.size() - 1));
                break;                                    // stop at the end of the paragraph

            } else if (control_code->code == ARBITRARY_GAP) { // Not used!

                UnbrokenSpan new_span;
                new_span.pango_item_index    = -1;
                new_span.input_index         = input_index;
                new_span.line_height.ascent  = control_code->ascent * _flow.getTextLengthMultiplierDue();
                new_span.line_height.descent = control_code->descent * _flow.getTextLengthMultiplierDue();
                new_span.text_bytes          = 0;
                new_span.char_index_in_para  = char_index_in_para;
                para->unbroken_spans.push_back(new_span);
                TRACE(("add gap span %lu\n", para->unbroken_spans.size() - 1));
            }
        } else if (_flow._input_stream[input_index]->Type() == TEXT_SOURCE && pango_item_index < para->pango_items.size()) {
            Layout::InputStreamTextSource const *text_source = static_cast<Layout::InputStreamTextSource const *>(_flow._input_stream[input_index]);
            unsigned char_index_in_source = 0;
            unsigned span_start_byte_in_source = 0;

            // we'll need to make several spans from each text source, based on the rules described about the UnbrokenSpan definition
            for ( ; ; ) {
                /* we need to change spans at every change of PangoItem, source stream change,
                   or change in one of the attributes altering position/rotation. */

                unsigned const pango_item_bytes = ( pango_item_index >= para->pango_items.size()
                                                    ? 0
                                                    : ( para->pango_items[pango_item_index].item->offset
                                                        + para->pango_items[pango_item_index].item->length
                                                        - byte_index_in_para ) );
                unsigned const text_source_bytes = ( text_source->text_end.base()
                                                     - text_source->text_begin.base()
                                                     - span_start_byte_in_source );
                TRACE(("New Unbroken Span\n"));
                UnbrokenSpan new_span;
                new_span.text_bytes = std::min(text_source_bytes, pango_item_bytes);
                new_span.input_stream_first_character = Glib::ustring::const_iterator(text_source->text_begin.base() + span_start_byte_in_source);
                new_span.char_index_in_para = char_index_in_para + char_index_in_source;
                new_span.input_index = input_index;

                // cut at <tspan> attribute changes as well
                new_span.x._set = false;
                new_span.y._set = false;
                new_span.dx._set = false;
                new_span.dy._set = false;
                new_span.rotate._set = false;
                if (_block_progression == TOP_TO_BOTTOM || _block_progression == BOTTOM_TO_TOP) {
                    // Horizontal text
                    if (text_source->x.size()  > char_index_in_source) new_span.x  = text_source->x[char_index_in_source];
                    if (text_source->y.size()  > char_index_in_source) new_span.y  = text_source->y[char_index_in_source];
                    if (text_source->dx.size() > char_index_in_source) new_span.dx = text_source->dx[char_index_in_source].computed * _flow.getTextLengthMultiplierDue();
                    if (text_source->dy.size() > char_index_in_source) new_span.dy = text_source->dy[char_index_in_source].computed * _flow.getTextLengthMultiplierDue();
                } else {
                    // Vertical text
                    if (text_source->x.size()  > char_index_in_source) new_span.y  = text_source->x[char_index_in_source];
                    if (text_source->y.size()  > char_index_in_source) new_span.x  = text_source->y[char_index_in_source];
                    if (text_source->dx.size() > char_index_in_source) new_span.dy = text_source->dx[char_index_in_source].computed * _flow.getTextLengthMultiplierDue();
                    if (text_source->dy.size() > char_index_in_source) new_span.dx = text_source->dy[char_index_in_source].computed * _flow.getTextLengthMultiplierDue();
                }
                if (text_source->rotate.size() > char_index_in_source) new_span.rotate = text_source->rotate[char_index_in_source];
                else if (char_index_in_source == 0) new_span.rotate = 0.f;
                if (input_index == 0 && para->unbroken_spans.empty() && !new_span.y._set && _flow._input_wrap_shapes.empty()) {
                    // if we don't set an explicit y some of the automatic wrapping code takes over and moves the text vertically
                    // so that the top of the letters is at zero, not the baseline
                    new_span.y = 0.0;
                }
                Glib::ustring::const_iterator iter_text = new_span.input_stream_first_character;
                iter_text++;
                for (unsigned i = char_index_in_source + 1 ; ; i++, iter_text++) {
                    if (iter_text >= text_source->text_end) break;
                    if (iter_text.base() - new_span.input_stream_first_character.base() >= (int)new_span.text_bytes) break;
                    if (   i >= text_source->x.size() && i >= text_source->y.size()
                        && i >= text_source->dx.size() && i >= text_source->dy.size()
                        && i >= text_source->rotate.size()) break;
                    if (   (text_source->x.size()  > i && text_source->x[i]._set)
                        || (text_source->y.size()  > i && text_source->y[i]._set)
                        || (text_source->dx.size() > i && text_source->dx[i]._set && text_source->dx[i].computed != 0.0)
                        || (text_source->dy.size() > i && text_source->dy[i]._set && text_source->dy[i].computed != 0.0)
                        || (text_source->rotate.size() > i && text_source->rotate[i]._set
                            && (i == 0 || text_source->rotate[i].computed != text_source->rotate[i - 1].computed))) {
                        new_span.text_bytes = iter_text.base() - new_span.input_stream_first_character.base();
                        break;
                    }
                }

                // now we know the length, do some final calculations and add the UnbrokenSpan to the list
                new_span.font_size = text_source->style->font_size.computed * _flow.getTextLengthMultiplierDue();
                if (new_span.text_bytes) {
                    new_span.glyph_string = pango_glyph_string_new();
                    /* Some assertions intended to help diagnose bug #1277746. */
                    g_assert( 0 < new_span.text_bytes );
                    g_assert( span_start_byte_in_source < text_source->text->bytes() );
                    g_assert( span_start_byte_in_source + new_span.text_bytes <= text_source->text->bytes() );
                    g_assert( memchr(text_source->text->data() + span_start_byte_in_source, '\0', static_cast<size_t>(new_span.text_bytes))
                              == nullptr );

                    /* Notes as of 4/29/13.  Pango_shape is not generating English language ligatures, but it is generating
                    them for Hebrew (and probably other similar languages).  In the case observed 3 unicode characters (a base
                    and 2 Mark, nonspacings) are merged into two glyphs (the base + first Mn, the 2nd Mn).  All of these map
                    from glyph to first character of the log_cluster range.  This destroys the 1:1 correspondence between
                    characters and glyphs.  A big chunk of the conditional code which immediately follows this call
                    is there to clean up the resulting mess.
                    */

                    // Assumption: old and new arguments are the same.
                    auto gold = std::string_view(text_source->text->data() + span_start_byte_in_source, new_span.text_bytes);
                    auto gnew = std::string_view(para->text.data()         + para_text_index,           new_span.text_bytes);
                    assert (gold == gnew);

                    // Convert characters to glyphs
                    pango_shape_full(para->text.data() + para_text_index,
                                     new_span.text_bytes,
                                     para->text.data(),
                                     -1,
                                     &para->pango_items[pango_item_index].item->analysis,
                                     new_span.glyph_string);

                    if (para->pango_items[pango_item_index].item->analysis.level & 1) {
                        // Right to left text (Arabic, Hebrew, etc.)

                        // pango_shape() will reorder glyphs in rtl sections into visual order
                        // (start offsets in accending order) which messes us up because the svg
                        // spec requires us to draw glyphs in logical order so let's reverse the
                        // glyphstring.

                        const unsigned nglyphs = new_span.glyph_string->num_glyphs;
                        std::vector<PangoGlyphInfo> infos(nglyphs);
                        std::vector<gint>           clusters(nglyphs);

                        for (int i = 0; i < nglyphs; ++i) {
                            std::copy(&new_span.glyph_string->glyphs[i],       &new_span.glyph_string->glyphs[i+1],       infos.end() - i - 1);
                            std::copy(&new_span.glyph_string->log_clusters[i], &new_span.glyph_string->log_clusters[i+1], clusters.end() - i - 1);
                        }

                        std::copy(infos.begin(), infos.end(), new_span.glyph_string->glyphs);
                        std::copy(clusters.begin(), clusters.end(), new_span.glyph_string->log_clusters);

                        // We've messed up the flag that tells a glyph it is first in a cluster.
                        for (int i = 0; i < nglyphs; ++i) {

                            // Set flag for start of cluster, we skip all other glyphs in cluster below.
                            new_span.glyph_string->glyphs[i].attr.is_cluster_start = 1;

                            // Find index of first glyph in next cluster
                            int j = i + 1;
                            while( (j < nglyphs) &&
                                   (new_span.glyph_string->log_clusters[j] == new_span.glyph_string->log_clusters[i])
                                ) {
                                new_span.glyph_string->glyphs[j].attr.is_cluster_start = 0; // Zero
                                j++;
                            }

                            // Move on to next cluster.
                            i = j;
                        }

                    } // End right to left text.

                    //  The following sorting doesn't seem to be necessary, and causes
                    //  https://gitlab.com/inkscape/inkscape/-/issues/394 ...

                    /*
                        CAREFUL, within a log_cluster the order of glyphs may not map 1:1, or
                        even in the same order, to the original unicode characters!!!  Among
                        other things, diacritical mark glyphs can end up sequentially in front of the base
                        character glyph.  That makes determining kerning, even approximately, difficult
                        later on.

                        To resolve this to the extent possible sort the glyphs within the same
                        log_cluster into descending order by width in a special manner before copying.  Diacritical marks
                        and similar have zero width and the glyph they modify has nonzero width.  The order
                        of the zero width ones does not matter.  A logical cluster is sorted into sequential order
                           [base] [zw_modifier1] [zw_modifier2]
                        where all the modifiers have zero width and the base does not. This works for languages like Hebrew.

                        Pango also creates log clusters for languages like Telugu having many glyphs with nonzero widths.
                        Since these are nonzero, their order is not modified.

                        If some language mixes these modes, having a log cluster having something like
                           [base1] [zw_modifier1] [base2] [zw_modifier2]
                        the result will be incorrect:
                           base1] [base2] [zw_modifier1] [zw_modifier2]

                           If ligatures other than with Mark, nonspacing are ever implemented in Pango this will screw up, for instance
                        changing "fi" to "if".
                    */

                    // If it is necessary to move zero width glyphs.. then it applies to both right-to-left and left-to-right text.
                    // const unsigned nglyphs = new_span.glyph_string->num_glyphs;
                    // for (int i = 0; i < nglyphs; ++i) {

                    //     // Zero flag for start of cluster, we zero the rest below, and then reset it after sorting.
                    //     new_span.glyph_string->glyphs[i].attr.is_cluster_start = 0;

                    //     // Find index of first glyph in next cluster
                    //     int j = i + 1;
                    //     while( (j < nglyphs) &&
                    //            (new_span.glyph_string->log_clusters[j] == new_span.glyph_string->log_clusters[i])
                    //         ) {
                    //         new_span.glyph_string->glyphs[j].attr.is_cluster_start = 0; // Zero
                    //         j++;
                    //     }

                    //     if (j - i) {
                    //         // More than one glyph in cluster -> sort.
                    //         std::sort(&(new_span.glyph_string->glyphs[i]), &(new_span.glyph_string->glyphs[j]), compareGlyphWidth);
                    //     }

                    //     // Now we're sorted, set flag for start of cluster.
                    //     new_span.glyph_string->glyphs[i].attr.is_cluster_start = 1;

                    //     // Move on to next cluster.
                    //     i = j;
                    // }
                    /* glyphs[].x_offset values are probably out of order within any log_clusters, apparently harmless */


                    new_span.pango_item_index = pango_item_index;
                    new_span.line_height_multiplier = _computeFontLineHeight(text_source->style);
                    new_span.line_height.set(para->pango_items[pango_item_index].font.get());
                    new_span.line_height *= new_span.font_size;

                    // At some point we may want to calculate baseline_shift here (to take advantage
                    // of otm features like superscript baseline), but for now we use style baseline_shift.
                    new_span.baseline_shift = text_source->style->baseline_shift.computed;
                    new_span.text_orientation = (SPCSSTextOrientation)text_source->style->text_orientation.computed;

                    // TODO: metrics for vertical text
                    TRACE(("add text span %lu \"%s\"\n", para->unbroken_spans.size(), text_source->text->raw().substr(span_start_byte_in_source, new_span.text_bytes).c_str()));
                    TRACE(("  %d glyphs\n", new_span.glyph_string->num_glyphs));
                } else {
                    // if there's no text we still need to initialise the styles
                    new_span.pango_item_index = -1;
                    auto font = text_source->styleGetFontInstance();
                    if (font) {
                        new_span.line_height_multiplier = _computeFontLineHeight( text_source->style );
                        new_span.line_height.set(font.get());
                        new_span.line_height *= new_span.font_size;
                    } else {
                        new_span.line_height *= 0.0;  // Set all to zero
                        new_span.line_height_multiplier = LINE_HEIGHT_NORMAL;
                    }
                    TRACE(("add style init span %lu\n", para->unbroken_spans.size()));
                }
                para->unbroken_spans.push_back(new_span);

                // calculations for moving to the next UnbrokenSpan
                byte_index_in_para += new_span.text_bytes;
                para_text_index += new_span.text_bytes;
                char_index_in_source += g_utf8_strlen(&*new_span.input_stream_first_character.base(), new_span.text_bytes);

                if (new_span.text_bytes >= pango_item_bytes) {   // end of pango item
                    pango_item_index++;
                    if (pango_item_index == para->pango_items.size()) break;  // end of paragraph
                }
                if (new_span.text_bytes == text_source_bytes)
                    break;    // end of source
                // else <tspan> attribute changed
                span_start_byte_in_source += new_span.text_bytes;
            }
            char_index_in_para += char_index_in_source; // This seems wrong. Probably should be inside loop.
        }
    }
    TRACE(("end build spans\n"));
    return input_index;
}

/**
 * Moves onto next shape with a new scanline_maker.
 * If there is no next shape, creates an infinite scanline maker to stash remaining text.
 * Returns false if an infinite scanline maker is created.
 */
bool Layout::Calculator::_goToNextWrapShape()
{
    if (_flow._input_wrap_shapes.size() == 0) {
        // Shouldn't happen.
        std::cerr << "Layout::Calculator::_goToNextWrapShape() called for text without shapes!" << std::endl;
        return false;
    }

    if (_current_shape_index >= _flow._input_wrap_shapes.size()) {
        // Shouldn't happen.
        std::cerr << "Layout::Calculator::_goToNextWrapShape(): shape index too large!" << std::endl;
    }

    _current_shape_index++;

    delete _scanline_maker;
    _scanline_maker = nullptr;

    if (_current_shape_index < _flow._input_wrap_shapes.size()) {
        _scanline_maker =
            new ShapeScanlineMaker(_flow._input_wrap_shapes[_current_shape_index].shape.get(), _block_progression);
        TRACE(("begin wrap shape %u\n", _current_shape_index));
        return true;
    } else {
        // Out of shapes, create infinite scanline maker to stash overflow.

        // First find a suitable position for overflow text.  (index - 1 exists since we just incremented index)
        double x = _flow._input_wrap_shapes[_current_shape_index - 1].shape->leftX;
        double y = _flow._input_wrap_shapes[_current_shape_index - 1].shape->bottomY;

        _scanline_maker = new InfiniteScanlineMaker(x, y, _block_progression);
        TRACE(("out of wrap shapes, stash leftover\n"));
        return false;
    }

    // Shouldn't reach
}

/**
 * Given \a para filled in and \a start_span_pos set, keeps trying to
 * find somewhere it can fit the next line of text. The process of finding
 * the text that fits will involve creating one or more entries in
 * \a chunk_info describing the bounds of the fitted text and several
 * bits of information that will prove useful when we come to output the
 * line to #_flow. Returns with \a start_span_pos set to the end of the
 * text that was fitted, \a chunk_info completely filled out and
 * \a line_box_height set with the largest ascent and the largest
 * descent (individually per CSS) on the line. The line_box_height
 * can never be smaller than the line_box_strut (which is determined
 * by the block level value of line_height). The return
 * value is false only if we've run out of shapes to wrap inside (and
 * hence stashed overflow).
 */
bool Layout::Calculator::_findChunksForLine(ParagraphInfo const &para,
                                            UnbrokenSpanPosition *start_span_pos,
                                            std::vector<ChunkInfo> *chunk_info,
                                            FontMetrics *line_box_height,
                                            FontMetrics const *strut_height)
{
    TRACE(("  begin _findChunksForLine: chunks: %lu, em size: %f\n", chunk_info->size(), line_box_height->emSize() ));

    // CSS 2.1 dictates that the minimum line height (i.e. the strut height)
    // is found from the block element.
    *line_box_height = *strut_height;
    TRACE(("    initial line_box_height (em size): %f\n", line_box_height->emSize() ));

    bool truncated = false;

    UnbrokenSpanPosition span_pos;
    for( ; ; ) {
        // Get regions where one can place one line of text (can be more than one, if filling a
        // donut for example).
        std::vector<ScanlineMaker::ScanRun> scan_runs;
        scan_runs = _scanline_maker->makeScanline(*line_box_height); // 1 scan run with "InfiniteScanlineMaker"

        // If scan_runs is empty, we must have reached the bottom of a shape. Go to next shape.
        while (scan_runs.empty()) {
            // Reset for new shape.
            *line_box_height = *strut_height;

            // Only used by ShapeScanlineMaker
            if (!_goToNextWrapShape()) {
                truncated = true;
            }

            // If we've run out of shapes, this will be the infinite line scanline maker with one scan_run).
            scan_runs = _scanline_maker->makeScanline(*line_box_height);
        }


        TRACE(("    finding line fit y=%f, %lu scan runs\n", scan_runs.front().y, scan_runs.size()));
        chunk_info->clear();
        chunk_info->reserve(scan_runs.size());
        if (para.direction == RIGHT_TO_LEFT) std::reverse(scan_runs.begin(), scan_runs.end());
        unsigned scan_run_index;
        span_pos = *start_span_pos;
        for (scan_run_index = 0 ; scan_run_index < scan_runs.size() ; scan_run_index++) {
            // Returns false if some text in line requires a taller line_box_height.
            // (We try again with a larger line_box_height.)
            if (!_buildChunksInScanRun(para, span_pos, scan_runs[scan_run_index], chunk_info, line_box_height)) {
                break;
            }

            if (!chunk_info->empty() && !chunk_info->back().broken_spans.empty()) {
                span_pos = chunk_info->back().broken_spans.back().end;
            }
        }

        if (scan_run_index == scan_runs.size()) break;  // ie when buildChunksInScanRun() succeeded

    } // End for loop

    *start_span_pos = span_pos;
    TRACE(("    final line_box_height: %f\n", line_box_height->emSize() ));
    TRACE(("  end _findChunksForLine: chunks: %lu, truncated: %s\n", chunk_info->size(), truncated ? "true" : "false"));
    return !truncated;
}

/**
 * Given a scan run and a first character, append one or more chunks to
 * the \a chunk_info vector that describe all the spans and other detail
 * necessary to output the greatest amount of text that will fit on this scan
 * line (greedy line breaking algorithm). Each chunk contains one or more
 * BrokenSpan structures that link back to UnbrokenSpan structures that link
 * to the text itself. Normally there will be either one or zero (if the
 * scanrun is too short to fit any text) chunk added to \a chunk_info by
 * each call to this method, but we will add more than one if an x or y
 * attribute has been set on a tspan. \a line_height must be set on input,
 * and if it needs to be made larger and the #_scanline_maker can't do
 * an in-situ resize then it will be set to the required value and the
 * method will return false.
 */
bool Layout::Calculator::_buildChunksInScanRun(ParagraphInfo const &para,
                                               UnbrokenSpanPosition const &start_span_pos,
                                               ScanlineMaker::ScanRun const &scan_run,
                                               std::vector<ChunkInfo> *chunk_info,
                                               FontMetrics *line_height) const
{
    TRACE(("    begin _buildChunksInScanRun: chunks: %lu, em size: %f\n", chunk_info->size(), line_height->emSize() ));

    FontMetrics line_height_saved = *line_height; // Store for recalculating line height if chunks are backed out

    ChunkInfo new_chunk;
    new_chunk.text_width = 0.0;
    new_chunk.whitespace_count = 0;
    new_chunk.scanrun_width = scan_run.width();
    new_chunk.x = scan_run.x_start;

    // we haven't done anything yet so the last valid break position is the beginning
    BrokenSpan last_span_at_break, last_span_at_emergency_break;
    last_span_at_break.start = start_span_pos;
    last_span_at_break.setZero();
    last_span_at_emergency_break.start = start_span_pos;
    last_span_at_emergency_break.setZero();

    TRACE(("      trying chunk from %f to %g\n", scan_run.x_start, scan_run.x_end));
    BrokenSpan new_span;
    new_span.end = start_span_pos;
    while (new_span.end.iter_span != para.unbroken_spans.end()) {    // this loops once for each UnbrokenSpan
        new_span.start = new_span.end;

        // force a chunk change at x or y attribute change
        if ((new_span.start.iter_span->x._set || new_span.start.iter_span->y._set) && new_span.start.char_byte == 0) {

            if (new_span.start.iter_span != start_span_pos.iter_span)
                chunk_info->push_back(new_chunk);

            new_chunk.x += new_chunk.text_width;
            new_chunk.text_width = 0.0;
            new_chunk.whitespace_count = 0;
            new_chunk.broken_spans.clear();
            if (new_span.start.iter_span->x._set) new_chunk.x = new_span.start.iter_span->x.computed;
            // y doesn't need to be done until output time
        }

        // see if this span is too tall to fit on the current line
        FontMetrics new_span_height = new_span.start.iter_span->line_height;
        new_span_height.computeEffective( new_span.start.iter_span->line_height_multiplier );

        /* floating point 80-bit/64-bit rounding problems require epsilon. See
           discussion http://inkscape.gristle.org/2005-03-16.txt around 22:00 */
        if ( new_span_height.ascent  > line_height->ascent  + std::numeric_limits<float>::epsilon() ||
             new_span_height.descent > line_height->descent + std::numeric_limits<float>::epsilon() ) {
            // Take larger of each of the two ascents and two descents per CSS
            line_height->max(new_span_height);

            // Currently always true for flowed text and false for Inkscape multiline text.
            if (!_scanline_maker->canExtendCurrentScanline(*line_height)) {
                return false;
            }
        }

        bool span_fitted = _measureUnbrokenSpan(para, &new_span, &last_span_at_break, &last_span_at_emergency_break, new_chunk.scanrun_width - new_chunk.text_width);

        new_chunk.text_width += new_span.width;
        new_chunk.whitespace_count += new_span.whitespace_count;
        new_chunk.broken_spans.push_back(new_span);   // if !span_fitted we'll correct ourselves below

        if (!span_fitted) break;

        if (new_span.end.iter_span == para.unbroken_spans.end()) {
            last_span_at_break = new_span;
            break;
        }

        PangoLogAttr const &char_attributes = _charAttributes(para, new_span.end);
        if (char_attributes.is_mandatory_break) {
            last_span_at_break = new_span;
            break;
        }
    }

    TRACE(("      chunk complete, used %f width (%d whitespaces, %lu brokenspans)\n", new_chunk.text_width, new_chunk.whitespace_count, new_chunk.broken_spans.size()));
    chunk_info->push_back(new_chunk);

    if (scan_run.width() >= 4.0 * line_height->emSize() && last_span_at_break.end == start_span_pos) {
        /* **non-SVG spec bit**: See bug #1191102
           If the user types a very long line with no spaces, the way the spec
           is written at the moment means that when the length of the text
           exceeds the available width of all remaining areas, the text is
           completely hidden. This condition alters that behaviour so that if
           the length of the line is greater than four times the line-height
           and there are no spaces, it'll be emergency-wrapped at the last
           character. One could read the SVG Tiny 1.2 draft as permitting this
           sort of behaviour, but it's still a bit dodgy. The hard-coding of
           4x is not nice, either. */
        last_span_at_break = last_span_at_emergency_break;
    }

    if (!chunk_info->back().broken_spans.empty() && last_span_at_break.end != chunk_info->back().broken_spans.back().end) {
        // need to back out spans until we come to the one with the last break in it
        while (!chunk_info->empty() && last_span_at_break.start.iter_span != chunk_info->back().broken_spans.back().start.iter_span) {
            chunk_info->back().text_width -= chunk_info->back().broken_spans.back().width;
            chunk_info->back().whitespace_count -= chunk_info->back().broken_spans.back().whitespace_count;
            chunk_info->back().broken_spans.pop_back();
            if (chunk_info->back().broken_spans.empty())
                chunk_info->pop_back();
        }
        if (!chunk_info->empty()) {
            chunk_info->back().text_width -= chunk_info->back().broken_spans.back().width;
            chunk_info->back().whitespace_count -= chunk_info->back().broken_spans.back().whitespace_count;
            if (last_span_at_break.start == last_span_at_break.end) {
                chunk_info->back().broken_spans.pop_back();   // last break was at an existing boundary
                if (chunk_info->back().broken_spans.empty())
                    chunk_info->pop_back();
            } else {
                chunk_info->back().broken_spans.back() = last_span_at_break;
                chunk_info->back().text_width += last_span_at_break.width;
                chunk_info->back().whitespace_count += last_span_at_break.whitespace_count;
            }
            TRACE(("      correction: fitted span %lu width = %f\n", last_span_at_break.start.iter_span - para.unbroken_spans.begin(), last_span_at_break.width));
        }
    }

    // Recalculate line_box_height after backing out chunks
    *line_height = line_height_saved;
    for (const auto & it_chunk : *chunk_info) {
        for (const auto & broken_span : it_chunk.broken_spans) {
            FontMetrics span_height = broken_span.start.iter_span->line_height;
            TRACE(("      brokenspan line_height: %f\n", span_height.emSize() ));
            span_height.computeEffective( broken_span.start.iter_span->line_height_multiplier );
            line_height->max( span_height );
        }
    }
    TRACE(("      line_box_height: %f\n", line_height->emSize()));

    if (!chunk_info->empty() && !chunk_info->back().broken_spans.empty() && chunk_info->back().broken_spans.back().ends_with_whitespace) {
        // for justification we need to discard space occupied by the single whitespace at the end of the chunk
        TRACE(("      backing out whitespace\n"));
        chunk_info->back().broken_spans.back().ends_with_whitespace = false;
        chunk_info->back().broken_spans.back().width -= chunk_info->back().broken_spans.back().each_whitespace_width;
        chunk_info->back().broken_spans.back().whitespace_count--;
        chunk_info->back().text_width -= chunk_info->back().broken_spans.back().each_whitespace_width;
        chunk_info->back().whitespace_count--;
    }

    if (!chunk_info->empty() && !chunk_info->back().broken_spans.empty() ) {
        // for justification we need to discard line-spacing and word-spacing at end of the chunk
        chunk_info->back().broken_spans.back().width -= chunk_info->back().broken_spans.back().letter_spacing;
        chunk_info->back().text_width -= chunk_info->back().broken_spans.back().letter_spacing;
        TRACE(("      width after subtracting last letter_spacing: %f\n", chunk_info->back().broken_spans.back().width));
    }

    TRACE(("    end _buildChunksInScanRun: chunks: %lu\n", chunk_info->size()));
    return true;
}

#ifdef DEBUG_LAYOUT_TNG_COMPUTE
/**
 * For debugging, not called in distributed code
 *
 * Input: para->first_input_index, para->pango_items
 */
void Layout::Calculator::dumpPangoItemsOut(ParagraphInfo *para){
    std::cerr << "Pango items: " << para->pango_items.size() << std::endl;
    FontFactory &factory = FontFactory::get();
    for(unsigned pidx = 0 ; pidx < para->pango_items.size(); pidx++){
        std::cerr 
        << "idx: " << pidx 
        << " offset: " 
        << para->pango_items[pidx].item->offset
        << " length: "
        << para->pango_items[pidx].item->length
        << " font: "
        << factory.ConstructFontSpecification(para->pango_items[pidx].font.get())
        << std::endl;
    }
}

/**
 * For debugging, not called in distributed code
 *
 * Input: para->first_input_index, para->pango_items
 */
void Layout::Calculator::dumpUnbrokenSpans(ParagraphInfo *para){
    std::cerr << "Unbroken Spans: " << para->unbroken_spans.size() << std::endl;
    for(unsigned uidx = 0 ; uidx < para->unbroken_spans.size(); uidx++){
        std::cerr 
        << "idx: "                 << uidx 
        << " pango_item_index: "   << para->unbroken_spans[uidx].pango_item_index
        << " input_index: "        << para->unbroken_spans[uidx].input_index
        << " char_index_in_para: " << para->unbroken_spans[uidx].char_index_in_para
        << " text_bytes: "         << para->unbroken_spans[uidx].text_bytes
        << std::endl;
    }
}
#endif //DEBUG_LAYOUT_TNG_COMPUTE

/** The management function to start the whole thing off. */
bool Layout::Calculator::calculate()
{
    if (_flow._input_stream.empty())
        return false;
    /**
    * hm, why do we want assert (crash) the application, now do simply return false
    * \todo check if this is the correct behaviour
    * g_assert(_flow._input_stream.front()->Type() == TEXT_SOURCE);
    */
    if (_flow._input_stream.front()->Type() != TEXT_SOURCE)
    {
        g_warning("flow text is not of type TEXT_SOURCE. Abort.");
        return false;
    }
    TRACE(("begin calculate()\n"));

    _flow._clearOutputObjects();

    _pango_context = FontFactory::get().get_font_context();

    _font_factory_size_multiplier = FontFactory::get().fontSize;

    _block_progression = _flow._blockProgression();
    if( _block_progression == RIGHT_TO_LEFT || _block_progression == LEFT_TO_RIGHT ) {
        // Vertical text, CJK
        switch (_flow._blockTextOrientation()) {
            case SP_CSS_TEXT_ORIENTATION_MIXED:
                pango_context_set_base_gravity(_pango_context, PANGO_GRAVITY_EAST);
                pango_context_set_gravity_hint(_pango_context, PANGO_GRAVITY_HINT_NATURAL);
                break;
            case SP_CSS_TEXT_ORIENTATION_UPRIGHT:
                pango_context_set_base_gravity(_pango_context, PANGO_GRAVITY_EAST);
                pango_context_set_gravity_hint(_pango_context, PANGO_GRAVITY_HINT_STRONG);
                break;
            case SP_CSS_TEXT_ORIENTATION_SIDEWAYS:
                pango_context_set_base_gravity(_pango_context, PANGO_GRAVITY_SOUTH);
                pango_context_set_gravity_hint(_pango_context, PANGO_GRAVITY_HINT_STRONG);
                break;
            default:
                std::cerr << "Layout::Calculator: Unhandled text orientation!" << std::endl;
        }
    } else {
        // Horizontal text
        pango_context_set_base_gravity(_pango_context, PANGO_GRAVITY_AUTO);
        pango_context_set_gravity_hint(_pango_context, PANGO_GRAVITY_HINT_NATURAL);
    }

    // Minimum line box height determined by block container.
    FontMetrics strut_height = _flow.strut;
    _y_offset = 0.0;
    _createFirstScanlineMaker();

    ParagraphInfo para;
    FontMetrics line_box_height; // Current value of line box height for line.
    bool keep_going = true; // Set false if we ran out of space and had to stash overflow.
    for(para.first_input_index = 0 ; para.first_input_index < _flow._input_stream.size() ; ) {

        // jump to the next wrap shape if this is a SHAPE_BREAK control code
        if (_flow._input_stream[para.first_input_index]->Type() == CONTROL_CODE) {
            InputStreamControlCode const *control_code = static_cast<InputStreamControlCode const *>(_flow._input_stream[para.first_input_index]);
            if (control_code->code == SHAPE_BREAK) {
                TRACE(("shape break control code\n"));
                if (!_goToNextWrapShape()) {
                    std::cerr << "Layout::Calculator::calculate: Found SHAPE_BREAK but out of shapes!" << std::endl;
                }
                continue; // Go to next paragraph (paragraph only contained control code).
            }
        }

        // Break things up into little pango units with unique direction, gravity, etc.
        _buildPangoItemizationForPara(&para);

        // Do shaping (convert characters to glyphs)
        unsigned para_end_input_index = _buildSpansForPara(&para);

        if (_flow._input_stream[para.first_input_index]->Type() == TEXT_SOURCE)
            para.alignment = static_cast<InputStreamTextSource*>(_flow._input_stream[para.first_input_index])->styleGetAlignment(para.direction, !_flow._input_wrap_shapes.empty());
        else
            para.alignment = para.direction == LEFT_TO_RIGHT ? LEFT : RIGHT;

        TRACE(("para prepared, adding as #%lu\n", _flow._paragraphs.size()));
        Layout::Paragraph new_paragraph;
        new_paragraph.base_direction = para.direction;
        new_paragraph.alignment = para.alignment;
        _flow._paragraphs.push_back(new_paragraph);

        // start scanning lines
        UnbrokenSpanPosition span_pos;
        span_pos.iter_span = para.unbroken_spans.begin();
        span_pos.char_byte = 0;
        span_pos.char_index = 0;

        do {   // Until end of paragraph
            TRACE(("begin line\n"));

            std::vector<ChunkInfo> line_chunk_info;

            // Fill line.
            // If we've run out of space, we've put the remaining text in a single line and
            // returned false. If we ran out of space on previous paragraph, we continue with
            // single-line scan-line maker.
            bool flowed =_findChunksForLine(para, &span_pos, &line_chunk_info, &line_box_height, &strut_height );
            if (!flowed) {
                keep_going = false;
            }

            if (line_box_height.emSize() < 0.001 && line_chunk_info.empty()) {
                // We need to avoid an infinite (or semi-infinite) loop.
                std::cerr << "Layout::Calculator::calculate: No room for text and line advance is very small" << std::endl;
                return false; // For the moment
            }


            // For Inkscape multi-line text (using role="line") we run into a problem if the first
            // line is empty - namely, there is no character to attach a 'y' attribute value. The
            // result is that the code that takes a baseline position (e.g. 'y') and finds the top
            // of the layout box is bypassed resulting in wrongly placed text (we layout the text
            // relative to the top of the box as this is required for text-in-a-shape). We don't
            // know how to find the top of the box from the 'y' position until we have found the
            // line height parameters for the given line (after calling _findChunksForLine() just
            // above).
            if (para.first_input_index == 0 && (_flow.wrap_mode == WRAP_NONE)) {

                // Calculate new top of box... given specified baseline.
                double top_of_line_box = _scanline_maker->yCoordinate(); // Set in constructor.
                if( _block_progression == RIGHT_TO_LEFT ) {
                    // Vertical text, use em box center as baseline
                    top_of_line_box += 0.5 * line_box_height.emSize();
                } else if (_block_progression == LEFT_TO_RIGHT ) {
                    // Vertical text, use em box center as baseline
                    top_of_line_box -= 0.5 * line_box_height.emSize();
                } else {
                    top_of_line_box -= line_box_height.getTypoAscent();
                }
                TRACE(("      y attribute set, next line top_of_line_box: %f\n", top_of_line_box ));
                // Set the initial y coordinate of the for this line (see above).
                _scanline_maker->setNewYCoordinate(top_of_line_box);
            }

            // !keep_going --> truncated --> hidden
            _outputLine(para, line_box_height, line_chunk_info, !keep_going);

            _scanline_maker->setLineHeight( line_box_height );
            _scanline_maker->completeLine(); // Increments y by line height
            TRACE(("end line\n"));
        } while (span_pos.iter_span != para.unbroken_spans.end());

        TRACE(("para %lu end\n\n", _flow._paragraphs.size() - 1));
        if (keep_going) {
            // We have more to do, setup next section.
            bool is_empty_para = _flow._characters.empty() || _flow._characters.back().line(&_flow).in_paragraph != _flow._paragraphs.size() - 1;
            if ((is_empty_para && para_end_input_index + 1 >= _flow._input_stream.size())
                || para_end_input_index + 1 < _flow._input_stream.size()) {
                // we need a span just for the para if it's either an empty last para or a break in the middle
                Layout::Span new_span;
                if (_flow._spans.empty()) {
                    new_span.font = nullptr;
                    new_span.font_size = line_box_height.emSize();
                    new_span.line_height = line_box_height;
                    new_span.x_end = 0.0;
                } else {
                    new_span = _flow._spans.back();
                    if (_flow._chunks[new_span.in_chunk].in_line != _flow._lines.size() - 1)
                        new_span.x_end = 0.0;
                }
                new_span.in_chunk = _flow._chunks.size() - 1;
                new_span.x_start = new_span.x_end;
                new_span.baseline_shift = 0.0;
                new_span.direction = para.direction;
                new_span.block_progression = _block_progression;
                if (para_end_input_index == _flow._input_stream.size())
                    new_span.in_input_stream_item = _flow._input_stream.size() - 1;
                else
                    new_span.in_input_stream_item = para_end_input_index;
                _flow._spans.push_back(new_span);
            }
            if (para_end_input_index + 1 < _flow._input_stream.size()) {
                // we've got to add an invisible character between paragraphs so that we can position iterators
                // (and hence cursors) both before and after the paragraph break
                Layout::Character new_character;
                new_character.the_char = '@';
                new_character.in_span = _flow._spans.size() - 1;
                new_character.char_attributes.is_line_break = 1;
                new_character.char_attributes.is_mandatory_break = 1;
                new_character.char_attributes.is_char_break = 1;
                new_character.char_attributes.is_white = 1;
                new_character.char_attributes.is_cursor_position = 1;
                new_character.char_attributes.is_word_start = 0;
                new_character.char_attributes.is_word_end = 1;
                new_character.char_attributes.is_sentence_start = 0;
                new_character.char_attributes.is_sentence_end = 1;
                new_character.char_attributes.is_sentence_boundary = 1;
                new_character.char_attributes.backspace_deletes_character = 1;
                new_character.x = _flow._spans.back().x_end - _flow._spans.back().x_start;
                new_character.in_glyph = -1;
                _flow._characters.push_back(new_character);
            }
        }
        // dumpPangoItemsOut(&para);
        // dumpUnbrokenSpans(&para);

        para.free();
        para.first_input_index = para_end_input_index + 1;
    } // Loop over paras

    para.free();
    if (_scanline_maker) {
        delete _scanline_maker;
    }

    _flow._input_truncated = !keep_going;

    if (_flow.textLength._set) {
        // Calculate the adjustment needed to meet the textLength
        double actual_length = _flow.getActualLength();
        double difference = _flow.textLength.computed - actual_length;
        _flow.textLengthMultiplier = (actual_length + difference) / actual_length;
        _flow.textLengthIncrement = difference / (_flow._characters.size() == 1? 1 : _flow._characters.size() - 1);
    }

    return true;
}

void Layout::_calculateCursorShapeForEmpty()
{
    _empty_cursor_shape.position = Geom::Point(0, 0);
    _empty_cursor_shape.height = 0.0;
    _empty_cursor_shape.rotation = 0.0;
    if (_input_stream.empty() || _input_stream.front()->Type() != TEXT_SOURCE)
        return;

    auto text_source = static_cast<InputStreamTextSource const *>(_input_stream.front());

    auto font = text_source->styleGetFontInstance();
    double font_size = text_source->style->font_size.computed;
    double caret_slope_run = 0.0, caret_slope_rise = 1.0;
    FontMetrics line_height;
    if (font) {
        font->FontSlope(caret_slope_run, caret_slope_rise);
        font->FontMetrics(line_height.ascent, line_height.descent, line_height.xheight);
        line_height *= font_size;
    }

    double caret_slope = atan2(caret_slope_run, caret_slope_rise);
    _empty_cursor_shape.height = font_size / std::cos(caret_slope);
    _empty_cursor_shape.rotation = caret_slope;

    if (_input_wrap_shapes.empty()) {
        _empty_cursor_shape.position = Geom::Point(text_source->x.empty() || !text_source->x.front()._set ? 0.0 : text_source->x.front().computed,
                                                 text_source->y.empty() || !text_source->y.front()._set ? 0.0 : text_source->y.front().computed);
    } else if (wrap_mode == WRAP_INLINE_SIZE) {
        // 'inline-size' has a wrap shape of an "infinite" rectangle, we need the place where the text should begin.
        double x = 0;
        double y = 0;
        if (!text_source->x.empty())
            x = text_source->x.front().computed;
        if (!text_source->y.empty())
            y = text_source->y.front().computed;
        _empty_cursor_shape.position = Geom::Point(x, y);
    } else {
        Direction block_progression = text_source->styleGetBlockProgression();
        ShapeScanlineMaker scanline_maker(_input_wrap_shapes.front().shape.get(), block_progression);
        std::vector<ScanlineMaker::ScanRun> scan_runs = scanline_maker.makeScanline(line_height);
        if (!scan_runs.empty()) {
            if (block_progression == LEFT_TO_RIGHT || block_progression == RIGHT_TO_LEFT) {
                // Vertical text
                _empty_cursor_shape.position = Geom::Point(scan_runs.front().y + font_size, scan_runs.front().x_start);
            } else {
                // Horizontal text
                _empty_cursor_shape.position = Geom::Point(scan_runs.front().x_start, scan_runs.front().y + font_size);
            }
        }
    }
}

bool Layout::calculateFlow()
{
    TRACE(("begin calculateFlow()\n"));
    Layout::Calculator calc = Calculator(this);
    bool result = calc.calculate();

    if (textLengthIncrement != 0) {
        TRACE(("Recalculating layout the second time to fit textLength!\n"));
        result = calc.calculate();
    }

    if (_characters.empty()) {
        _calculateCursorShapeForEmpty();
    }

    _calculateBaselines();
    return result;
}

}//namespace Text
}//namespace Inkscape


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
