#define __SP_SELECTION_CHEMISTRY_C__

/*
 * Miscellanous operations on selected items
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Frank Felfe <innerspace@iname.com>
 *   MenTaLguY <mental@rydia.net>
 *
 * Copyright (C) 2004 MenTaLguY
 * Copyright (C) 1999-2002 Frank Felfe
 * Copyright (C) 1999-2002 Lauris Kaplinski
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include <config.h>

#include <string.h>
#include <gtk/gtk.h>
#include <gtkmm.h>

#include "svg/svg.h"
#include "xml/repr-private.h"
#include "xml/repr-private.h"
#include "document.h"
#include "inkscape.h"
#include "desktop.h"
#include "desktop-style.h"
#include "selection.h"
#include "tools-switch.h"
#include "desktop-handles.h"
#include "sp-item-transform.h"
#include "sp-item-group.h"
#include "sp-marker.h"
#include "sp-path.h"
#include "sp-use.h"
#include "sp-text.h"
#include "sp-tspan.h"
#include "text-context.h"
#include "dropper-context.h"
#include "helper/sp-intl.h"
#include "display/sp-canvas.h"
#include "path-chemistry.h"
#include "desktop-affine.h"
#include "snap.h"
#include "libnr/nr-matrix.h"
#include "libnr/nr-matrix-ops.h"
#include "libnr/nr-matrix-rotate-ops.h"
#include "libnr/nr-matrix-translate-ops.h"
#include "libnr/nr-rotate-fns.h"
#include "libnr/nr-scale-ops.h"
#include "libnr/nr-scale-translate-ops.h"
#include "libnr/nr-translate-matrix-ops.h"
#include "libnr/nr-translate-scale-ops.h"
#include "style.h"
#include "document-private.h"
#include "sp-gradient.h"
#include "sp-gradient-reference.h"
#include "sp-linear-gradient-fns.h"
#include "sp-pattern.h"
#include "sp-radial-gradient-fns.h"
#include "sp-use-reference.h"
#include "sp-namedview.h"
#include "prefs-utils.h"
#include "sp-offset.h"
#include "file.h"
#include "dir-util.h"
using NR::X;
using NR::Y;

#include "selection-chemistry.h"

/* fixme: find a better place */
GSList *clipboard = NULL;
GSList *defs_clipboard = NULL;
SPCSSAttr *style_clipboard = NULL;

void sp_selection_delete()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == NULL) {
        return;
    }

    SPSelection *selection = SP_DT_SELECTION(desktop);

    // check if something is selected
    if (selection->isEmpty()) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("<b>Nothing</b> was deleted."));
        return;
    }

    GSList *selected = g_slist_copy(const_cast<GSList *>(selection->itemList()));
    GSList *iter;
    for ( iter = selected ; iter ; iter = iter->next ) {
        sp_object_ref((SPObject *)iter->data, NULL);
    }
    selection->clear();

    while (selected) {
        SPItem *item = (SPItem *)selected->data;
        SP_OBJECT(item)->deleteObject();
        sp_object_unref((SPObject *)item, NULL);
        selected = g_slist_remove(selected, selected->data);
    }

    /* a tool may have set up private information in it's selection context
     * that depends on desktop items.  I think the only sane way to deal with
     * this currently is to reset the current tool, which will reset it's
     * associated selection context.  For example: deleting an object
     * while moving it around the canvas.
     */
    tools_switch ( desktop, tools_active ( desktop ) );

    sp_document_done(SP_DT_DOCUMENT(desktop));
}

/* fixme: sequencing */
void sp_selection_duplicate()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == NULL)
        return;

    SPSelection *selection = SP_DT_SELECTION(desktop);

    // check if something is selected
    if (selection->isEmpty()) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select <b>object(s)</b> to duplicate."));
        return;
    }

    GSList *reprs = g_slist_copy((GSList *) selection->reprList());

    selection->clear();

    SPRepr *parent = ((SPRepr *) reprs->data)->parent;
    gboolean sort = TRUE;
    for (GSList *i = reprs->next; i; i = i->next) {
        if ((((SPRepr *) i->data)->parent) != parent) {
            // We can duplicate items from different parents, but we cannot do sorting in this case
            sort = FALSE;
        }
    }

    if (sort)
        reprs = g_slist_sort(reprs, (GCompareFunc) sp_repr_compare_position);

    GSList *newsel = NULL;

    while (reprs) {
        parent = ((SPRepr *) reprs->data)->parent;
        SPRepr *copy = sp_repr_duplicate((SPRepr *) reprs->data);

        sp_repr_append_child(parent, copy);

        newsel = g_slist_prepend(newsel, copy);
        reprs = g_slist_remove(reprs, reprs->data);
        sp_repr_unref(copy);
    }

    sp_document_done(SP_DT_DOCUMENT(desktop));

    selection->setReprList(newsel);

    g_slist_free(newsel);
}

void sp_edit_clear_all()
{
    SPDesktop *dt = SP_ACTIVE_DESKTOP;
    if (!dt)
        return;

    SPDocument *doc = SP_DT_DOCUMENT(dt);
    SP_DT_SELECTION(dt)->clear();

    g_return_if_fail(SP_IS_GROUP(dt->currentLayer()));
    GSList *items = sp_item_group_item_list(SP_GROUP(dt->currentLayer()));

    while (items) {
        SP_OBJECT (items->data)->deleteObject();
        items = g_slist_remove(items, items->data);
    }

    sp_document_done(doc);
}

GSList *
get_all_items (GSList *list, SPObject *from, SPDesktop *desktop, bool onlyvisible, bool onlysensitive)
{
    for (SPObject *child = sp_object_first_child(SP_OBJECT(from)) ; child != NULL; child = SP_OBJECT_NEXT(child) ) {
        if (SP_IS_ITEM(child) &&
            !desktop->isLayer(SP_ITEM(child)) &&
            (!onlysensitive || !SP_ITEM(child)->isLocked()) && 
            (!onlyvisible || !desktop->itemIsHidden(SP_ITEM(child))) )
        {
            list = g_slist_prepend (list, SP_ITEM(child));
        }

        if (SP_IS_ITEM(child) && desktop->isLayer(SP_ITEM(child))) {
            list = get_all_items (list, child, desktop, onlyvisible, onlysensitive);
        }
    }

    return list;
}

void sp_edit_select_all()
{
    SPDesktop *dt = SP_ACTIVE_DESKTOP;
    if (!dt)
        return;

    SPSelection *selection = SP_DT_SELECTION(dt);

    g_return_if_fail(SP_IS_GROUP(dt->currentLayer()));

    bool inlayer = prefs_get_int_attribute ("options.kbselection", "inlayer", 1);
    bool onlyvisible = prefs_get_int_attribute ("options.kbselection", "onlyvisible", 1);
    bool onlysensitive = prefs_get_int_attribute ("options.kbselection", "onlysensitive", 1);

    if (inlayer) {

        if ( (onlysensitive && SP_ITEM(dt->currentLayer())->isLocked()) ||
             (onlyvisible && dt->itemIsHidden(SP_ITEM(dt->currentLayer()))) )
        return;

        GSList *items = sp_item_group_item_list(SP_GROUP(dt->currentLayer()));
        while (items) {
            SPRepr *repr = SP_OBJECT_REPR(items->data);
            if ((!onlysensitive || !SP_ITEM(items->data)->isLocked()) && 
                (!onlyvisible || !dt->itemIsHidden(SP_ITEM(items->data))) &&
                !dt->isLayer(SP_ITEM(items->data)) &&
                !selection->includesRepr(repr)) {
                selection->addRepr(repr);
            }
            items = g_slist_remove(items, items->data);
        }
    } else {
        GSList *all_items = get_all_items (NULL, dt->currentRoot(), dt, onlyvisible, onlysensitive);
        for ( GSList const *iter = all_items ; iter != NULL ; iter = iter->next ) {
            if (!selection->includes(SP_OBJECT (iter->data))) {
                selection->add (SP_OBJECT (iter->data));
            }
        }
    }
}

