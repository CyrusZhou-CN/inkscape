#define __CURVE_C__

/** \file
 * Routines for SPCurve and for NArtBpath arrays generally.
 */

/*
 * Author:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 2000 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 * Copyright (C) 2002 Lauris Kaplinski
 *
 * Released under GNU GPL
 */

#include <math.h>
#include <string.h>
#include <glib/gmessages.h>

#include <display/curve.h>
#include <libnr/n-art-bpath.h>
#include <libnr/nr-point.h>
#include <libnr/nr-path.h>
#include <libnr/nr-macros.h>
#include <libnr/nr-matrix-ops.h>
#include <libnr/nr-point-fns.h>
#include <libnr/nr-translate-ops.h>

#define SP_CURVE_LENSTEP 32

static bool sp_bpath_good(NArtBpath const bpath[]);
static NArtBpath *sp_bpath_clean(NArtBpath const bpath[]);
static NArtBpath const *sp_bpath_check_subpath(NArtBpath const bpath[]);
static unsigned sp_bpath_length(NArtBpath const bpath[]);
static bool sp_bpath_closed(NArtBpath const bpath[]);

/* Constructors */

SPCurve *
sp_curve_new()
{
    return sp_curve_new_sized(SP_CURVE_LENSTEP);
}

SPCurve *
sp_curve_new_sized(gint length)
{
    g_return_val_if_fail(length > 0, NULL);

    SPCurve *curve = g_new(SPCurve, 1);

    curve->refcount = 1;
    curve->bpath = nr_new(NArtBpath, length);
    curve->bpath->code = NR_END;
    curve->end = 0;
    curve->length = length;
    curve->substart = 0;
    curve->sbpath = FALSE;
    curve->hascpt = FALSE;
    curve->posset = FALSE;
    curve->moving = FALSE;
    curve->closed = FALSE;

    return curve;
}

/**
* \return new SPCurve, or NULL if the curve was not created for some reason.
*/
SPCurve *
sp_curve_new_from_bpath(NArtBpath *bpath)
{
    g_return_val_if_fail(bpath != NULL, NULL);

    if (!sp_bpath_good(bpath)) {
        NArtBpath *new_bpath = sp_bpath_clean(bpath);
        if (new_bpath == NULL) {
            return NULL;
        }
        nr_free(bpath);
        bpath = new_bpath;
    }

    SPCurve *curve = g_new(SPCurve, 1);

    curve->refcount = 1;
    curve->bpath = bpath;
    curve->length = sp_bpath_length(bpath);
    curve->end = curve->length - 1;
    gint i = curve->end;
    for (; i > 0; i--)
        if ((curve->bpath[i].code == NR_MOVETO) ||
            (curve->bpath[i].code == NR_MOVETO_OPEN))
            break;
    curve->substart = i;
    curve->sbpath = FALSE;
    curve->hascpt = FALSE;
    curve->posset = FALSE;
    curve->moving = FALSE;
    curve->closed = sp_bpath_closed(bpath);

    return curve;
}

SPCurve *
sp_curve_new_from_static_bpath(NArtBpath const *bpath)
{
    g_return_val_if_fail(bpath != NULL, NULL);

    bool sbpath;
    if (!sp_bpath_good(bpath)) {
        NArtBpath *new_bpath = sp_bpath_clean(bpath);
        g_return_val_if_fail(new_bpath != NULL, NULL);
        sbpath = false;
        bpath = new_bpath;
    } else {
        sbpath = true;
    }

    SPCurve *curve = g_new(SPCurve, 1);

    curve->refcount = 1;
    curve->bpath = const_cast<NArtBpath *>(bpath);
    curve->length = sp_bpath_length(bpath);
    curve->end = curve->length - 1;
    gint i = curve->end;
    for (; i > 0; i--)
        if ((curve->bpath[i].code == NR_MOVETO) ||
            (curve->bpath[i].code == NR_MOVETO_OPEN))
            break;
    curve->substart = i;
    curve->sbpath = sbpath;
    curve->hascpt = FALSE;
    curve->posset = FALSE;
    curve->moving = FALSE;
    curve->closed = sp_bpath_closed(bpath);

    return curve;
}

