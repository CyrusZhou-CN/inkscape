// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Boolean operations.
 *//*
 * Authors:
 * see git history
 *  Created by fred on Fri Dec 05 2003.
 *  tweaked endlessly by bulia byak <buliabyak@users.sf.net>
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <vector>

#include <glibmm/i18n.h>
#include <2geom/intersection-graph.h>
#include <2geom/svg-path-parser.h> // to get from SVG on boolean to Geom::Path
#include <2geom/utils.h>

#include "path-boolop.h"
#include "path-util.h"

#include "message-stack.h"
#include "path-chemistry.h"     // copy_object_properties()

#include "helper/geom.h"        // pathv_to_linear_and_cubic_beziers()

#include "livarot/Path.h"
#include "livarot/Shape.h"

#include "object/sp-flowtext.h"
#include "object/sp-shape.h"
#include "object/sp-text.h"

#include "display/curve.h"

#include "ui/icon-names.h"

#include "xml/repr-sorting.h"

using Inkscape::DocumentUndo;

/*
 * ObjectSet functions
 */

bool Inkscape::ObjectSet::pathUnion(bool skip_undo, bool silent)
{
    return pathBoolOp(bool_op_union, skip_undo, false, INKSCAPE_ICON("path-union"), _("Union"), silent) == DONE;
}

bool Inkscape::ObjectSet::pathIntersect(bool skip_undo, bool silent)
{
    return pathBoolOp(bool_op_inters, skip_undo, false, INKSCAPE_ICON("path-intersection"), _("Intersection"), silent) == DONE;
}

bool Inkscape::ObjectSet::pathDiff(bool skip_undo, bool silent)
{
    return pathBoolOp(bool_op_diff, skip_undo, false, INKSCAPE_ICON("path-difference"), _("Difference"), silent) == DONE;
}

bool Inkscape::ObjectSet::pathSymDiff(bool skip_undo, bool silent)
{
    return pathBoolOp(bool_op_symdiff, skip_undo, false, INKSCAPE_ICON("path-exclusion"), _("Exclusion"), silent) == DONE;
}

bool Inkscape::ObjectSet::pathCut(bool skip_undo, bool silent)
{
    return pathBoolOp(bool_op_cut, skip_undo, false, INKSCAPE_ICON("path-division"), _("Division"), silent) == DONE;
}

bool Inkscape::ObjectSet::pathSlice(bool skip_undo, bool silent)
{
    return pathBoolOp(bool_op_slice, skip_undo, false, INKSCAPE_ICON("path-cut"), _("Cut path"), silent) == DONE;
}

/*
 * Utilities
 */

/**
 * Return a rough estimate of a pathvector's size, based on its bounding box.
 */
static double diameter(Geom::PathVector const &pathv)
{
    if (auto rect = pathv.boundsExact()) {
        return rect->dimensions().length();
    }

    return 0;
}

/**
 * Estimate a suitable approximation threshold for a pathvector.
 */
static double get_threshold(Geom::PathVector const &pathv)
{
    return diameter(pathv) * 1e-3;
}

/**
 * Create a flattened shape from a path.
 *
 * @param path The path to convert.
 * @param path_id The id to assign to all the edges in the resultant shape.
 * @param fill_rule The fill rule with which to flatten the path.
 * @param close_if_needed If the path is not closed, whether to add a closing segment.
 */
static Shape make_shape(Path &path, int path_id = -1, FillRule fill_rule = fill_nonZero, bool close_if_needed = true)
{
    Shape result;

    Shape tmp;
    path.Fill(&tmp, path_id, false, close_if_needed);
    result.ConvertToShape(&tmp, fill_rule);

    return result;
}

/**
 * Create a path with backdata from a pathvector,
 * automatically estimating a suitable conversion threshold.
 */
static Path make_path(Geom::PathVector const &pathv)
{
    Path result;

    result.LoadPathVector(pathv);
    result.ConvertWithBackData(get_threshold(pathv));

    return result;
}

/**
 * Return whether a path is a single open line segment.
 */