static void
sp_group_cleanup(SPGroup *group)
{
    GSList *l = NULL;
    for (SPObject *child = sp_object_first_child(SP_OBJECT(group)) ; child != NULL; child = SP_OBJECT_NEXT(child) ) {
        sp_object_ref(child, NULL);
        l = g_slist_prepend(l, child);
    }

    while (l) {
        if (SP_IS_GROUP(l->data)) {
            sp_group_cleanup(SP_GROUP(l->data));
        } else if (SP_IS_PATH(l->data)) {
            sp_path_cleanup(SP_PATH(l->data));
        }
        sp_object_unref(SP_OBJECT(l->data), NULL);
        l = g_slist_remove(l, l->data);
    }


    if (!strcmp(sp_repr_name(SP_OBJECT_REPR(group)), "g")) {
        gint numitems;
        numitems = 0;
        for (SPObject *child = sp_object_first_child(SP_OBJECT(group)) ; child != NULL; child = SP_OBJECT_NEXT(child) ) {
            if (SP_IS_ITEM(child)) numitems += 1;
        }
        if (numitems <= 1) {
            sp_item_group_ungroup(group, NULL);
        }
    }
}

void sp_selection_cleanup()
{
    SPDocument *doc = SP_ACTIVE_DOCUMENT;
    if (!doc)
        return;

    if (SP_ACTIVE_DESKTOP) {
        SP_DT_SELECTION(SP_ACTIVE_DESKTOP)->clear();
    }

    SPGroup *root = SP_GROUP(SP_DOCUMENT_ROOT(doc));
    sp_group_cleanup(root);

    sp_document_done(doc);
}

void sp_selection_group()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == NULL)
        return;

    SPSelection *selection = SP_DT_SELECTION(desktop);

    // Check if something is selected.
    if (selection->isEmpty()) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select <b>two or more objects</b> to group."));
        return;
    }

    GSList const *l = (GSList *) selection->reprList();

    // Check if at least two objects are selected.
    if (l->next == NULL) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select <b>at least two objects</b> to group."));
        return;
    }

    // Check if all selected objects have common parent.
    GSList *reprs = g_slist_copy((GSList *) selection->reprList());
    SPRepr *parent = ((SPRepr *) reprs->data)->parent;
    for (GSList *i = reprs->next; i; i = i->next) {
        if ((((SPRepr *) i->data)->parent) != parent) {
            desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("You cannot group objects from <b>different groups</b> or <b>layers</b>."));
            return;
        }
    }

    GSList *p = g_slist_copy((GSList *) l);

    selection->clear();

    p = g_slist_sort(p, (GCompareFunc) sp_repr_compare_position);

    // Remember the position of the topmost object.
    gint topmost = sp_repr_position((SPRepr *) g_slist_last(p)->data);

    SPRepr *group = sp_repr_new("g");

    while (p) {
        SPRepr *spnew;
        SPRepr *current = (SPRepr *) p->data;
        spnew = sp_repr_duplicate(current);
        sp_repr_unparent(current);
        sp_repr_append_child(group, spnew);
        sp_repr_unref(spnew);
        topmost --;
        p = g_slist_remove(p, current);
    }

    // Add the new group to the group members' common parent.
    sp_repr_append_child(parent, group);

    // Move to the position of the topmost, reduced by the number of deleted items.
    sp_repr_set_position_absolute(group, topmost > 0 ? topmost + 1 : 0);

    sp_document_done(SP_DT_DOCUMENT(desktop));

    selection->setRepr(group);
    sp_repr_unref(group);
}

void sp_selection_ungroup()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == NULL)
        return;

    SPSelection *selection = SP_DT_SELECTION(desktop);

    if (selection->isEmpty()) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select a <b>group</b> to ungroup."));
        return;
    }

    // Get a copy of current selection.
    GSList *new_select = NULL;
    bool ungrouped = false;
    for (GSList *items = g_slist_copy((GSList *) selection->itemList());
         items != NULL;
         items = items->next)
    {
        SPItem *group = (SPItem *) items->data;

        /* We do not allow ungrouping <svg> etc. (lauris) */
        if (strcmp(sp_repr_name(SP_OBJECT_REPR(group)), "g") && strcmp(sp_repr_name(SP_OBJECT_REPR(group)), "switch")) {
            // keep the non-group item in the new selection
            new_select = g_slist_prepend(new_select, group);
            continue;
        }

        GSList *children = NULL;
        /* This is not strictly required, but is nicer to rely on group ::destroy (lauris) */
        sp_item_group_ungroup(SP_GROUP(group), &children, false);
        ungrouped = true;
        // Add ungrouped items to the new selection.
        new_select = g_slist_concat(new_select, children);
    }

    if (new_select) { // Set new selection.
        selection->clear();
        selection->setItemList(new_select);
        g_slist_free(new_select);
    }
    if (!ungrouped) {
        desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("<b>No groups</b> to ungroup in the selection."));
    }

    sp_document_done(SP_DT_DOCUMENT(desktop));
}

static SPGroup *
sp_item_list_common_parent_group(const GSList *items)
{
    if (!items) {
        return NULL;
    }
    SPObject *parent = SP_OBJECT_PARENT(items->data);
    /* Strictly speaking this CAN happen, if user selects <svg> from XML editor */
    if (!SP_IS_GROUP(parent)) {
        return NULL;
    }
    for (items = items->next; items; items = items->next) {
        if (SP_OBJECT_PARENT(items->data) != parent) {
            return NULL;
        }
    }

    return SP_GROUP(parent);
}

/** Finds out the minimum common bbox of the selected items
 */
NR::Rect
enclose_items(const GSList *items)
{
    g_assert(items != NULL);

    NR::Rect r = sp_item_bbox_desktop((SPItem *) items->data);

    for (GSList *i = items->next; i; i = i->next) {
        r = NR::Rect::union_bounds(r, sp_item_bbox_desktop((SPItem *) i->data));
    }

    return r;
}

SPObject *
prev_sibling(SPObject *child)
{
    SPObject *parent = SP_OBJECT_PARENT(child);
    if (!SP_IS_GROUP(parent)) {
        return NULL;
    }
    for ( SPObject *i = sp_object_first_child(parent) ; i; i = SP_OBJECT_NEXT(i) ) {
        if (i->next == child)
            return i;
    }
    return NULL;
}

void
sp_selection_raise()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (!desktop)
        return;

    SPSelection *selection = SP_DT_SELECTION(desktop);

    GSList const *items = (GSList *) selection->itemList();
    if (!items) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select <b>objects</b> to raise."));
        return;
    }

    SPGroup const *group = sp_item_list_common_parent_group(items);
    if (!group) {
        desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("You cannot raise/lower objects from <b>different groups</b> or <b>layers</b>."));
        return;
    }

    SPRepr *grepr = SP_OBJECT_REPR(group);

    /* construct reverse-ordered list of selected children */
    GSList *rev = g_slist_copy((GSList *) items);
    rev = g_slist_sort(rev, (GCompareFunc) sp_item_repr_compare_position);

    // find out the common bbox of the selected items
    NR::Rect selected = enclose_items(items);

    // for all objects in the selection (starting from top)
    while (rev) {
        SPObject *child = SP_OBJECT(rev->data);
        // for each selected object, find the next sibling
        for (SPObject *newref = child->next; newref; newref = newref->next) {
            // if the sibling is an item AND overlaps our selection,
            if (SP_IS_ITEM(newref) && selected.intersects(sp_item_bbox_desktop(SP_ITEM(newref)))) {
                // AND if it's not one of our selected objects,
                if (!g_slist_find((GSList *) items, newref)) {
                    // move the selected object after that sibling
                    sp_repr_change_order(grepr, SP_OBJECT_REPR(child), SP_OBJECT_REPR(newref));
                }
                break;
            }
        }
        rev = g_slist_remove(rev, child);
    }

    sp_document_done(SP_DT_DOCUMENT(desktop));
}

void sp_selection_raise_to_top()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == NULL)
        return;

    SPDocument *document = SP_DT_DOCUMENT(desktop);
    SPSelection *selection = SP_DT_SELECTION(desktop);

    if (selection->isEmpty()) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select <b>object(s)</b> to raise to top."));
        return;
    }

    GSList const *items = (GSList *) selection->itemList();

    SPGroup const *group = sp_item_list_common_parent_group(items);
    if (!group) {
        desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("You cannot raise/lower objects from <b>different groups</b> or <b>layers</b>."));
        return;
    }

    GSList *rl = g_slist_copy((GSList *) selection->reprList());
    rl = g_slist_sort(rl, (GCompareFunc) sp_repr_compare_position);

    for (GSList *l = rl; l != NULL; l = l->next) {
        SPRepr *repr = (SPRepr *) l->data;
        sp_repr_set_position_absolute(repr, -1);
    }

    g_slist_free(rl);

    sp_document_done(document);
}