SPCurve *sp_curve_new_from_foreign_bpath(NArtBpath const bpath[])
{
    g_return_val_if_fail(bpath != NULL, NULL);

    NArtBpath *new_bpath;
    if (!sp_bpath_good(bpath)) {
        new_bpath = sp_bpath_clean(bpath);
        g_return_val_if_fail(new_bpath != NULL, NULL);
    } else {
        unsigned const len = sp_bpath_length(bpath);
        new_bpath = nr_new(NArtBpath, len);
        memcpy(new_bpath, bpath, len * sizeof(NArtBpath));
    }

    SPCurve *curve = sp_curve_new_from_bpath(new_bpath);

    if (!curve)
        nr_free(new_bpath);

    return curve;
}

SPCurve *
sp_curve_ref(SPCurve *curve)
/* should this be shared with other refcounting code? */
{
    g_return_val_if_fail(curve != NULL, NULL);

    curve->refcount += 1;

    return curve;
}

SPCurve *
sp_curve_unref(SPCurve *curve)
/* should this be shared with other refcounting code? */
{
    g_return_val_if_fail(curve != NULL, NULL);

    curve->refcount -= 1;

    if (curve->refcount < 1) {
        if ((!curve->sbpath) && (curve->bpath)) {
            nr_free(curve->bpath);
        }
        g_free(curve);
    }

    return NULL;
}


void
sp_curve_finish(SPCurve *curve)
{
    g_return_if_fail(curve != NULL);
    g_return_if_fail(curve->sbpath);

    if (curve->end > 0) {
        NArtBpath *bp = curve->bpath + curve->end - 1;
        if (bp->code == NR_LINETO) {
            curve->end--;
            bp->code = NR_END;
        }
    }

    if (curve->end < (curve->length - 1)) {
        curve->bpath = nr_renew(curve->bpath, NArtBpath, curve->end);
    }

    curve->hascpt = FALSE;
    curve->posset = FALSE;
    curve->moving = FALSE;
}

void
sp_curve_ensure_space(SPCurve *curve, gint space)
{
    g_return_if_fail(curve != NULL);
    g_return_if_fail(space > 0);

    if (curve->end + space < curve->length)
        return;

    if (space < SP_CURVE_LENSTEP)
        space = SP_CURVE_LENSTEP;

    curve->bpath = nr_renew(curve->bpath, NArtBpath, curve->length + space);

    curve->length += space;
}

SPCurve *
sp_curve_copy(SPCurve *curve)
{
    g_return_val_if_fail(curve != NULL, NULL);

    return sp_curve_new_from_foreign_bpath(curve->bpath);
}

SPCurve *
sp_curve_concat(GSList const *list)
{
    g_return_val_if_fail(list != NULL, NULL);

    gint length = 0;

    for (GSList const *l = list; l != NULL; l = l->next) {
        SPCurve *c = (SPCurve *) l->data;
        length += c->end;
    }

    SPCurve *new_curve = sp_curve_new_sized(length + 1);

    NArtBpath *bp = new_curve->bpath;

    for (GSList const *l = list; l != NULL; l = l->next) {
        SPCurve *c = (SPCurve *) l->data;
        memcpy(bp, c->bpath, c->end * sizeof(NArtBpath));
        bp += c->end;
    }

    bp->code = NR_END;

    new_curve->end = length;
    gint i;
    for (i = new_curve->end; i > 0; i--) {
        if ((new_curve->bpath[i].code == NR_MOVETO)     ||
            (new_curve->bpath[i].code == NR_MOVETO_OPEN)  )
            break;
    }

    new_curve->substart = i;

    return new_curve;
}

GSList *
sp_curve_split(SPCurve *curve)
{
    g_return_val_if_fail(curve != NULL, NULL);

    gint p = 0;
    GSList *l = NULL;

    while (p < curve->end) {
        gint i = 1;
        while ((curve->bpath[p + i].code == NR_LINETO) ||
               (curve->bpath[p + i].code == NR_CURVETO))
            i++;
        SPCurve *new_curve = sp_curve_new_sized(i + 1);
        memcpy(new_curve->bpath, curve->bpath + p, i * sizeof(NArtBpath));
        new_curve->end = i;
        new_curve->bpath[i].code = NR_END;
        new_curve->substart = 0;
        new_curve->closed = (new_curve->bpath->code == NR_MOVETO);
        new_curve->hascpt = (new_curve->bpath->code == NR_MOVETO_OPEN);
        l = g_slist_append(l, new_curve);
        p += i;
    }

    return l;
}