static bool is_line(Path const &path)
{
    return path.pts.size() == 2 && path.pts[0].isMoveTo && !path.pts[1].isMoveTo;
}

/*
 * Flattening
 */

Geom::PathVector flattened(Geom::PathVector const &pathv, FillRule fill_rule)
{
    auto path = make_path(pathv);
    auto shape = make_shape(path, 0, fill_rule);

    Path res;
    shape.ConvertToForme(&res, 1, std::begin({ &path }));

    return res.MakePathVector();
}

void sp_flatten(Geom::PathVector &pathv, FillRule fill_rule)
{
    pathv = flattened(pathv, fill_rule);
}

/*
 * Boolean operations on pathvectors
 */

std::vector<Geom::PathVector> pathvector_cut(Geom::PathVector const &pathv, Geom::PathVector const &lines)
{
    auto patha = make_path(pathv);
    auto pathb = make_path(lines);
    auto shapea = make_shape(patha, 0);
    auto shapeb = make_shape(pathb, 1, fill_justDont, is_line(pathb));

    Shape shape;
    shape.Booleen(&shapeb, &shapea, bool_op_cut, 1);

    Path path;
    int num_nesting = 0;
    int *nesting = nullptr;
    int *conts = nullptr;
    shape.ConvertToFormeNested(&path, 2, std::begin({ &patha, &pathb }), num_nesting, nesting, conts, true);

    int num_paths;
    auto paths = path.SubPathsWithNesting(num_paths, false, num_nesting, nesting, conts);

    std::vector<Geom::PathVector> result;
    result.reserve(num_paths);

    for (int i = 0; i < num_paths; i++) {
        result.emplace_back(paths[i]->MakePathVector());
    }

    g_free(paths);
    g_free(conts);
    g_free(nesting);

    return result;
}

Geom::PathVector sp_pathvector_boolop(Geom::PathVector const &pathva, Geom::PathVector const &pathvb, BooleanOp bop,
                                      FillRule fra, FillRule frb, bool livarotonly, bool flattenbefore)
{
    bool error = false;
    return sp_pathvector_boolop(pathva, pathvb, bop, fra, frb, livarotonly, flattenbefore, error);
}