void
sp_selection_lower()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == NULL)
        return;

    SPSelection *selection = SP_DT_SELECTION(desktop);

    GSList const *items = (GSList *) selection->itemList();
    if (!items) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select <b>object(s)</b> to lower."));
        return;
    }

    SPGroup const *group = sp_item_list_common_parent_group(items);
    if (!group) {
        desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("You cannot raise/lower objects from <b>different groups</b> or <b>layers</b>."));
        return;
    }

    SPRepr *grepr = SP_OBJECT_REPR(group);

    // find out the common bbox of the selected items
    NR::Rect selected = enclose_items(items);

    /* construct direct-ordered list of selected children */
    GSList *rev = g_slist_copy((GSList *) items);
    rev = g_slist_sort(rev, (GCompareFunc) sp_item_repr_compare_position);
    rev = g_slist_reverse(rev);

    // for all objects in the selection (starting from top)
    while (rev) {
        SPObject *child = SP_OBJECT(rev->data);
        // for each selected object, find the prev sibling
        for (SPObject *newref = prev_sibling(child); newref; newref = prev_sibling(newref)) {
            // if the sibling is an item AND overlaps our selection,
            if (SP_IS_ITEM(newref) && selected.intersects(sp_item_bbox_desktop(SP_ITEM(newref)))) {
                // AND if it's not one of our selected objects,
                if (!g_slist_find((GSList *) items, newref)) {
                    // move the selected object before that sibling
                    SPObject *put_after = prev_sibling(newref);
                    if (put_after)
                        sp_repr_change_order(grepr, SP_OBJECT_REPR(child), SP_OBJECT_REPR(put_after));
                    else
                        sp_repr_set_position_absolute(SP_OBJECT_REPR(child), 0);
                }
                break;
            }
        }
        rev = g_slist_remove(rev, child);
    }

    sp_document_done(SP_DT_DOCUMENT(desktop));

}

void sp_selection_lower_to_bottom()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == NULL)
        return;

    SPDocument *document = SP_DT_DOCUMENT(desktop);
    SPSelection *selection = SP_DT_SELECTION(desktop);

    if (selection->isEmpty()) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select <b>object(s)</b> to lower to bottom."));
        return;
    }

    GSList const *items = (GSList *) selection->itemList();

    SPGroup const *group = sp_item_list_common_parent_group(items);
    if (!group) {
        desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("You cannot raise/lower objects from <b>different groups</b> or <b>layers</b>."));
        return;
    }

    GSList *rl;
    rl = g_slist_copy((GSList *) selection->reprList());
    rl = g_slist_sort(rl, (GCompareFunc) sp_repr_compare_position);
    rl = g_slist_reverse(rl);

    for (GSList *l = rl; l != NULL; l = l->next) {
        gint minpos;
        SPObject *pp, *pc;
        SPRepr *repr = (SPRepr *) l->data;
        pp = document->getObjectByRepr(sp_repr_parent(repr));
        minpos = 0;
        g_assert(SP_IS_GROUP(pp));
        pc = sp_object_first_child(pp);
        while (!SP_IS_ITEM(pc)) {
            minpos += 1;
            pc = pc->next;
        }
        sp_repr_set_position_absolute(repr, minpos);
    }

    g_slist_free(rl);

    sp_document_done(document);
}

void
sp_undo(SPDesktop *desktop, SPDocument *doc)
{
    if (SP_IS_DESKTOP(desktop)) {
        if (!sp_document_undo(SP_DT_DOCUMENT(desktop)))
            desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Nothing to undo."));
    }
}

void
sp_redo(SPDesktop *desktop, SPDocument *doc)
{
    if (SP_IS_DESKTOP(desktop)) {
        if (!sp_document_redo(SP_DT_DOCUMENT(desktop)))
            desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Nothing to redo."));
    }
}

void sp_selection_cut()
{
    sp_selection_copy();
    sp_selection_delete();
}

static void sp_copy_stuff_used_by_item(SPItem *item, const GSList *items);

void sp_copy_gradient (SPGradient *gradient)
{
    SPGradient *ref = gradient;

    while (ref) { 
        // climb up the refs, copying each one in the chain
        SPRepr *grad_repr =sp_repr_duplicate (SP_OBJECT_REPR(ref));
        defs_clipboard = g_slist_prepend(defs_clipboard, grad_repr);

        ref = ref->ref->getObject();
    }
}

void sp_copy_pattern (SPPattern *pattern)
{
    SPPattern *ref = pattern;

    while (ref) {
        // climb up the refs, copying each one in the chain
        SPRepr *pattern_repr = sp_repr_duplicate(SP_OBJECT_REPR(ref));
        defs_clipboard = g_slist_prepend(defs_clipboard, pattern_repr);

        // items in the pattern may also use gradients and other patterns, so we need to recurse here as well
        for (SPObject *child = sp_object_first_child(SP_OBJECT(ref)) ; child != NULL; child = SP_OBJECT_NEXT(child) ) {
            if (!SP_IS_ITEM (child))
                continue;
            sp_copy_stuff_used_by_item ((SPItem *) child, NULL);
        }

        ref = ref->ref->getObject();
    }
}

void sp_copy_marker (SPMarker *marker)
{
    SPRepr *marker_repr = sp_repr_duplicate(SP_OBJECT_REPR(marker));
    defs_clipboard = g_slist_prepend(defs_clipboard, marker_repr);
}


void sp_copy_textpath_path (SPTextPath *tp, const GSList *items)
{
    SPItem *path = sp_textpath_get_path_item (tp);
    if (!path)
        return;
    if (items && g_slist_find ((GSList *) items, path)) // do not copy it to defs if it is already in the list of items copied
        return;
    SPRepr *repr = sp_repr_duplicate (SP_OBJECT_REPR(path));
    defs_clipboard = g_slist_prepend (defs_clipboard, repr);
}

void sp_copy_stuff_used_by_item (SPItem *item, const GSList *items)
{
    SPStyle *style = SP_OBJECT_STYLE (item); 

    if (style && (style->fill.type == SP_PAINT_TYPE_PAINTSERVER)) { 
        SPObject *server = SP_OBJECT_STYLE_FILL_SERVER(item);
        if (SP_IS_LINEARGRADIENT (server) || SP_IS_RADIALGRADIENT (server))
            sp_copy_gradient (SP_GRADIENT(server));
        if (SP_IS_PATTERN (server))
            sp_copy_pattern (SP_PATTERN(server));
    }

    if (style && (style->stroke.type == SP_PAINT_TYPE_PAINTSERVER)) { 
        SPObject *server = SP_OBJECT_STYLE_STROKE_SERVER(item);
        if (SP_IS_LINEARGRADIENT (server) || SP_IS_RADIALGRADIENT (server))
            sp_copy_gradient (SP_GRADIENT(server));
        if (SP_IS_PATTERN (server))
            sp_copy_pattern (SP_PATTERN(server));
    }

    if (SP_IS_SHAPE (item)) { 
        SPShape *shape = SP_SHAPE (item);
        for (int i = 0 ; i < SP_MARKER_LOC_QTY ; i++) {
            if (shape->marker[i]) {
                sp_copy_marker (SP_MARKER (shape->marker[i]));
            }
        }
    }

    if (SP_IS_TEXT_TEXTPATH (item)) {
        sp_copy_textpath_path (SP_TEXTPATH(sp_object_first_child(SP_OBJECT(item))), items);
    }

    // recurse
    for (SPObject *o = SP_OBJECT(item)->children; o != NULL; o = o->next) {
        if (SP_IS_ITEM(o))
            sp_copy_stuff_used_by_item (SP_ITEM (o), items);
    }
}

/**
 * \pre item != NULL
 */