template<class M>
static void
tmpl_curve_transform(SPCurve *const curve, M const &m)
{
    g_return_if_fail(curve != NULL);
    g_return_if_fail(!curve->sbpath);

    for (gint i = 0; i < curve->end; i++) {
        NArtBpath *p = curve->bpath + i;
        switch (p->code) {
            case NR_MOVETO:
            case NR_MOVETO_OPEN:
            case NR_LINETO: {
                p->setC(3, p->c(3) * m);
                break;
            }
            case NR_CURVETO:
                for (unsigned i = 1; i <= 3; ++i) {
                    p->setC(i, p->c(i) * m);
                }
                break;
            default:
                g_warning("Illegal pathcode %d", p->code);
                break;
        }
    }
}

void
sp_curve_transform(SPCurve *const curve, NR::Matrix const &m)
{
    tmpl_curve_transform<NR::Matrix>(curve, m);
}

void
sp_curve_transform(SPCurve *const curve, NR::translate const &m)
{
    tmpl_curve_transform<NR::translate>(curve, m);
}


/* Methods */

void
sp_curve_reset(SPCurve *curve)
{
    g_return_if_fail(curve != NULL);
    g_return_if_fail(!curve->sbpath);

    curve->bpath->code = NR_END;
    curve->end = 0;
    curve->substart = 0;
    curve->hascpt = FALSE;
    curve->posset = FALSE;
    curve->moving = FALSE;
    curve->closed = FALSE;
}

/* Several consecutive movetos are ALLOWED */

void
sp_curve_moveto(SPCurve *curve, gdouble x, gdouble y)
{
    sp_curve_moveto(curve, NR::Point(x, y));
}

void
sp_curve_moveto(SPCurve *curve, NR::Point const &p)
{
    g_return_if_fail(curve != NULL);
    g_return_if_fail(!curve->sbpath);
    g_return_if_fail(!curve->moving);

    curve->substart = curve->end;
    curve->hascpt = TRUE;
    curve->posset = TRUE;
    curve->movePos = p;
}

void
sp_curve_lineto(SPCurve *curve, NR::Point const &p)
{
    sp_curve_lineto(curve, p[NR::X], p[NR::Y]);
}

void
sp_curve_lineto(SPCurve *curve, gdouble x, gdouble y)
{
    g_return_if_fail(curve != NULL);
    g_return_if_fail(!curve->sbpath);
    g_return_if_fail(curve->hascpt);

    if (curve->moving) {
        /* fix endpoint */
        g_return_if_fail(!curve->posset);
        g_return_if_fail(curve->end > 1);
        NArtBpath *bp = curve->bpath + curve->end - 1;
        g_return_if_fail(bp->code == NR_LINETO);
        bp->x3 = x;
        bp->y3 = y;
        curve->moving = FALSE;
        return;
    }

    if (curve->posset) {
        /* start a new segment */
        sp_curve_ensure_space(curve, 2);
        NArtBpath *bp = curve->bpath + curve->end;
        bp->code = NR_MOVETO_OPEN;
        bp->setC(3, curve->movePos);
        bp++;
        bp->code = NR_LINETO;
        bp->x3 = x;
        bp->y3 = y;
        bp++;
        bp->code = NR_END;
        curve->end += 2;
        curve->posset = FALSE;
        curve->closed = FALSE;
        return;
    }

    /* add line */

    g_return_if_fail(curve->end > 1);
    sp_curve_ensure_space(curve, 1);
    NArtBpath *bp = curve->bpath + curve->end;
    bp->code = NR_LINETO;
    bp->x3 = x;
    bp->y3 = y;
    bp++;
    bp->code = NR_END;
    curve->end++;
}

void
sp_curve_lineto_moving(SPCurve *curve, gdouble x, gdouble y)
{
    g_return_if_fail(curve != NULL);
    g_return_if_fail(!curve->sbpath);
    g_return_if_fail(curve->hascpt);

    if (curve->moving) {
        /* change endpoint */
        g_return_if_fail(!curve->posset);
        g_return_if_fail(curve->end > 1);
        NArtBpath *bp = curve->bpath + curve->end - 1;
        g_return_if_fail(bp->code == NR_LINETO);
        bp->x3 = x;
        bp->y3 = y;
        return;
    }

    if (curve->posset) {
        /* start a new segment */
        sp_curve_ensure_space(curve, 2);
        NArtBpath *bp = curve->bpath + curve->end;
        bp->code = NR_MOVETO_OPEN;
        bp->setC(3, curve->movePos);
        bp++;
        bp->code = NR_LINETO;
        bp->x3 = x;
        bp->y3 = y;
        bp++;
        bp->code = NR_END;
        curve->end += 2;
        curve->posset = FALSE;
        curve->moving = TRUE;
        curve->closed = FALSE;
        return;
    }

    /* add line */

    g_return_if_fail(curve->end > 1);
    sp_curve_ensure_space(curve, 1);
    NArtBpath *bp = curve->bpath + curve->end;
    bp->code = NR_LINETO;
    bp->x3 = x;
    bp->y3 = y;
    bp++;
    bp->code = NR_END;
    curve->end++;
    curve->moving = TRUE;
}