Geom::PathVector sp_pathvector_boolop(Geom::PathVector const &pathva, Geom::PathVector const &pathvb, BooleanOp bop,
                                      FillRule fra, FillRule frb, bool livarotonly, bool flattenbefore, bool &error)
{
    // Livarot's outline of arcs is broken. So convert the path to linear and cubics only, for which the outline is created correctly.
    auto a = pathv_to_linear_and_cubic_beziers(pathva);
    auto b = pathv_to_linear_and_cubic_beziers(pathvb);

    if (!livarotonly) {
        try {
            if (flattenbefore) {
                sp_flatten(a, fra);
                sp_flatten(b, frb);
            }

            // Don't change tolerance - gives errors on boolops.
            auto pig = Geom::PathIntersectionGraph(a, b);

            switch (bop) {
                case bool_op_inters:
                    return pig.getIntersection();
                case bool_op_union:
                    return pig.getUnion();
                case bool_op_symdiff:
                    return pig.getXOR();
                case bool_op_diff:
                    return pig.getBminusA(); // livarot order...
                case bool_op_cut: {
                    auto out = pig.getBminusA();
                    auto tmp = pig.getIntersection();
                    out.insert(out.end(), tmp.begin(), tmp.end());
                    return out;
                }
                default:
                    g_debug("Path Intersection Graph unsupported operation, fallback to livarot");
                    break;
            }
        } catch (...) {
            g_debug("Path Intersection Graph failed boolops, fallback to livarot");
        }

        error = true;
    }

    auto patha = make_path(a);
    auto pathb = make_path(b);

    Path result;

    if (bop == bool_op_inters || bop == bool_op_union || bop == bool_op_diff || bop == bool_op_symdiff) {
        // true boolean op
        // get the polygons of each path, with the winding rule specified, and apply the operation iteratively
        auto shapea = make_shape(patha, 0, fra);
        auto shapeb = make_shape(pathb, 1, frb);

        Shape shape;
        shape.Booleen(&shapeb, &shapea, bop);

        shape.ConvertToForme(&result, 2, std::begin({ &patha, &pathb }));

    } else if (bop == bool_op_cut) {
        // cuts= sort of a bastard boolean operation, thus not the axact same modus operandi
        // technically, the cut path is not necessarily a polygon (thus has no winding rule)
        // it is just uncrossed, and cleaned from duplicate edges and points
        // then it's fed to Booleen() which will uncross it against the other path
        // then comes the trick: each edge of the cut path is duplicated (one in each direction),
        // thus making a polygon. the weight of the edges of the cut are all 0, but
        // the Booleen need to invert the ones inside the source polygon (for the subsequent
        // ConvertToForme)

        // the cut path needs to have the highest pathID in the back data
        // that's how the Booleen() function knows it's an edge of the cut
        // fill_justDont doesn't compute winding numbers
        // see LP Bug 177956 for why is_line is needed
        auto shapea = make_shape(patha, 1, fill_justDont, is_line(patha));
        auto shapeb = make_shape(pathb, 0, frb);

        Shape shape;
        shape.Booleen(&shapea, &shapeb, bool_op_cut, 1);

        shape.ConvertToForme(&result, 2, std::begin({ &pathb, &patha }), true);

    } else if (bop == bool_op_slice) {
        // slice is not really a boolean operation
        // you just put the 2 shapes in a single polygon, uncross it
        // the points where the degree is > 2 are intersections
        // just check it's an intersection on the path you want to cut, and keep it
        // the intersections you have found are then fed to ConvertPositionsToMoveTo() which will
        // make new subpath at each one of these positions
        // inversion pour l'opération

        Shape tmp;
        pathb.Fill(&tmp, 0, false, false, false); // don't closeIfNeeded
        patha.Fill(&tmp, 1, true, false, false); // don't closeIfNeeded and just dump in the shape, don't reset it

        Shape shape;
        shape.ConvertToShape(&tmp, fill_justDont);

        std::vector<Path::cut_position> toCut;

        assert(shape.hasBackData());

        for (int i = 0; i < shape.numberOfPoints(); i++) {
            if (shape.getPoint(i).totalDegree() > 2) {
                // possibly an intersection
                // we need to check that at least one edge from the source path is incident to it
                // before we declare it's an intersection
                int nbOrig = 0;
                int nbOther = 0;
                int piece = -1;
                double t = 0.0;

                int cb = shape.getPoint(i).incidentEdge[FIRST];
                while (cb >= 0 && cb < shape.numberOfEdges()) {
                    if (shape.ebData[cb].pathID == 0) {
                        // the source has an edge incident to the point, get its position on the path
                        piece = shape.ebData[cb].pieceID;
                        t = shape.getEdge(cb).st == i ? shape.ebData[cb].tSt : shape.ebData[cb].tEn;
                        nbOrig++;
                    }
                    if (shape.ebData[cb].pathID == 1) {
                        nbOther++; // the cut is incident to this point
                    }
                    cb = shape.NextAt(i, cb);
                }

                if (nbOrig > 0 && nbOther > 0) {
                    // point incident to both path and cut: an intersection
                    // note that you only keep one position on the source; you could have degenerate
                    // cases where the source crosses itself at this point, and you wouyld miss an intersection
                    toCut.push_back({ .piece = piece, .t = t });
                }
            }
        }

        // I think it's useless now
        for (int i = shape.numberOfEdges() - 1; i >= 0; i--) {
            if (shape.ebData[i].pathID == 1) {
                shape.SubEdge(i);
            }
        }

        result.Copy(&pathb);
        result.ConvertPositionsToMoveTo(toCut.size(), toCut.data()); // cut where you found intersections
    }

    return result.MakePathVector();
}

/**
 * Workaround for buggy Path::Transform() which incorrectly transforms arc commands.
 *
 * TODO: Fix PathDescrArcTo::transform() and then remove this workaround.
 */