SPCSSAttr *
take_style_from_item (SPItem *item)
{
    // write the complete cascaded style, context-free
    SPCSSAttr *css = sp_css_attr_from_style (SP_OBJECT(item), SP_STYLE_FLAG_ALWAYS);
    if ((SP_IS_GROUP(item) && SP_OBJECT(item)->children) ||
        (SP_IS_TEXT (item) && SP_OBJECT(item)->children && SP_OBJECT(item)->children->next == NULL)) {
        // if this is a text with exactly one tspan child, merge the style of that tspan as well
        // If this is a group, merge the style of its first child
        SPCSSAttr *temp = sp_css_attr_from_style (sp_object_last_child (item), SP_STYLE_FLAG_IFSET);
        sp_repr_css_merge (css, temp);
        sp_repr_css_attr_unref (temp);
    }
    if (!(SP_IS_TEXT (item) || SP_IS_TSPAN (item) || SP_IS_STRING (item))) {
        // do not copy text properties from non-text objects, it's confusing
        css = sp_css_attr_unset_text (css);
    }

    // FIXME: also transform gradient/pattern fills
    double ex = NR::expansion(sp_item_i2doc_affine(item));
    if (ex != 1.0) {
        css = sp_css_attr_scale (css, ex);
    }

    //sp_repr_css_print (style_clipboard);

    return css;
}

void sp_selection_copy()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == NULL)
        return;

    SPSelection *selection = SP_DT_SELECTION(desktop);

    if (tools_isactive (desktop, TOOLS_DROPPER)) {
        sp_dropper_context_copy(desktop->event_context);
        return; // copied color under cursor, nothing else to do
    }

    // check if something is selected
    if (selection->isEmpty()) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Nothing was copied."));
        return;
    }

    const GSList *items = (GSList *) selection->itemList();

    // 0. Copy text to system clipboard
    // FIXME: for non-texts, put serialized XML as text to the clipboard; 
    //for this sp_repr_write_stream needs to be rewritten with iostream instead of FILE
    Glib::ustring text;
    guint texts = 0;
    for (GSList *i = (GSList *) items; i; i = i->next) {
        SPItem *item = SP_ITEM (i->data);
        if (SP_IS_TEXT (item)) {
            if (texts > 0) // if more than one text object is copied, separate them by spaces
                text += " ";
            text += sp_text_get_string_multiline (SP_TEXT (item));
            texts++;
        }
    }
    if (!text.empty()) {
        Glib::RefPtr<Gtk::Clipboard> refClipboard = Gtk::Clipboard::get();
        refClipboard->set_text(text);
    }

    // 1.  Store referenced stuff:
    // clear old defs clipboard
    while (defs_clipboard) {
        sp_repr_unref((SPRepr *) defs_clipboard->data);
        defs_clipboard = g_slist_remove (defs_clipboard, defs_clipboard->data);
    }
    // copy stuff referenced by all items to defs_clipboard
    for (GSList *i = (GSList *) items; i != NULL; i = i->next) {
        sp_copy_stuff_used_by_item (SP_ITEM (i->data), items);
    }

    GSList *reprs = g_slist_copy ((GSList *) selection->reprList());

    // 2.  Store style:
    if (style_clipboard) {
        sp_repr_css_attr_unref (style_clipboard);
    }
    SPItem *item = SP_ITEM (items->data);
    style_clipboard = take_style_from_item (item);

    // 3.  Sort items:
    SPRepr *parent = ((SPRepr *) reprs->data)->parent;
    gboolean sort = TRUE;
    for (GSList *i = reprs->next; i; i = i->next) {
         if ((((SPRepr *) i->data)->parent) != parent) {
             // We can copy items from different parents, but we cannot do sorting in this case
             sort = FALSE;
         }
     }

     if (sort)
        reprs = g_slist_sort(reprs, (GCompareFunc) sp_repr_compare_position);

    // 4.  Copy item reprs:
    //clear old clipboard 
    while (clipboard) {
        sp_repr_unref((SPRepr *) clipboard->data);
        clipboard = g_slist_remove(clipboard, clipboard->data);
    }

    while (reprs != NULL) {
        SPRepr *repr = (SPRepr *) reprs->data;
        reprs = g_slist_remove (reprs, repr);
        SPCSSAttr *css = sp_repr_css_attr_inherited(repr, "style");
        SPRepr *copy = sp_repr_duplicate(repr);
        sp_repr_css_set(copy, css, "style");
        sp_repr_css_attr_unref(css);

        clipboard = g_slist_prepend(clipboard, copy);
    }

    clipboard = g_slist_reverse(clipboard);
    defs_clipboard = g_slist_reverse(defs_clipboard);
}

/**
Add gradients/patterns/markers referenced by copied objects to defs
*/
void 
paste_defs (SPDocument *doc)
{
    for (GSList *gl = defs_clipboard; gl != NULL; gl = gl->next) {
        SPDefs *defs= (SPDefs *) SP_DOCUMENT_DEFS(doc);
        SPRepr *repr = (SPRepr *) gl->data;
        SPObject *exists = doc->getObjectByRepr(repr);
        if (!exists){
            SPRepr *copy = sp_repr_duplicate(repr);
            sp_repr_add_child(SP_OBJECT_REPR(defs), copy, NULL);
            sp_repr_unref(copy);
        }
    }
}

void sp_selection_paste(bool in_place)
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == NULL) return;
    g_assert(SP_IS_DESKTOP(desktop));

            SPItem *layer=SP_ITEM(desktop->currentLayer());
            if ( !layer || desktop->itemIsHidden(layer)) {
                desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("<b>Current layer is hidden</b>. Unhide it to be able to paste to it."));
                return;
            }
            if ( !layer || layer->isLocked()) {
                desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("<b>Current layer is locked</b>. Unlock it to be able to paste to it."));
                return;
            }

    SPSelection *selection = SP_DT_SELECTION(desktop);

    if (tools_isactive (desktop, TOOLS_TEXT)) {
        if (sp_text_paste_inline(desktop->event_context))
            return; // pasted from system clipboard into text, nothing else to do
    }

    // check if something is in the clipboard
    if (clipboard == NULL) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Nothing on the clipboard."));
        return;
    }

    selection->clear();

    paste_defs (SP_DT_DOCUMENT(desktop));

    GSList *copied = NULL;
    // add objects to document
    for (GSList *l = clipboard; l != NULL; l = l->next) {
        SPRepr *repr = (SPRepr *) l->data;
        SPRepr *copy = sp_repr_duplicate(repr);
        desktop->currentLayer()->appendChildRepr(copy);
        copied = g_slist_append(copied, copy);
        sp_repr_unref(copy);
    }

    // add new documents to selection (would have done this above but screws with fill/stroke dialog)
    for (GSList *l = copied; l != NULL; l = l->next) {
        SPRepr *repr = (SPRepr *) l->data;
        selection->addRepr(repr);
    }

    if (!in_place) {
        sp_document_ensure_up_to_date(SP_DT_DOCUMENT(desktop));

        NR::Point m( sp_desktop_point(desktop) - selection->bounds().midpoint() );

        /* Snap the offset of the new item(s) to the grid */
        /* FIXME: this gridsnap fiddling is a hack. */
        gdouble const curr_gridsnap = desktop->namedview->grid_snapper.getDistance();
        desktop->namedview->grid_snapper.setDistance(NR_HUGE);
        namedview_free_snap(desktop->namedview, Snapper::SNAP_POINT, m);
        desktop->namedview->grid_snapper.setDistance(curr_gridsnap);
        sp_selection_move_relative(selection, m[NR::X], m[NR::Y]);
    }

    sp_document_done(SP_DT_DOCUMENT(desktop));
}

void sp_selection_paste_style()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == NULL) return;
    g_assert(SP_IS_DESKTOP(desktop));

    SPSelection *selection = SP_DT_SELECTION(desktop);

    // check if something is in the clipboard
    if (clipboard == NULL) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Nothing on the clipboard."));
        return;
    }

    // check if something is selected
    if (selection->isEmpty()) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select <b>object(s)</b> to paste style to."));
        return;
    }

    paste_defs (SP_DT_DOCUMENT(desktop));

    sp_desktop_set_style (desktop, style_clipboard);

    sp_document_done(SP_DT_DOCUMENT (desktop));
}