void
sp_curve_curveto(SPCurve *curve, NR::Point const &p0, NR::Point const &p1, NR::Point const &p2)
{
    using NR::X;
    using NR::Y;
    sp_curve_curveto(curve,
                     p0[X], p0[Y],
                     p1[X], p1[Y],
                     p2[X], p2[Y]);
}

void
sp_curve_curveto(SPCurve *curve, gdouble x0, gdouble y0, gdouble x1, gdouble y1, gdouble x2, gdouble y2)
{
    g_return_if_fail(curve != NULL);
    g_return_if_fail(!curve->sbpath);
    g_return_if_fail(curve->hascpt);
    g_return_if_fail(!curve->moving);

    if (curve->posset) {
        /* start a new segment */
        sp_curve_ensure_space(curve, 2);
        NArtBpath *bp = curve->bpath + curve->end;
        bp->code = NR_MOVETO_OPEN;
        bp->setC(3, curve->movePos);
        bp++;
        bp->code = NR_CURVETO;
        bp->x1 = x0;
        bp->y1 = y0;
        bp->x2 = x1;
        bp->y2 = y1;
        bp->x3 = x2;
        bp->y3 = y2;
        bp++;
        bp->code = NR_END;
        curve->end += 2;
        curve->posset = FALSE;
        curve->closed = FALSE;
        return;
    }

    /* add curve */

    g_return_if_fail(curve->end > 1);
    sp_curve_ensure_space(curve, 1);
    NArtBpath *bp = curve->bpath + curve->end;
    bp->code = NR_CURVETO;
    bp->x1 = x0;
    bp->y1 = y0;
    bp->x2 = x1;
    bp->y2 = y1;
    bp->x3 = x2;
    bp->y3 = y2;
    bp++;
    bp->code = NR_END;
    curve->end++;
}

void
sp_curve_closepath(SPCurve *curve)
{
    g_return_if_fail(curve != NULL);
    g_return_if_fail(!curve->sbpath);
    g_return_if_fail(curve->hascpt);
    g_return_if_fail(!curve->posset);
    g_return_if_fail(!curve->moving);
    g_return_if_fail(!curve->closed);
    /* We need at least moveto, curveto, end. */
    g_return_if_fail(curve->end - curve->substart > 1);

    {
        NArtBpath *bs = curve->bpath + curve->substart;
        NArtBpath *be = curve->bpath + curve->end - 1;

        if (bs->c(3) != be->c(3)) {
            sp_curve_lineto(curve, bs->c(3));
        }

        bs->code = NR_MOVETO;
    }
    curve->closed = TRUE;

    for (NArtBpath const *bp = curve->bpath; bp->code != NR_END; bp++) {
        /* effic: Maintain a count of NR_MOVETO_OPEN's (e.g. instead of the closed boolean). */
        if (bp->code == NR_MOVETO_OPEN) {
            curve->closed = FALSE;
            break;
        }
    }

    curve->hascpt = FALSE;
}

/** Like sp_curve_closepath but sets the end point of the current
    command to the subpath start point instead of adding a new lineto.

    Used for freehand drawing when the user draws back to the start point.
**/
void
sp_curve_closepath_current(SPCurve *curve)
{
    g_return_if_fail(curve != NULL);
    g_return_if_fail(!curve->sbpath);
    g_return_if_fail(curve->hascpt);
    g_return_if_fail(!curve->posset);
    g_return_if_fail(!curve->closed);
    /* We need at least moveto, curveto, end. */
    g_return_if_fail(curve->end - curve->substart > 1);

    {
        NArtBpath *bs = curve->bpath + curve->substart;
        NArtBpath *be = curve->bpath + curve->end - 1;

        be->x3 = bs->x3;
        be->y3 = bs->y3;

        bs->code = NR_MOVETO;
    }
    curve->closed = TRUE;

    for (NArtBpath const *bp = curve->bpath; bp->code != NR_END; bp++) {
        /* effic: Maintain a count of NR_MOVETO_OPEN's (e.g. instead of the closed boolean). */
        if (bp->code == NR_MOVETO_OPEN) {
            curve->closed = FALSE;
            break;
        }
    }

    curve->hascpt = FALSE;
    curve->moving = FALSE;
}

