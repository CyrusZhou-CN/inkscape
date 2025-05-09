// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Simplify paths (reduce node count).
 *//*
 * Authors:
 * see git history
 *  Created by fred on Fri Dec 05 2003.
 *  tweaked endlessly by bulia byak <buliabyak@users.sf.net>
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifdef HAVE_CONFIG_H
#endif

#include <vector>

#include "path-simplify.h"
#include "path-util.h"

#include "document-undo.h"
#include "preferences.h"

#include "livarot/Path.h"

#include "object/sp-item-group.h"
#include "object/sp-path.h"

using Inkscape::DocumentUndo;

// Return number of paths simplified (can be greater than one if group).
int
path_simplify(SPItem *item, float threshold, bool justCoalesce, double size)
{
    //If this is a group, do the children instead
    auto group = cast<SPGroup>(item);
    if (group) {
        int pathsSimplified = 0;
        std::vector<SPItem*> items = group->item_list();
        for (auto item : items) {
            pathsSimplified += path_simplify(item, threshold, justCoalesce, size);
        }
        return pathsSimplified;
    }

    auto path = cast<SPPath>(item);
    if (!path) {
        return 0;
    }

    std::string orig_path_str;
    if (path->getRepr()->attribute("d")) {
        orig_path_str = path->getRepr()->attribute("d");
    }

    int nodes_before_simplify = path->nodesInPath();

    // There is actually no option in the preferences dialog for this!
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool simplifyIndividualPaths = prefs->getBool("/options/simplifyindividualpaths/value");
    if (simplifyIndividualPaths) {
        Geom::OptRect itemBbox = item->documentVisualBounds();
        if (itemBbox) {
            size = L2(itemBbox->dimensions());
        } else {
            size = 0;
        }
    }

    // Correct virtual size by full transform (bug #166937).
    size /= item->i2doc_affine().descrim();

    // Save the transform, to re-apply it after simplification.
    Geom::Affine const transform(item->transform);

    /*
       reset the transform, effectively transforming the item by transform.inverse();
       this is necessary so that the item is transformed twice back and forth,
       allowing all compensations to cancel out regardless of the preferences
    */
    item->doWriteTransform(Geom::identity());

    // SPLivarot: Start  -----------------

    // Get path to simplify (note that the path *before* LPE calculation is needed)
    auto orig = Path_for_item_before_LPE(item, false);
    if (!orig) {
        return 0;
    }

    if ( justCoalesce ) {
        orig->Coalesce(threshold * size);
    } else {
        orig->ConvertEvenLines(threshold * size);
        orig->Simplify(threshold * size);
    }

    // Path
    auto simplified_path_str = orig->svg_dump_path();

    // SPLivarot: End  -------------------

    char const *patheffect = item->getRepr()->attribute("inkscape:path-effect");
    if (patheffect) {
        item->setAttribute("inkscape:original-d", simplified_path_str.c_str());
    } else {
        item->setAttribute("d", simplified_path_str.c_str());
    }

    // reapply the transform
    item->doWriteTransform(transform);

    // remove irrelevant old nodetypes attibute
    item->removeAttribute("sodipodi:nodetypes");

    int nodes_after_simplify = path->nodesInPath();

    if (nodes_before_simplify < nodes_after_simplify) {
        if (patheffect) {
            item->setAttribute("inkscape:original-d", orig_path_str.c_str());
        } else {
            item->setAttribute("d", orig_path_str.c_str());
        }
        return 0;
    }

    return 1;
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