void sp_selection_apply_affine(SPSelection *selection, NR::Matrix const &affine, bool set_i2d)
{
    if (selection->isEmpty())
        return;

    for (GSList const *l = selection->itemList(); l != NULL; l = l-> next) {
        SPItem *item = SP_ITEM(l->data);

#if 0 /* Re-enable this once persistent guides have a graphical indication.
	 At the time of writing, this is the only place to re-enable. */
        sp_item_update_cns(*item, selection->desktop());
#endif

        // we're moving both a clone and its original
        bool move_clone_with_original = (affine.is_translation() && SP_IS_USE(item) && selection->includesItem( sp_use_get_original (SP_USE(item)) ));
        bool transform_textpath_with_path = (SP_IS_TEXT_TEXTPATH(item) && selection->includesItem( sp_textpath_get_path_item (SP_TEXTPATH(sp_object_first_child(SP_OBJECT(item)))) ));
        bool move_offset_with_source = (affine.is_translation() && (SP_IS_OFFSET(item) && SP_OFFSET (item)->sourceHref) && selection->includesItem( sp_offset_get_source (SP_OFFSET(item)) ));

        // "clones are unmoved when original is moved" preference
        bool prefs_unmoved = (prefs_get_int_attribute("options.clonecompensation", "value", SP_CLONE_COMPENSATION_PARALLEL) == SP_CLONE_COMPENSATION_UNMOVED);

	// If this is a clone and it's selected along with its original, do not move it; it will feel the
	// transform of its original and respond to it itself. Without this, a clone is doubly
	// transformed, very unintuitive.
      // Same for textpath if we are also doing ANY transform to its path: do not touch textpath,
      // letters cannot be squeezed or rotated anyway, they only refill the changed path.
      // Same for linked offset if we are also moving its source: do not move it.
        if ((move_clone_with_original && !prefs_unmoved) || transform_textpath_with_path || move_offset_with_source) {
		// just restore the transform field from the repr
            sp_object_read_attr (SP_OBJECT (item), "transform");
        } else {

            if (set_i2d) {
                sp_item_set_i2d_affine(item, sp_item_i2d_affine(item) * affine);
            }

            // we need to store this in a variable so that we can take a pointer
            NR::Matrix inv = (item->transform).inverse();

            // send inv as advertised transform if we're moving clone with original _and_ clones
            // compensation is set to unmoved - in this case we actually _want_ to move it (bug
            // 983568), so we're sending the inverse transform to balance out the compensation in
            // sp_use_move_compensate
            sp_item_write_transform(item, SP_OBJECT_REPR(item), item->transform, (move_clone_with_original && prefs_unmoved) ? &inv : NULL);
        }
    }
}

void sp_selection_remove_transform()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == NULL)
        return;

    SPSelection *selection = SP_DT_SELECTION(desktop);

    GSList const *l = (GSList *) selection->reprList();
    while (l != NULL) {
        sp_repr_set_attr((SPRepr*)l->data,"transform", NULL);
        l = l->next;
    }

    sp_document_done(SP_DT_DOCUMENT(desktop));
}

void
sp_selection_scale_absolute(SPSelection *selection,
                            double const x0, double const x1,
                            double const y0, double const y1)
{
    if (selection->isEmpty())
        return;

    NR::Rect const bbox(selection->bounds());

    NR::translate const p2o(-bbox.min());

    NR::scale const newSize(x1 - x0,
                            y1 - y0);
    NR::scale const scale( newSize / NR::scale(bbox.dimensions()) );
    NR::translate const o2n(x0, y0);
    NR::Matrix const final( p2o * scale * o2n );

    sp_selection_apply_affine(selection, final);
}


void sp_selection_scale_relative(SPSelection *selection, NR::Point const &align, NR::scale const &scale)
{
    if (selection->isEmpty())
        return;

    // don't try to scale above 1 Mpt, it won't display properly and will crash sooner or later anyway
    NR::Rect const bbox(selection->bounds());
    if ( bbox.extent(NR::X) * scale[NR::X] > 1e6  ||
         bbox.extent(NR::Y) * scale[NR::Y] > 1e6 )
    {
        return;
    }

    NR::translate const n2d(-align);
    NR::translate const d2n(align);
    NR::Matrix const final( n2d * scale * d2n );
    sp_selection_apply_affine(selection, final);
}

void
sp_selection_rotate_relative(SPSelection *selection, NR::Point const &center, gdouble const angle_degrees)
{
    NR::translate const d2n(center);
    NR::translate const n2d(-center);
    NR::rotate const rotate(rotate_degrees(angle_degrees));
    NR::Matrix const final( NR::Matrix(n2d) * rotate * d2n );
    sp_selection_apply_affine(selection, final);
}

void
sp_selection_skew_relative(SPSelection *selection, NR::Point const &align, double dx, double dy)
{
    NR::translate const d2n(align);
    NR::translate const n2d(-align);
    NR::Matrix const skew(1, dy,
                          dx, 1,
                          0, 0);
    NR::Matrix const final( n2d * skew * d2n );
    sp_selection_apply_affine(selection, final);
}

void sp_selection_move_relative(SPSelection *selection, double dx, double dy)
{
    sp_selection_apply_affine(selection, NR::Matrix(NR::translate(dx, dy)));
}


/**
 * \brief sp_selection_rotate_90
 *
 * This function rotates selected objects 90 degrees clockwise.
 *
 */

void sp_selection_rotate_90_cw()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (!SP_IS_DESKTOP(desktop))
        return;

    SPSelection *selection = SP_DT_SELECTION(desktop);

    if (selection->isEmpty())
        return;

    GSList const *l = selection->itemList();
    NR::rotate const rot_neg_90(NR::Point(0, -1));
    for (GSList const *l2 = l ; l2 != NULL ; l2 = l2->next) {
        SPItem *item = SP_ITEM(l2->data);
        sp_item_rotate_rel(item, rot_neg_90);
    }

    sp_document_done(SP_DT_DOCUMENT(desktop));
}


/**
 * \brief sp_selection_rotate_90_ccw
 *
 * This function rotates selected objects 90 degrees counter-clockwise.
 *
 */

void sp_selection_rotate_90_ccw()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (!SP_IS_DESKTOP(desktop))
        return;

    SPSelection *selection = SP_DT_SELECTION(desktop);

    if (selection->isEmpty())
        return;

    GSList const *l = selection->itemList();
    NR::rotate const rot_neg_90(NR::Point(0, 1));
    for (GSList const *l2 = l ; l2 != NULL ; l2 = l2->next) {
        SPItem *item = SP_ITEM(l2->data);
        sp_item_rotate_rel(item, rot_neg_90);
    }

    sp_document_done(SP_DT_DOCUMENT(desktop));
}

void
sp_selection_rotate(SPSelection *selection, gdouble const angle_degrees)
{
    if (selection->isEmpty())
        return;

    NR::Point const center(selection->bounds().midpoint());

    sp_selection_rotate_relative(selection, center, angle_degrees);

    sp_document_maybe_done(SP_DT_DOCUMENT(selection->desktop()),
                           ( ( angle_degrees > 0 )
                             ? "selector:rotate:ccw"
                             : "selector:rotate:cw" ));
}

/**
\param  angle   the angle in "angular pixels", i.e. how many visible pixels must move the outermost point of the rotated object
*/
void
sp_selection_rotate_screen(SPSelection *selection, gdouble angle)
{
    if (selection->isEmpty())
        return;

    NR::Rect const bbox(selection->bounds());
    NR::Point const center(bbox.midpoint());

    gdouble const zoom = SP_DESKTOP_ZOOM(selection->desktop());
    gdouble const zmove = angle / zoom;
    gdouble const r = NR::L2(bbox.max() - center);

    gdouble const zangle = 180 * atan2(zmove, r) / M_PI;

    sp_selection_rotate_relative(selection, center, zangle);

    sp_document_maybe_done(SP_DT_DOCUMENT(selection->desktop()),
                           ( (angle > 0)
                             ? "selector:rotate:ccw"
                             : "selector:rotate:cw" ));
}