gboolean
sp_curve_empty(SPCurve *curve)
{
    g_return_val_if_fail(curve != NULL, TRUE);

    return (curve->bpath->code == NR_END);
}

NArtBpath *
sp_curve_last_bpath(SPCurve const *curve)
{
    g_return_val_if_fail(curve != NULL, NULL);

    if (curve->end == 0) {
        return NULL;
    }

    return curve->bpath + curve->end - 1;
}

NArtBpath *
sp_curve_first_bpath(SPCurve const *curve)
{
    g_return_val_if_fail(curve != NULL, NULL);

    if (curve->end == 0) {
        return NULL;
    }

    return curve->bpath;
}

NR::Point
sp_curve_first_point(SPCurve const *const curve)
{
    NArtBpath *const bpath = sp_curve_first_bpath(curve);
    g_return_val_if_fail(bpath != NULL, NR::Point(0, 0));
    return bpath->c(3);
}

NR::Point
sp_curve_last_point(SPCurve const *const curve)
{
    NArtBpath *const bpath = sp_curve_last_bpath(curve);
    g_return_val_if_fail(bpath != NULL, NR::Point(0, 0));
    return bpath->c(3);
}

static bool
is_moveto(NRPathcode const c)
{
    return c == NR_MOVETO || c == NR_MOVETO_OPEN;
}

/** Returns \a curve but drawn in the opposite direction.  Should result in the same shape, but
    with all its markers drawn facing the other direction.
**/
SPCurve *
sp_curve_reverse(SPCurve const *curve)
{
    /* We need at least moveto, curveto, end. */
    g_return_val_if_fail(curve->end - curve->substart > 1, NULL);

    NArtBpath const *be = curve->bpath + curve->end - 1;

    g_assert(is_moveto(curve->bpath[curve->substart].code));
    g_assert(is_moveto(curve->bpath[0].code));
    g_assert((be+1)->code == NR_END);

    SPCurve  *new_curve = sp_curve_new_sized(curve->length);
    sp_curve_moveto(new_curve, be->c(3));

    for (NArtBpath const *bp = be;;) {
        switch (bp->code) {
            case NR_MOVETO:
                g_assert(new_curve->bpath[new_curve->substart].code == NR_MOVETO_OPEN);
                new_curve->bpath[new_curve->substart].code = NR_MOVETO;
                /* FALL-THROUGH */
            case NR_MOVETO_OPEN:
                sp_curve_moveto(new_curve, (bp-1)->c(3));
                break;

            case NR_LINETO:
                sp_curve_lineto(new_curve, (bp-1)->c(3));
                break;

            case NR_CURVETO:
                sp_curve_curveto(new_curve, bp->c(2), bp->c(1), (bp-1)->c(3));
                break;

            case NR_END:
                g_assert_not_reached();
        }

        if (bp == curve->bpath) {
            break;
        }
        --bp;
    }

    return new_curve;
}

void
sp_curve_append(SPCurve *curve,
                SPCurve const *curve2,
                gboolean use_lineto)
{
    g_return_if_fail(curve != NULL);
    g_return_if_fail(curve2 != NULL);

    if (curve2->end < 1)
        return;

    NArtBpath const *bs = curve2->bpath;

    bool closed = curve->closed;

    for (NArtBpath const *bp = bs; bp->code != NR_END; bp++) {
        switch (bp->code) {
            case NR_MOVETO_OPEN:
                if (use_lineto && curve->hascpt) {
                    sp_curve_lineto(curve, bp->x3, bp->y3);
                    use_lineto = FALSE;
                } else {
                    if (closed) sp_curve_closepath(curve);
                    sp_curve_moveto(curve, bp->x3, bp->y3);
                }
                closed = false;
                break;

            case NR_MOVETO:
                if (use_lineto && curve->hascpt) {
                    sp_curve_lineto(curve, bp->x3, bp->y3);
                    use_lineto = FALSE;
                } else {
                    if (closed) sp_curve_closepath(curve);
                    sp_curve_moveto(curve, bp->x3, bp->y3);
                }
                closed = true;
                break;

            case NR_LINETO:
                sp_curve_lineto(curve, bp->x3, bp->y3);
                break;

            case NR_CURVETO:
                sp_curve_curveto(curve, bp->x1, bp->y1, bp->x2, bp->y2, bp->x3, bp->y3);
                break;

            case NR_END:
                g_assert_not_reached();
        }
    }

    if (closed) {
        sp_curve_closepath(curve);
    }
}