static void transformLivarotPath(Path *res, Geom::Affine const &affine)
{
    res->LoadPathVector(res->MakePathVector() * affine);
}

// helper for printing error messages, regardless of whether we have a GUI or not
// If desktop == NULL, errors will be shown on stderr
static void boolop_display_error_message(SPDesktop *desktop, Glib::ustring const &msg)
{
    if (desktop) {
        desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, msg);
    } else {
        g_printerr("%s\n", msg.c_str());
    }
}

// boolean operations on the desktop
// take the source paths from the file, do the operation, delete the originals and add the results
BoolOpErrors Inkscape::ObjectSet::pathBoolOp(bool_op bop, const bool skip_undo, const bool checked,
                                             const Glib::ustring icon_name, const Glib::ustring description,
                                             bool const silent)
{
    if (nullptr != desktop() && !checked) {
        SPDocument *doc = desktop()->getDocument();
        BoolOpErrors returnCode = ObjectSet::pathBoolOp(bop, true, true,icon_name);

        switch(returnCode) {
        case ERR_TOO_LESS_PATHS_1:
            if (!silent) {
                boolop_display_error_message(desktop(),
                                             _("Select <b>at least 1 path</b> to perform a boolean union."));
            }
            break;
        case ERR_TOO_LESS_PATHS_2:
            if (!silent) {
                boolop_display_error_message(desktop(),
                                             _("Select <b>at least 2 paths</b> to perform a boolean operation."));
            }
            break;
        case ERR_NO_PATHS:
            if (!silent) {
                boolop_display_error_message(desktop(),
                                             _("One of the objects is <b>not a path</b>, cannot perform boolean operation."));
            }
            break;
        case ERR_Z_ORDER:
            if (!silent) {
                boolop_display_error_message(desktop(),
                                             _("Unable to determine the <b>z-order</b> of the objects selected for difference, XOR, division, or path cut."));
            }
            break;
        case DONE_NO_PATH:
            if (!skip_undo) { 
                DocumentUndo::done(doc, description, "");
            }
            break;
        case DONE:
            if (!skip_undo) { 
                DocumentUndo::done(doc, description, icon_name);
            }
            break;
        case DONE_NO_ACTION:
            // Do nothing (?)
            break;
        }
        return returnCode;
    }

    SPDocument *doc = document();
    std::vector<SPItem*> il(items().begin(), items().end());

    // allow union on a single object for the purpose of removing self overlapse (svn log, revision 13334)
    if (il.size() < 2 && bop != bool_op_union) {
        return ERR_TOO_LESS_PATHS_2;
    }
    else if (il.size() < 1) {
        return ERR_TOO_LESS_PATHS_1;
    }

    g_assert(!il.empty());

    // reverseOrderForOp marks whether the order of the list is the top->down order
    // it's only used when there are 2 objects, and for operations who need to know the
    // topmost object (differences, cuts)
    bool reverseOrderForOp = false;

    if (bop == bool_op_diff || bop == bool_op_cut || bop == bool_op_slice) {
        // check in the tree to find which element of the selection list is topmost (for 2-operand commands only)
        Inkscape::XML::Node *a = il.front()->getRepr();
        Inkscape::XML::Node *b = il.back()->getRepr();

        if (a == nullptr || b == nullptr) {
            return ERR_Z_ORDER;
        }

        if (is_descendant_of(a, b)) {
            // a is a child of b, already in the proper order
        } else if (is_descendant_of(b, a)) {
            // reverse order
            reverseOrderForOp = true;
        } else {

            // objects are not in parent/child relationship;
            // find their lowest common ancestor
            Inkscape::XML::Node *parent = lowest_common_ancestor(a, b);
            if (parent == nullptr) {
                return ERR_Z_ORDER;
            }

            // find the children of the LCA that lead from it to the a and b
            Inkscape::XML::Node *as = find_containing_child(a, parent);
            Inkscape::XML::Node *bs = find_containing_child(b, parent);

            // find out which comes first
            for (Inkscape::XML::Node *child = parent->firstChild(); child; child = child->next()) {
                if (child == as) {
                    /* a first, so reverse. */
                    reverseOrderForOp = true;
                    break;
                }
                if (child == bs)
                    break;
            }
        }
    }

    g_assert(!il.empty());

    // first check if all the input objects have shapes
    // otherwise bail out
    for (auto item : il)
    {
        if (!is<SPShape>(item) && !is<SPText>(item) && !is<SPFlowtext>(item))
        {
            return ERR_NO_PATHS;
        }
    }

    // extract the livarot Paths from the source objects
    // also get the winding rule specified in the style
    int nbOriginaux = il.size();
    std::vector<Path *> originaux(nbOriginaux);
    std::vector<FillRule> origWind(nbOriginaux);
    std::vector<double> origThresh(nbOriginaux);
    int curOrig;
    {
        curOrig = 0;
        for (auto item : il)
        {
            // apply live path effects prior to performing boolean operation
            char const *id = item->getAttribute("id");
            if (auto lpeitem = cast<SPLPEItem>(item)) {
                SPDocument * document = item->document;
                lpeitem->removeAllPathEffects(true);
                SPObject *elemref = document->getObjectById(id);
                if (elemref && elemref != item) {
                    // If the LPE item is a shape, it is converted to a path 
                    // so we need to reupdate the item
                    item = cast<SPItem>(elemref);
                }
            }

            auto css = sp_repr_css_attr(il[0]->getRepr(), "style");
            auto val = sp_repr_css_property(css, "fill-rule", nullptr);
            if (val && strcmp(val, "nonzero") == 0) {
                origWind[curOrig]= fill_nonZero;
            } else if (val && strcmp(val, "evenodd") == 0) {
                origWind[curOrig]= fill_oddEven;
            } else {
                origWind[curOrig]= fill_nonZero;
            }
            sp_repr_css_attr_unref(css);

            if (auto curve = curve_for_item(item)) {
                auto pathv = curve->get_pathvector() * item->i2doc_affine();
                originaux[curOrig] = Path_for_pathvector(pathv).release();
                origThresh[curOrig] = get_threshold(pathv);
            } else {
                originaux[curOrig] = nullptr;
            }

            if (!originaux[curOrig] || originaux[curOrig]->descr_cmd.size() <= 1) {
                for (int i = curOrig; i >= 0; i--) delete originaux[i];
                return DONE_NO_ACTION;
            }

            curOrig++;
        }
    }
    // reverse if needed
    // note that the selection list keeps its order
    if ( reverseOrderForOp ) {
        std::swap(originaux[0], originaux[1]);
        std::swap(origWind[0], origWind[1]);
        std::swap(origThresh[0], origThresh[1]);
    }

    // and work
    // some temporary instances, first
    Shape *theShapeA = new Shape;
    Shape *theShapeB = new Shape;
    Shape *theShape = new Shape;
    Path *res = new Path;
    res->SetBackData(false);
    Path::cut_position  *toCut=nullptr;
    int                  nbToCut=0;

    if ( bop == bool_op_inters || bop == bool_op_union || bop == bool_op_diff || bop == bool_op_symdiff ) {
        // true boolean op
        // get the polygons of each path, with the winding rule specified, and apply the operation iteratively
        originaux[0]->ConvertWithBackData(origThresh[0]);

        originaux[0]->Fill(theShape, 0);

        theShapeA->ConvertToShape(theShape, origWind[0]);

        curOrig = 1;
        for (auto item : il){
            if(item==il[0])continue;
            originaux[curOrig]->ConvertWithBackData(origThresh[curOrig]);

            originaux[curOrig]->Fill(theShape, curOrig);

            theShapeB->ConvertToShape(theShape, origWind[curOrig]);

            /* Due to quantization of the input shape coordinates, we may end up with A or B being empty.
             * If this is a union or symdiff operation, we just use the non-empty shape as the result:
             *   A=0  =>  (0 or B) == B
             *   B=0  =>  (A or 0) == A
             *   A=0  =>  (0 xor B) == B
             *   B=0  =>  (A xor 0) == A
             * If this is an intersection operation, we just use the empty shape as the result:
             *   A=0  =>  (0 and B) == 0 == A
             *   B=0  =>  (A and 0) == 0 == B
             * If this a difference operation, and the upper shape (A) is empty, we keep B.
             * If the lower shape (B) is empty, we still keep B, as it's empty:
             *   A=0  =>  (B - 0) == B
             *   B=0  =>  (0 - A) == 0 == B
             *
             * In any case, the output from this operation is stored in shape A, so we may apply
             * the above rules simply by judicious use of swapping A and B where necessary.
             */
            bool zeroA = theShapeA->numberOfEdges() == 0;
            bool zeroB = theShapeB->numberOfEdges() == 0;
            if (zeroA || zeroB) {
                // We might need to do a swap. Apply the above rules depending on operation type.
                bool resultIsB =   ((bop == bool_op_union || bop == bool_op_symdiff) && zeroA)
                                   || ((bop == bool_op_inters) && zeroB)
                                   ||  (bop == bool_op_diff);
                if (resultIsB) {
                    // Swap A and B to use B as the result
                    Shape *swap = theShapeB;
                    theShapeB = theShapeA;
                    theShapeA = swap;
                }
            } else {
                // Just do the Boolean operation as usual
                // les elements arrivent en ordre inverse dans la liste
                theShape->Booleen(theShapeB, theShapeA, bop);
                Shape *swap = theShape;
                theShape = theShapeA;
                theShapeA = swap;
            }
            curOrig++;
        }

        {
            Shape *swap = theShape;
            theShape = theShapeA;
            theShapeA = swap;
        }

    } else if ( bop == bool_op_cut ) {
        // cuts= sort of a bastard boolean operation, thus not the axact same modus operandi
        // technically, the cut path is not necessarily a polygon (thus has no winding rule)
        // it is just uncrossed, and cleaned from duplicate edges and points
        // then it's fed to Booleen() which will uncross it against the other path
        // then comes the trick: each edge of the cut path is duplicated (one in each direction),
        // thus making a polygon. the weight of the edges of the cut are all 0, but
        // the Booleen need to invert the ones inside the source polygon (for the subsequent
        // ConvertToForme)

        // the cut path needs to have the highest pathID in the back data
        // that's how the Booleen() function knows it's an edge of the cut
        std::swap(originaux[0], originaux[1]);
        std::swap(origWind[0], origWind[1]);
        std::swap(origThresh[0], origThresh[1]);

        originaux[0]->ConvertWithBackData(origThresh[0]);

        originaux[0]->Fill(theShape, 0);

        theShapeA->ConvertToShape(theShape, origWind[0]);

        originaux[1]->ConvertWithBackData(origThresh[1]);

        if ((originaux[1]->pts.size() == 2) && originaux[1]->pts[0].isMoveTo && !originaux[1]->pts[1].isMoveTo)
            originaux[1]->Fill(theShape, 1,false,true,false); // see LP Bug 177956
        else
            originaux[1]->Fill(theShape, 1,false,false,false); //do not closeIfNeeded

        theShapeB->ConvertToShape(theShape, fill_justDont); // fill_justDont doesn't computes winding numbers

        // les elements arrivent en ordre inverse dans la liste
        theShape->Booleen(theShapeB, theShapeA, bool_op_cut, 1);

    } else if ( bop == bool_op_slice ) {
        // slice is not really a boolean operation
        // you just put the 2 shapes in a single polygon, uncross it
        // the points where the degree is > 2 are intersections
        // just check it's an intersection on the path you want to cut, and keep it
        // the intersections you have found are then fed to ConvertPositionsToMoveTo() which will
        // make new subpath at each one of these positions
        // inversion pour l'opération
        std::swap(originaux[0], originaux[1]);
        std::swap(origWind[0], origWind[1]);
        std::swap(origThresh[0], origThresh[1]);

        originaux[0]->ConvertWithBackData(origThresh[0]);

        originaux[0]->Fill(theShapeA, 0,false,false,false); // don't closeIfNeeded

        originaux[1]->ConvertWithBackData(origThresh[1]);

        originaux[1]->Fill(theShapeA, 1,true,false,false);// don't closeIfNeeded and just dump in the shape, don't reset it

        theShape->ConvertToShape(theShapeA, fill_justDont);

        if ( theShape->hasBackData() ) {
            // should always be the case, but ya never know
            {
                for (int i = 0; i < theShape->numberOfPoints(); i++) {
                    if ( theShape->getPoint(i).totalDegree() > 2 ) {
                        // possibly an intersection
                        // we need to check that at least one edge from the source path is incident to it
                        // before we declare it's an intersection
                        int cb = theShape->getPoint(i).incidentEdge[FIRST];
                        int   nbOrig=0;
                        int   nbOther=0;
                        int   piece=-1;
                        float t=0.0;
                        while ( cb >= 0 && cb < theShape->numberOfEdges() ) {
                            if ( theShape->ebData[cb].pathID == 0 ) {
                                // the source has an edge incident to the point, get its position on the path
                                piece=theShape->ebData[cb].pieceID;
                                if ( theShape->getEdge(cb).st == i ) {
                                    t=theShape->ebData[cb].tSt;
                                } else {
                                    t=theShape->ebData[cb].tEn;
                                }
                                nbOrig++;
                            }
                            if ( theShape->ebData[cb].pathID == 1 ) nbOther++; // the cut is incident to this point
                            cb=theShape->NextAt(i, cb);
                        }
                        if ( nbOrig > 0 && nbOther > 0 ) {
                            // point incident to both path and cut: an intersection
                            // note that you only keep one position on the source; you could have degenerate
                            // cases where the source crosses itself at this point, and you wouyld miss an intersection
                            toCut=(Path::cut_position*)realloc(toCut, (nbToCut+1)*sizeof(Path::cut_position));
                            toCut[nbToCut].piece=piece;
                            toCut[nbToCut].t=t;
                            nbToCut++;
                        }
                    }
                }
            }
            {
                // i think it's useless now
                int i = theShape->numberOfEdges() - 1;
                for (;i>=0;i--) {
                    if ( theShape->ebData[i].pathID == 1 ) {
                        theShape->SubEdge(i);
                    }
                }
            }

        }
    }

    int*    nesting=nullptr;
    int*    conts=nullptr;
    int     nbNest=0;
    // pour compenser le swap juste avant
    if ( bop == bool_op_slice ) {
        res->Copy(originaux[0]);
        res->ConvertPositionsToMoveTo(nbToCut, toCut); // cut where you found intersections
        free(toCut);
    } else if ( bop == bool_op_cut ) {
        // il faut appeler pour desallouer PointData (pas vital, mais bon)
        // the Booleen() function did not deallocate the point_data array in theShape, because this
        // function needs it.
        // this function uses the point_data to get the winding number of each path (ie: is a hole or not)
        // for later reconstruction in objects, you also need to extract which path is parent of holes (nesting info)
        theShape->ConvertToFormeNested(res, nbOriginaux, &originaux[0], nbNest, nesting, conts, true);
    } else {
        theShape->ConvertToForme(res, nbOriginaux, &originaux[0]);
    }

    delete theShape;
    delete theShapeA;
    delete theShapeB;
    for (int i = 0; i < nbOriginaux; i++)  delete originaux[i];

    if (res->descr_cmd.size() <= 1)
    {
        // only one command, presumably a moveto: it isn't a path
        for (auto l : il){
            l->deleteObject();
        }
        clear();

        delete res;
        return DONE_NO_PATH;
    }

    // get the source path object
    SPObject *source;
    if ( bop == bool_op_diff || bop == bool_op_cut || bop == bool_op_slice ) {
        if (reverseOrderForOp) {
            source = il[0];
        } else {
            source = il.back();
        }
    } else {
        // find out the bottom object
        std::vector<Inkscape::XML::Node*> sorted(xmlNodes().begin(), xmlNodes().end());

        sort(sorted.begin(),sorted.end(),sp_repr_compare_position_bool);

        source = doc->getObjectByRepr(sorted.front());
    }

    // adjust style properties that depend on a possible transform in the source object in order
    // to get a correct style attribute for the new path
    auto item_source = cast<SPItem>(source);
    Geom::Affine i2doc(item_source->i2doc_affine());

    Inkscape::XML::Node *repr_source = source->getRepr();

    // remember important aspects of the source path, to be restored
    gint pos = repr_source->position();
    Inkscape::XML::Node *parent = repr_source->parent();
    // remove source paths
    clear();
    for (auto l : il){
        if (l != item_source) {
            // delete the object for real, so that its clones can take appropriate action
            l->deleteObject();
        }
    }

    auto const source2doc_inverse = i2doc.inverse();
    char const *const old_transform_attibute = repr_source->attribute("transform");

    // now that we have the result, add it on the canvas
    if ( bop == bool_op_cut || bop == bool_op_slice ) {
        int    nbRP=0;
        Path** resPath;
        if ( bop == bool_op_slice ) {
            // there are moveto's at each intersection, but it's still one unique path
            // so break it down and add each subpath independently
            // we could call break_apart to do this, but while we have the description...
            resPath=res->SubPaths(nbRP, false);
        } else {
            // cut operation is a bit wicked: you need to keep holes
            // that's why you needed the nesting
            // ConvertToFormeNested() dumped all the subpath in a single Path "res", so we need
            // to get the path for each part of the polygon. that's why you need the nesting info:
            // to know in which subpath to add a subpath
            resPath=res->SubPathsWithNesting(nbRP, true, nbNest, nesting, conts);

            // cleaning
            if ( conts ) free(conts);
            if ( nesting ) free(nesting);
        }

        // add all the pieces resulting from cut or slice
        std::vector <Inkscape::XML::Node*> selection;
        for (int i=0;i<nbRP;i++) {
            transformLivarotPath(resPath[i], source2doc_inverse);
            gchar *d = resPath[i]->svg_dump_path();

            Inkscape::XML::Document *xml_doc = doc->getReprDoc();
            Inkscape::XML::Node *repr = xml_doc->createElement("svg:path");

            Inkscape::copy_object_properties(repr, repr_source);

            // Delete source on last iteration (after we don't need repr_source anymore). As a consequence, the last
            // item will inherit the original's id.
            if (i + 1 == nbRP) {
                item_source->deleteObject(false);
            }

            repr->setAttribute("d", d);
            g_free(d);

            // for slice, remove fill
            if (bop == bool_op_slice) {
                SPCSSAttr *css;

                css = sp_repr_css_attr_new();
                sp_repr_css_set_property(css, "fill", "none");

                sp_repr_css_change(repr, css, "style");

                sp_repr_css_attr_unref(css);
            }

            repr->setAttributeOrRemoveIfEmpty("transform", old_transform_attibute);

            // add the new repr to the parent
            // move to the saved position
            parent->addChildAtPos(repr, pos);

            selection.push_back(repr);
            Inkscape::GC::release(repr);

            delete resPath[i];
        }
        setReprList(selection);
        if ( resPath ) free(resPath);

    } else {
        transformLivarotPath(res, source2doc_inverse);
        gchar *d = res->svg_dump_path();

        Inkscape::XML::Document *xml_doc = doc->getReprDoc();
        Inkscape::XML::Node *repr = xml_doc->createElement("svg:path");

        Inkscape::copy_object_properties(repr, repr_source);

        // delete it so that its clones don't get alerted; this object will be restored shortly, with the same id
        item_source->deleteObject(false);

        repr->setAttribute("d", d);
        g_free(d);

        repr->setAttributeOrRemoveIfEmpty("transform", old_transform_attibute);

        parent->addChildAtPos(repr, pos);

        set(repr);
        Inkscape::GC::release(repr);
    }

    delete res;

    return DONE;
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