void
sp_selection_scale(SPSelection *selection, gdouble grow)
{
    if (selection->isEmpty())
        return;

    NR::Rect const bbox(selection->bounds());
    NR::Point const center(bbox.midpoint());
    double const max_len = bbox.maxExtent();

    // you can't scale "do nizhe pola" (below zero)
    if ( max_len + grow <= 1e-3 ) {
        return;
    }

    double const times = 1.0 + grow / max_len;
    sp_selection_scale_relative(selection, center, NR::scale(times, times));

    sp_document_maybe_done(SP_DT_DOCUMENT(selection->desktop()),
                           ( (grow > 0)
                             ? "selector:scale:larger"
                             : "selector:scale:smaller" ));
}

void
sp_selection_scale_screen(SPSelection *selection, gdouble grow_pixels)
{
    sp_selection_scale(selection,
                       grow_pixels / SP_DESKTOP_ZOOM(selection->desktop()));
}

void
sp_selection_scale_times(SPSelection *selection, gdouble times)
{
    if (selection->isEmpty())
        return;

    NR::Point const center(selection->bounds().midpoint());
    sp_selection_scale_relative(selection, center, NR::scale(times, times));
    sp_document_done(SP_DT_DOCUMENT(selection->desktop()));
}

void
sp_selection_move(gdouble dx, gdouble dy)
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    g_return_if_fail(SP_IS_DESKTOP(desktop));
    SPSelection *selection = SP_DT_SELECTION(desktop);
    if (selection->isEmpty()) {
        return;
    }

    sp_selection_move_relative(selection, dx, dy);

    if (dx == 0) {
        sp_document_maybe_done(SP_DT_DOCUMENT(desktop), "selector:move:vertical");
    } else if (dy == 0) {
        sp_document_maybe_done(SP_DT_DOCUMENT(desktop), "selector:move:horizontal");
    } else {
        sp_document_done(SP_DT_DOCUMENT(desktop));
    }
}

void
sp_selection_move_screen(gdouble dx, gdouble dy)
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    g_return_if_fail(SP_IS_DESKTOP(desktop));

    SPSelection *selection = SP_DT_SELECTION(desktop);
    if (selection->isEmpty()) {
        return;
    }

    // same as sp_selection_move but divide deltas by zoom factor
    gdouble const zoom = SP_DESKTOP_ZOOM(desktop);
    gdouble const zdx = dx / zoom;
    gdouble const zdy = dy / zoom;
    sp_selection_move_relative(selection, zdx, zdy);

    if (dx == 0) {
        sp_document_maybe_done(SP_DT_DOCUMENT(desktop), "selector:move:vertical");
    } else if (dy == 0) {
        sp_document_maybe_done(SP_DT_DOCUMENT(desktop), "selector:move:horizontal");
    } else {
        sp_document_done(SP_DT_DOCUMENT(desktop));
    }
}

namespace {

template <typename D>
SPItem *next_item(SPDesktop *desktop, GSList *path, SPObject *root,
                  bool only_in_viewport, bool onlyvisible, bool onlysensitive);

template <typename D>
SPItem *next_item_from_list(SPDesktop *desktop, GSList const *items,
                            SPObject *root, bool only_in_viewport, bool onlyvisible, bool onlysensitive);

struct Forward {
    typedef SPObject *Iterator;

    static Iterator children(SPObject *o) { return sp_object_first_child(o); }
    static Iterator siblings_after(SPObject *o) { return SP_OBJECT_NEXT(o); }
    static void dispose(Iterator i) {}

    static SPObject *object(Iterator i) { return i; }
    static Iterator next(Iterator i) { return SP_OBJECT_NEXT(i); }
};

struct Reverse {
    typedef GSList *Iterator;

    static Iterator children(SPObject *o) {
        return make_list(o->firstChild(), NULL);
    }
    static Iterator siblings_after(SPObject *o) {
        return make_list(SP_OBJECT_PARENT(o)->firstChild(), o);
    }
    static void dispose(Iterator i) {
        g_slist_free(i);
    }

    static SPObject *object(Iterator i) {
        return reinterpret_cast<SPObject *>(i->data);
    }
    static Iterator next(Iterator i) { return i->next; }

private:
    static GSList *make_list(SPObject *object, SPObject *limit) {
        GSList *list=NULL;
        while ( object != limit ) {
            list = g_slist_prepend(list, object);
            object = SP_OBJECT_NEXT(object);
        }
        return list;
    }
};

}

void
sp_selection_item_next(void)
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    g_return_if_fail(desktop != NULL);
    if (!SP_IS_DESKTOP(desktop)) {
        return;
    }
    SPSelection *selection = SP_DT_SELECTION(desktop);

    bool inlayer = prefs_get_int_attribute ("options.kbselection", "inlayer", 1);
    bool onlyvisible = prefs_get_int_attribute ("options.kbselection", "onlyvisible", 1);
    bool onlysensitive = prefs_get_int_attribute ("options.kbselection", "onlysensitive", 1);

    SPObject *root;
    if (inlayer) {
        root = desktop->currentLayer();
    } else {
        root = desktop->currentRoot();
    }

    SPItem *item=next_item_from_list<Forward>(desktop, selection->itemList(), root, SP_CYCLING == SP_CYCLE_VISIBLE, onlyvisible, onlysensitive);

    if (item) {
        selection->setItem(item);
        if ( SP_CYCLING == SP_CYCLE_FOCUS ) {
            scroll_to_show_item(desktop, item);
        }
    }
}

void
sp_selection_item_prev(void)
{
    SPDocument *document = SP_ACTIVE_DOCUMENT;
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    g_return_if_fail(document != NULL);
    g_return_if_fail(desktop != NULL);
    if (!SP_IS_DESKTOP(desktop)) {
        return;
    }
    SPSelection *selection = SP_DT_SELECTION(desktop);

    bool inlayer = prefs_get_int_attribute ("options.kbselection", "inlayer", 1);
    bool onlyvisible = prefs_get_int_attribute ("options.kbselection", "onlyvisible", 1);
    bool onlysensitive = prefs_get_int_attribute ("options.kbselection", "onlysensitive", 1);

    SPObject *root;
    if (inlayer) {
        root = desktop->currentLayer();
    } else {
        root = desktop->currentRoot();
    }

    SPItem *item=next_item_from_list<Reverse>(desktop, selection->itemList(), root, SP_CYCLING == SP_CYCLE_VISIBLE, onlyvisible, onlysensitive);

    if (item) {
        selection->setItem(item);
        if ( SP_CYCLING == SP_CYCLE_FOCUS ) {
            scroll_to_show_item(desktop, item);
        }
    }
}

namespace {

template <typename D>
SPItem *next_item_from_list(SPDesktop *desktop, GSList const *items,
                            SPObject *root, bool only_in_viewport, bool onlyvisible, bool onlysensitive)
{
    SPObject *current=root;
    while (items) {
        SPItem *item=SP_ITEM(items->data);
        if ( root->isAncestorOf(item) &&
             ( !only_in_viewport || desktop->isWithinViewport(item) ) )
        {
            current = item;
            break;
        }
        items = items->next;
    }

    GSList *path=NULL;
    while ( current != root ) {
        path = g_slist_prepend(path, current);
        current = SP_OBJECT_PARENT(current);
    }

    SPItem *next;
    // first, try from the current object
    next = next_item<D>(desktop, path, root, only_in_viewport, onlyvisible, onlysensitive);
    g_slist_free(path);

    if (!next) { // if we ran out of objects, start over at the root
        next = next_item<D>(desktop, NULL, root, only_in_viewport, onlyvisible, onlysensitive);
    }

    return next;
}

template <typename D>
SPItem *next_item(SPDesktop *desktop, GSList *path, SPObject *root,
                  bool only_in_viewport, bool onlyvisible, bool onlysensitive)
{
    typename D::Iterator children;
    typename D::Iterator iter;

    SPItem *found=NULL;

    if (path) {
        SPObject *object=reinterpret_cast<SPObject *>(path->data);
        g_assert(SP_OBJECT_PARENT(object) == root);
        if (desktop->isLayer(object)) {
            found = next_item<D>(desktop, path->next, object, only_in_viewport, onlyvisible, onlysensitive);
        }
        iter = children = D::siblings_after(object);
    } else {
        iter = children = D::children(root);
    }

    while ( iter && !found ) {
        SPObject *object=D::object(iter);
        if (desktop->isLayer(object)) {
            found = next_item<D>(desktop, NULL, object, only_in_viewport, onlyvisible, onlysensitive);
        } else if ( SP_IS_ITEM(object) &&
                    ( !only_in_viewport || desktop->isWithinViewport(SP_ITEM(object)) ) &&
                    ( !onlyvisible || !desktop->itemIsHidden(SP_ITEM(object))) &&
                    ( !onlysensitive || !SP_ITEM(object)->isLocked()) &&
                    !desktop->isLayer(SP_ITEM(object)) )
        {
            found = SP_ITEM(object);
        }
        iter = D::next(iter);
    }

    D::dispose(children);

    return found;
}

}