SPCurve *
sp_curve_append_continuous(SPCurve *c0, SPCurve const *c1, gdouble tolerance)
{
    g_return_val_if_fail(c0 != NULL, NULL);
    g_return_val_if_fail(c1 != NULL, NULL);
    g_return_val_if_fail(!c0->closed, NULL);
    g_return_val_if_fail(!c1->closed, NULL);

    if (c1->end < 1) {
        return c0;
    }

    NArtBpath *be = sp_curve_last_bpath(c0);
    if (be) {
        NArtBpath const *bs = sp_curve_first_bpath(c1);
        if ( bs
             && ( fabs( bs->x3 - be->x3 ) <= tolerance )
             && ( fabs( bs->y3 - be->y3 ) <= tolerance ) )
        {
            /* fixme: Strictly we mess in case of multisegment mixed open/close curves */
            bool closed = false;
            for (bs = bs + 1; bs->code != NR_END; bs++) {
                switch (bs->code) {
                    case NR_MOVETO_OPEN:
                        if (closed) sp_curve_closepath(c0);
                        sp_curve_moveto(c0, bs->x3, bs->y3);
                        closed = false;
                        break;
                    case NR_MOVETO:
                        if (closed) sp_curve_closepath(c0);
                        sp_curve_moveto(c0, bs->x3, bs->y3);
                        closed = true;
                        break;
                    case NR_LINETO:
                        sp_curve_lineto(c0, bs->x3, bs->y3);
                        break;
                    case NR_CURVETO:
                        sp_curve_curveto(c0, bs->x1, bs->y1, bs->x2, bs->y2, bs->x3, bs->y3);
                        break;
                    case NR_END:
                        g_assert_not_reached();
                }
            }
        } else {
            sp_curve_append(c0, c1, TRUE);
        }
    } else {
        sp_curve_append(c0, c1, TRUE);
    }

    return c0;
}

void
sp_curve_backspace(SPCurve *curve)
{
    g_return_if_fail(curve != NULL);

    if (curve->end > 0) {
        curve->end -= 1;
        if (curve->end > 0) {
            NArtBpath *bp = curve->bpath + curve->end - 1;
            if ((bp->code == NR_MOVETO)     ||
                (bp->code == NR_MOVETO_OPEN)  )
            {
                curve->hascpt = TRUE;
                curve->posset = TRUE;
                curve->closed = FALSE;
                curve->movePos = bp->c(3);
                curve->end -= 1;
            }
        }
        curve->bpath[curve->end].code = NR_END;
    }
}

/* Private methods */

static bool sp_bpath_good(NArtBpath const bpath[])
{
    g_return_val_if_fail(bpath != NULL, FALSE);

    NArtBpath const *bp = bpath;
    while (bp->code != NR_END) {
        bp = sp_bpath_check_subpath(bp);
        if (bp == NULL)
            return false;
    }

    return true;
}

static NArtBpath *sp_bpath_clean(NArtBpath const bpath[])
{
    NArtBpath *new_bpath = nr_new(NArtBpath, sp_bpath_length(bpath));

    NArtBpath const *bp = bpath;
    NArtBpath *np = new_bpath;

    while (bp->code != NR_END) {
        if (sp_bpath_check_subpath(bp)) {
            *np++ = *bp++;
            while ((bp->code == NR_LINETO) ||
                   (bp->code == NR_CURVETO))
                *np++ = *bp++;
        } else {
            bp++;
            while ((bp->code == NR_LINETO) ||
                   (bp->code == NR_CURVETO))
                bp++;
        }
    }

    if (np == new_bpath) {
        nr_free(new_bpath);
        return NULL;
    }

    np->code = NR_END;
    np += 1;

    new_bpath = nr_renew(new_bpath, NArtBpath, np - new_bpath);

    return new_bpath;
}

static NArtBpath const *sp_bpath_check_subpath(NArtBpath const bpath[])
{
    g_return_val_if_fail(bpath != NULL, NULL);

    bool closed;
    if (bpath->code == NR_MOVETO) {
        closed = true;
    } else if (bpath->code == NR_MOVETO_OPEN) {
        closed = false;
    } else {
        return NULL;
    }

    gint len = 0;
    gint i;
    for (i = 1; (bpath[i].code != NR_END) && (bpath[i].code != NR_MOVETO) && (bpath[i].code != NR_MOVETO_OPEN); i++) {
        switch (bpath[i].code) {
            case NR_LINETO:
            case NR_CURVETO:
                len++;
                break;
            default:
                return NULL;
        }
    }

    if (closed) {
        if (len < 1)
            return NULL;

        if ((bpath->x3 != bpath[i-1].x3) || (bpath->y3 != bpath[i-1].y3))
            return NULL;
    } else {
        if (len < 1)
            return NULL;
    }

    return bpath + i;
}

static unsigned sp_bpath_length(NArtBpath const bpath[])
{
    g_return_val_if_fail(bpath != NULL, FALSE);

    unsigned ret = 0;
    while ( bpath[ret].code != NR_END ) {
        ++ret;
    }
    ++ret;

    return ret;
}

/*fixme: this is bogus -- it doesn't check for nr_moveto, which will indicate a closing of the
subpath it's nonsense to talk about a path as a whole being closed, although maybe someone would
want that for some other reason?  Oh, also, if the bpath just ends, then it's *open*.  I hope
nobody is using this code for anything. */
static bool sp_bpath_closed(NArtBpath const bpath[])
{
    g_return_val_if_fail(bpath != NULL, FALSE);

    for (NArtBpath const *bp = bpath; bp->code != NR_END; bp++) {
        if (bp->code == NR_MOVETO_OPEN) {
            return false;
        }
    }

    return true;
}

static double
bezier_len(NR::Point const &c0,
           NR::Point const &c1,
           NR::Point const &c2,
           NR::Point const &c3,
           double const threshold)
{
    /* The SVG spec claims that a closed form exists, but for the moment I'll use
     * a stupid algorithm.
     */
    double const lbound = L2( c3 - c0 );
    double const ubound = L2( c1 - c0 ) + L2( c2 - c1 ) + L2( c3 - c2 );
    double ret;
    if ( ubound - lbound <= threshold ) {
        ret = .5 * ( lbound + ubound );
    } else {
        NR::Point const a1( .5 * ( c0 + c1 ) );
        NR::Point const b2( .5 * ( c2 + c3 ) );
        NR::Point const c12( .5 * ( c1 + c2 ) );
        NR::Point const a2( .5 * ( a1 + c12 ) );
        NR::Point const b1( .5 * ( c12 + b2 ) );
        NR::Point const midpoint( .5 * ( a2 + b1 ) );
        double const rec_threshold = .625 * threshold;
        ret = bezier_len(c0, a1, a2, midpoint, rec_threshold) + bezier_len(midpoint, b1, b2, c3, rec_threshold);
        if (!(lbound - 1e-2 <= ret && ret <= ubound + 1e-2)) {
            using NR::X; using NR::Y;
            g_warning("ret=%f outside of expected bounds [%f, %f] for {(%.0f %.0f) (%.0f %.0f) (%.0f %.0f) (%.0f %.0f)}",
                      ret, lbound, ubound, c0[X], c0[Y], c1[X], c1[Y], c2[X], c2[Y], c3[X], c3[Y]);
        }
    }
    return ret;
}

/* Excludes length of closepath segments. */
static double
sp_curve_distance_including_space(SPCurve const *const curve, double seg2len[])
{
    g_return_val_if_fail(curve != NULL, 0.);

    double ret = 0.0;

    if ( curve->bpath->code == NR_END ) {
        return ret;
    }

    NR::Point prev(curve->bpath->c(3));
    for (gint i = 1; i < curve->end; ++i) {
        NArtBpath &p = curve->bpath[i];
        double seg_len;
        switch (p.code) {
            case NR_MOVETO_OPEN:
            case NR_MOVETO:
            case NR_LINETO:
                seg_len = L2(p.c(3) - prev);
                break;

            case NR_CURVETO:
                seg_len = bezier_len(prev, p.c(1), p.c(2), p.c(3), 1.);
                break;

            case NR_END:
                return ret;
        }
        seg2len[i - 1] = seg_len;
        ret += seg_len;
        prev = p.c(3);
    }
    g_assert(!(ret < 0));
    return ret;
}