/**
 * If \a item is not entirely visible then adjust visible area to centre on the centre on of
 * \a item.
 */
void scroll_to_show_item(SPDesktop *desktop, SPItem *item)
{
    NRRect dbox;
    sp_desktop_get_display_area(desktop, &dbox);
    NRRect sbox;
    sp_item_bbox_desktop(item, &sbox);
    if ( dbox.x0 > sbox.x0  ||
         dbox.y0 > sbox.y0  ||
         dbox.x1 < sbox.x1  ||
         dbox.y1 < sbox.y1 )
    {
        NR::Point const s_dt(( sbox.x0 + sbox.x1 ) / 2,
                             ( sbox.y0 + sbox.y1 ) / 2);
        NR::Point const s_w( s_dt * desktop->d2w );
        NR::Point const d_dt(( dbox.x0 + dbox.x1 ) / 2,
                             ( dbox.y0 + dbox.y1 ) / 2);
        NR::Point const d_w( d_dt * desktop->d2w );
        NR::Point const moved_w( d_w - s_w );
        gint const dx = (gint) moved_w[X];
        gint const dy = (gint) moved_w[Y];
        sp_desktop_scroll_world(desktop, dx, dy);
    }
}


void
sp_selection_clone()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == NULL)
        return;

    SPSelection *selection = SP_DT_SELECTION(desktop);

    // check if something is selected
    if (selection->isEmpty()) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select an <b>object</b> to clone."));
        return;
    }

    // Check if more than one object is selected.
    if (g_slist_length((GSList *) selection->itemList()) > 1) {
        desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("If you want to clone several objects, <b>group</b> them and <b>clone the group</b>."));
        return;
    }

    SPRepr *sel_repr = SP_OBJECT_REPR(selection->singleItem());
    SPRepr *parent = sp_repr_parent(sel_repr);

    SPRepr *clone = sp_repr_new("use");
    sp_repr_set_attr(clone, "x", "0");
    sp_repr_set_attr(clone, "y", "0");
    sp_repr_set_attr(clone, "xlink:href", g_strdup_printf("#%s", sp_repr_attr(sel_repr, "id")));

    // add the new clone to the top of the original's parent
    sp_repr_append_child(parent, clone);

    sp_document_done(SP_DT_DOCUMENT(desktop));

    selection->setRepr(clone);
    sp_repr_unref(clone);
}

void
sp_selection_unlink()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (!desktop)
        return;

    SPSelection *selection = SP_DT_SELECTION(desktop);

    if (selection->isEmpty()) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select a <b>clone</b> to unlink."));
        return;
    }

    // Get a copy of current selection.
    GSList *new_select = NULL;
    bool unlinked = false;
    for (GSList *items = g_slist_copy((GSList *) selection->itemList());
         items != NULL;
         items = items->next)
    {
        SPItem *use = (SPItem *) items->data;

        if (!SP_IS_USE(use)) {
            // keep the non-yse item in the new selection
            new_select = g_slist_prepend(new_select, use);
            continue;
        }

        SPItem *unlink = sp_use_unlink(SP_USE(use));
        unlinked = true;
        // Add ungrouped items to the new selection.
        new_select = g_slist_prepend(new_select, unlink);
    }

    if (new_select) { // set new selection
        selection->clear();
        selection->setItemList(new_select);
        g_slist_free(new_select);
    }
    if (!unlinked) {
        desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("<b>No clones to unlink</b> in the selection."));
    }

    sp_document_done(SP_DT_DOCUMENT(desktop));
}

void
sp_select_clone_original()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == NULL)
        return;

    SPSelection *selection = SP_DT_SELECTION(desktop);

    SPItem *item = selection->singleItem();

    const gchar *error = _("Select a <b>clone</b> to go to its original. Select a <b>linked offset</b> to go to its source. Select a <b>text on path</b> to go to the path.");

    // Check if other than two objects are selected
    if (g_slist_length((GSList *) selection->itemList()) != 1 || !item) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, error);
        return;
    }

    SPItem *original = NULL;
    if (SP_IS_USE(item)) {
        original = sp_use_get_original (SP_USE(item));
    } else if (SP_IS_OFFSET(item) && SP_OFFSET (item)->sourceHref) {
        original = sp_offset_get_source (SP_OFFSET(item));
    } else if (SP_IS_TEXT_TEXTPATH(item)) {
        original = sp_textpath_get_path_item (SP_TEXTPATH(sp_object_first_child(SP_OBJECT(item))));
    } else { // it's an object that we don't know what to do with
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, error);
        return;
    }

    if (!original) {
        desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("<b>Cannot find</b> the object to select (orphaned clone, offset, or textpath?)"));
        return;
    }

    for (SPObject *o = original; o && !SP_IS_ROOT(o); o = SP_OBJECT_PARENT (o)) {
        if (SP_IS_DEFS (o)) {
            desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("The object you're trying to select is <b>not visible</b> (it is in &lt;defs&gt;)"));
            return;
        }
    }

    if (original) {
        selection->clear();
        selection->setItem(original);
        if (SP_CYCLING == SP_CYCLE_FOCUS) {
            scroll_to_show_item(desktop, original);
        }
    }
}

void
sp_selection_tile(bool apply)
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == NULL)
        return;

    SPDocument *document = SP_DT_DOCUMENT(desktop);

    SPSelection *selection = SP_DT_SELECTION(desktop);

    // check if something is selected
    if (selection->isEmpty()) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select <b>object(s)</b> to convert to pattern."));
        return;
    }

    // calculate the transform to be applied to objects to move them to 0,0
    sp_document_ensure_up_to_date(document);
    NR::Rect r = selection->bounds();
    NR::Point move_p = NR::Point(0, sp_document_height(document)) - (r.min() + NR::Point (0, r.extent(NR::Y)));
    move_p[NR::Y] = -move_p[NR::Y];
    move_p *= 1.25;
    NR::Matrix move = NR::Matrix (NR::translate (move_p));

    GSList *reprs = g_slist_copy((GSList *) selection->reprList());

    SPRepr *parent = ((SPRepr *) reprs->data)->parent;
    gboolean sort = TRUE;
    for (GSList *i = reprs->next; i; i = i->next) {
        if ((((SPRepr *) i->data)->parent) != parent) {
            // We can tile items from different parents, but we cannot do sorting in this case
            sort = FALSE;
        }
    }

    if (sort)
        reprs = g_slist_sort(reprs, (GCompareFunc) sp_repr_compare_position);

    // remember the position of the first item
    gint pos = sp_repr_position ((SPRepr *) reprs->data);

    // create a list of duplicates
    GSList *repr_copies = NULL;
    for (GSList *i = reprs; i != NULL; i = i->next) {
        SPRepr *dup = sp_repr_duplicate (((SPRepr *) i->data));
        repr_copies = g_slist_prepend (repr_copies, dup);
    }
    
    NR::Rect bounds (sp_desktop_d2doc_xy_point(desktop, r.min()), sp_desktop_d2doc_xy_point(desktop, r.max()));

    const gchar *pat_id = pattern_tile (repr_copies, bounds, document, 
                                 NR::Matrix(NR::translate(sp_desktop_d2doc_xy_point (desktop, NR::Point(r.min()[NR::X], r.max()[NR::Y])))), move);

    if (apply) {
        // delete objects so that their clones don't get alerted; this object will be restored shortly 
        for (GSList *i = reprs; i != NULL; i = i->next) {
            SPObject *item = document->getObjectByRepr(((SPRepr *) i->data));
            item->deleteObject (false);
        }

        SPRepr *rect = sp_repr_new ("rect");
        sp_repr_set_attr (rect, "style", g_strdup_printf("stroke:none;fill:url(#%s)", pat_id));
        sp_repr_set_double (rect, "width", bounds.extent(NR::X));
        sp_repr_set_double (rect, "height", bounds.extent(NR::Y));
        sp_repr_set_double (rect, "x", bounds.min()[NR::X]);
        sp_repr_set_double (rect, "y", bounds.min()[NR::Y]);

        SPItem *rectangle = NULL;    
        if (sort) { // restore parent and position
            sp_repr_append_child (parent, rect);
            sp_repr_set_position_absolute (rect, pos > 0 ? pos : 0);
            rectangle = (SPItem *) SP_DT_DOCUMENT (desktop)->getObjectByRepr(rect);
        } else { // just add to the current layer
            rectangle = SP_ITEM(desktop->currentLayer()->appendChildRepr(rect));
        } 

        sp_repr_unref (rect);

        selection->clear();
        selection->setItem (rectangle);
    }

    sp_document_done (document);
}

void
sp_selection_untile()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == NULL)
        return;

    SPDocument *document = SP_DT_DOCUMENT(desktop);

    SPSelection *selection = SP_DT_SELECTION(desktop);

    // check if something is selected
    if (selection->isEmpty()) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select an <b>object with pattern fill</b> to extract objects from."));
        return;
    }

    GSList *new_select = NULL;

    bool did = false;

    for (GSList *items = g_slist_copy((GSList *) selection->itemList());
         items != NULL;
         items = items->next) {

        SPItem *item = (SPItem *) items->data;

        SPStyle *style = SP_OBJECT_STYLE (item);

        if (!style || style->fill.type != SP_PAINT_TYPE_PAINTSERVER) 
            continue;

        SPObject *server = SP_OBJECT_STYLE_FILL_SERVER(item);

        if (!SP_IS_PATTERN(server))
            continue;

        did = true;

        SPPattern *pattern = pattern_getroot (SP_PATTERN (server));

        NR::Matrix pat_transform = pattern_patternTransform (SP_PATTERN (server));
        pat_transform *= item->transform;

        for (SPObject *child = sp_object_first_child(SP_OBJECT(pattern)) ; child != NULL; child = SP_OBJECT_NEXT(child) ) {
            SPRepr *copy = sp_repr_duplicate (SP_OBJECT_REPR(child));
            SPItem *i = SP_ITEM (desktop->currentLayer()->appendChildRepr(copy));

           // FIXME: relink clones to the new canvas objects
           // use SPObject::setid when mental finishes it to steal ids of 

            // this is needed to make sure the new item has curve (simply requestDisplayUpdate does not work)
            sp_document_ensure_up_to_date (document);

            NR::Matrix transform( i->transform * pat_transform );
            sp_item_write_transform(i, SP_OBJECT_REPR(i), transform);

            new_select = g_slist_prepend(new_select, i);
        }

        SPCSSAttr *css = sp_repr_css_attr_new ();
        sp_repr_css_set_property (css, "fill", "none");
        sp_repr_css_change (SP_OBJECT_REPR (item), css, "style");
    }

    if (!did) {
        desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("<b>No pattern fills</b> in the selection."));
    } else {
        sp_document_done(SP_DT_DOCUMENT(desktop));
        selection->setItemList (new_select);
    }
}

void
sp_selection_create_bitmap_copy ()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (desktop == NULL)
        return;

    SPDocument *document = SP_DT_DOCUMENT(desktop);

    SPSelection *selection = SP_DT_SELECTION(desktop);

    // check if something is selected
    if (selection->isEmpty()) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select <b>object(s)</b> to make a bitmap copy."));
        return;
    }

    // List of the items to show; all others will be hidden
    GSList *items = g_slist_copy ((GSList *) selection->itemList());

    // Generate a random value from the current time (you may create bitmap from the same object(s)
    // multiple times, and this is done so that they don't clash)
    GTimeVal cu;
    g_get_current_time (&cu);
    guint current = (int) (cu.tv_sec * 1000000 + cu.tv_usec) % 1024; 

    // Create the filename
    gchar *filename = g_strdup_printf ("%s-%s-%u.png", document->name, sp_repr_attr (SP_OBJECT_REPR(items->data), "id"), current);
    // Imagemagick is known not to handle spaces in filenames, so we replace anything but letters,
    // digits, and a few other chars, with "_"
    filename = g_strcanon (filename, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.=+~$#@^&!?", '_');
    // Build the complete path by adding document->base if set
    gchar *filepath = g_build_filename (document->base?document->base:"", filename, NULL);

    //g_print ("%s\n", filepath);

    // Get the bounding box of the selection
    NRRect bbox;
    sp_document_ensure_up_to_date (document);
    selection->bounds(&bbox);

    // Calculate resolution
    double res;
    int const prefs_res = prefs_get_int_attribute ("options.createbitmap", "resolution", 0);
    if (0 < prefs_res) {
        // If it's given explicitly in prefs, take it
        res = prefs_res;
    } else {
        // Otherwise look up minimum bitmap size (default 250 pixels) and calculate resolution from it
        int const prefs_min = prefs_get_int_attribute ("options.createbitmap", "minsize", 250);
        res = prefs_min / MIN ((bbox.x1 - bbox.x0), (bbox.y1 - bbox.y0));
    }

    // The width and height of the bitmap in pixels
    int width = (int) floor ((bbox.x1 - bbox.x0) * res);
    int height =(int) floor ((bbox.y1 - bbox.y0) * res);

    // Find out if we have to run a filter
    const gchar *run = NULL;
    const gchar *filter = prefs_get_string_attribute ("options.createbitmap", "filter");
    if (filter) {
        // filter command is given;
        // see if we have a parameter to pass to it
        const gchar *param1 = prefs_get_string_attribute ("options.createbitmap", "filter_param1");
        if (param1) {
            if (param1[strlen(param1) - 1] == '%') {
                // if the param string ends with %, interpret it as a percentage of the image's max dimension
                gchar p1[256];
                g_ascii_dtostr (p1, 256, ceil (g_ascii_strtod (param1, NULL) * MAX(width, height) / 100));
                // the first param is always the image filename, the second is param1
                run = g_strdup_printf ("%s \"%s\" %s", filter, filepath, p1);
            } else {
                // otherwise pass the param1 unchanged
                run = g_strdup_printf ("%s \"%s\" %s", filter, filepath, param1);
            }
        } else {
            // run without extra parameter
            run = g_strdup_printf ("%s \"%s\"", filter, filepath);
        }
    }


    // Calculate the matrix that will be applied to the image so that it exactly overlaps the source objects
    NR::Matrix eek = NR::scale(0.8, -0.8) * NR::translate(0, sp_document_height(document));
    NR::Matrix t = NR::scale (1/res, -1/res) * NR::translate (bbox.x0, bbox.y1) * eek.inverse(); 

    // Do the export
    sp_export_png_file(document, filepath,
                   bbox.x0, bbox.y0, bbox.x1, bbox.y1,
                   width, height,
                   (guint32) 0xffffff00,
                   NULL, NULL,
                   true,  /*bool force_overwrite,*/
                   items);

    g_slist_free (items);

    // Run filter, if any
    if (run) {
        g_print ("Running external filter: %s\n", run);
        system (run);
    }

    // Import the image back
    GdkPixbuf *pb = gdk_pixbuf_new_from_file (filepath, NULL);
    if (pb) {
        // Create the repr for the image
        SPRepr * repr = sp_repr_new ("image");
        sp_repr_set_attr (repr, "xlink:href", filename);
        sp_repr_set_attr (repr, "sodipodi:absref", filepath);
        sp_repr_set_double (repr, "width", gdk_pixbuf_get_width (pb));
        sp_repr_set_double (repr, "height", gdk_pixbuf_get_height (pb));

        // Write transform
        gchar c[256];
        if (sp_svg_transform_write(c, 256, t)) {
            sp_repr_set_attr(repr, "transform", c);
        } 

        // Add it to the current layer
        desktop->currentLayer()->appendChildRepr(repr);

        // Set selection to the new image
        selection->clear();
        selection->addRepr(repr);

        // Clean up
        sp_repr_unref (repr);
        gdk_pixbuf_unref (pb);

        // Complete undoable transaction
        sp_document_done (document);
    }

    g_free (filename);
    g_free (filepath);
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