/** Like sp_curve_distance_including_space, but ensures that the result >= 1e-18:
 *  uses 1 per segment if necessary.
 */
static double
sp_curve_nonzero_distance_including_space(SPCurve const *const curve, double seg2len[])
{
    double const real_dist(sp_curve_distance_including_space(curve, seg2len));
    if (real_dist >= 1e-18) {
        return real_dist;
    } else {
        unsigned const nSegs = SP_CURVE_LENGTH(curve) - 1;
        for (unsigned i = 0; i < nSegs; ++i) {
            seg2len[i] = 1.;
        }
        return (double) nSegs;
    }
}

void
sp_curve_stretch_endpoints(SPCurve *curve, NR::Point const &new_p0, NR::Point const &new_p1)
{
    if (sp_curve_empty(curve)) {
        return;
    }
    g_assert(unsigned(SP_CURVE_LENGTH(curve)) + 1 == sp_bpath_length(curve->bpath));
    unsigned const nSegs = SP_CURVE_LENGTH(curve) - 1;
    g_assert(nSegs != 0);
    double *const seg2len = new double[nSegs];
    double const tot_len = sp_curve_nonzero_distance_including_space(curve, seg2len);
    NR::Point const offset0( new_p0 - sp_curve_first_point(curve) );
    NR::Point const offset1( new_p1 - sp_curve_last_point(curve) );
    curve->bpath->setC(3, new_p0);
    double begin_dist = 0.;
    for (unsigned si = 0; si < nSegs; ++si) {
        double const end_dist = begin_dist + seg2len[si];
        NArtBpath &p = curve->bpath[1 + si];
        switch (p.code) {
            case NR_LINETO:
            case NR_MOVETO:
            case NR_MOVETO_OPEN:
                p.setC(3, p.c(3) + NR::Lerp(end_dist / tot_len, offset0, offset1));
                break;

            case NR_CURVETO:
                for (unsigned ci = 1; ci <= 3; ++ci) {
                    p.setC(ci, p.c(ci) + Lerp((begin_dist + ci * seg2len[si] / 3.) / tot_len, offset0, offset1));
                }
                break;

            default:
                g_assert_not_reached();
        }

        begin_dist = end_dist;
    }
    g_assert(L1(curve->bpath[nSegs].c(3) - new_p1) < 1.);
    /* Explicit set for better numerical properties. */
    curve->bpath[nSegs].setC(3, new_p1);
    delete seg2len;
    g_assert(fabs(begin_dist - tot_len) < 1e-18);
}


/* ======== Doxygen documentation ======== */

/** \struct SPCurve
 *
 *  Wrapper around NArtBpath.
 */

/** \var SPCurve::end
 *
 * Index in bpath[] of NR_END element.
 */

/** \var SPCurve::length
 *
 * Allocated size (i.e.\ capacity) of bpath[] array.  Not to be confused with the SP_CURVE_LENGTH
 * macro, which returns the logical length of the path (i.e.\ index of NR_END).
 */

/** \var SPCurve::substart
 *
 * Index in bpath[] of the start (i.e. moveto element) of the last subpath in this path.
 */

/** \var SPCurve::movePos
 *
 * Previous moveto position.
 *
 * (Note: This is used for coalescing moveto's, whereas if we're to conform to the SVG spec then we
 * mustn't coalesce movetos if we have midpoint markers.  Ref:
 * http://www.w3.org/TR/SVG11/implnote.html#PathElementImplementationNotes, first subitem of the
 * item about zero-length path segments.)
 */

/** \var SPCurve::sbpath
 *
 * True iff bpath points to read-only, static storage (see callers of
 * sp_curve_new_from_static_bpath), in which case we shouldn't free bpath and shouldn't write
 * through it.
 */

/** \var SPCurve::hascpt
 *
 * True iff currentpoint is defined.
 */

/** \var SPCurve::posset
 *
 * True iff previous was moveto.
 */

/** \var SPCurve::moving
 *
 * True iff bpath end is moving.
 */

/** \var SPCurve::closed
 *
 * True iff all subpaths are closed.
 */


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
