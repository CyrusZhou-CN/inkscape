// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_KNOT_HOLDER_ENTITY_H
#define SEEN_KNOT_HOLDER_ENTITY_H
/*
 * Authors:
 *   Mitsuru Oka <oka326@parkcity.ne.jp>
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *
 * Copyright (C) 1999-2001 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 * Copyright (C) 2001 Mitsuru Oka
 * Copyright (C) 2004 Monash University
 * Copyright (C) 2008 Maximilian Albert
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <2geom/forward.h>

#include "knot.h"
#include "snapper.h"

#include "display/control/canvas-item-enums.h"
#include "display/control/canvas-item-quad.h"
#include "display/control/canvas-item-curve.h"
#include "helper/auto-connection.h"
#include "display/control/canvas-item-ptr.h"

class SPHatch;
class SPItem;
class SPKnot;
class SPDesktop;
class SPPattern;
class SPGaussianBlur;
class KnotHolder;

namespace Inkscape {
namespace LivePathEffect {
    class Effect;
} // namespace LivePathEffect
} // namespace Inkscape

typedef void (* SPKnotHolderSetFunc) (SPItem *item, Geom::Point const &p, Geom::Point const &origin, unsigned int state);
typedef Geom::Point (* SPKnotHolderGetFunc) (SPItem *item);

/**
 * KnotHolderEntity definition.
 */
class KnotHolderEntity {
public:
    KnotHolderEntity() {}
    virtual ~KnotHolderEntity();

    void create(SPDesktop *desktop, SPItem *item, KnotHolder *parent,
                Inkscape::CanvasItemCtrlType type = Inkscape::CANVAS_ITEM_CTRL_TYPE_DEFAULT,
                Glib::ustring const & name = Glib::ustring("unknown"),
                char const *tip = "",
                guint32 color = 0xffffff00);

    /* the get/set/click handlers are virtual functions; each handler class for a knot
       should be derived from KnotHolderEntity and override these functions */
    virtual void knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state) = 0;
    virtual void knot_grabbed(Geom::Point const &/*grab_position*/, unsigned /*state*/) {}
    virtual void knot_ungrabbed(Geom::Point const &p, Geom::Point const &origin, unsigned int state) = 0;
    virtual bool knot_missing() const { return false; }
    virtual Geom::Point knot_get() const = 0;
    virtual void knot_click(unsigned int /*state*/) {}
    virtual bool set_item_clickpos(Geom::Point loc) { return false; }

    virtual void on_created() {}
    virtual void update_knot();

    // private:
    Geom::Point snap_knot_position(Geom::Point const &p, unsigned int state);
    Geom::Point snap_knot_position_constrained(Geom::Point const &p, Inkscape::Snapper::SnapConstraint const &constraint, unsigned int state);

    SPKnot *knot = nullptr;
    SPItem *item = nullptr;
    SPDesktop *desktop = nullptr;
    KnotHolder *parent_holder = nullptr;

    int my_counter = 0;
    inline static int counter = 0;

    /** Connection to \a knot's "moved" signal. */
    unsigned int   handler_id = 0;
    /** Connection to \a knot's "clicked" signal. */
    unsigned int   _click_handler_id = 0;
    /** Connection to \a knot's "ungrabbed" signal. */
    unsigned int   _ungrab_handler_id = 0;

private:
    sigc::connection _mousedown_connection;
    sigc::connection _moved_connection;
    sigc::connection _click_connection;
    sigc::connection _ungrabbed_connection;
};

// derived KnotHolderEntity class for LPEs
class LPEKnotHolderEntity : public KnotHolderEntity {
public:
    LPEKnotHolderEntity(Inkscape::LivePathEffect::Effect *effect) : _effect(effect) {};
    void knot_ungrabbed(Geom::Point const &p, Geom::Point const &origin, guint state) override;
protected:
    Inkscape::LivePathEffect::Effect *_effect;
};

/* pattern manipulation */

class PatternKnotHolderEntity : public KnotHolderEntity {
  public:
    PatternKnotHolderEntity(bool fill) : KnotHolderEntity(), _fill(fill) {}
    void on_created() override;
    bool knot_missing() const override;
    void knot_ungrabbed(Geom::Point const &p, Geom::Point const &origin, guint state) override{};
    bool set_item_clickpos(Geom::Point loc) override;
    void update_knot() override;

protected:
    // true if the entity tracks fill, false for stroke
    bool _fill;
    SPPattern *_pattern() const;
    Geom::Point _get_pos(gdouble x, gdouble y, bool transform = true) const;
    Geom::IntPoint offset_to_cell(Geom::Point loc) const;
    Geom::IntPoint _cell;
};

class PatternKnotHolderEntityXY : public PatternKnotHolderEntity {
public:
    PatternKnotHolderEntityXY(bool fill) : PatternKnotHolderEntity(fill) {}
    ~PatternKnotHolderEntityXY() override = default;

    void on_created() override;
    void update_knot() override;
    Geom::Point knot_get() const override;
    void knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state) override;

private:
    // Extra visual element to show the pattern editing area
    CanvasItemPtr<Inkscape::CanvasItemQuad> _quad;
};

class PatternKnotHolderEntityAngle : public PatternKnotHolderEntity {
public:
    PatternKnotHolderEntityAngle(bool fill) : PatternKnotHolderEntity(fill) {}
    Geom::Point knot_get() const override;
    void knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state) override;
};

class PatternKnotHolderEntityScale : public PatternKnotHolderEntity {
public:
    PatternKnotHolderEntityScale(bool fill) : PatternKnotHolderEntity(fill) {}
    Geom::Point knot_get() const override;
    void knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state) override;
    void knot_grabbed(Geom::Point const &grab_pos, unsigned state) override;

private:
    /// Maximum number of pattern repetitons allowed in an item
    inline static double const MAX_REPETITIONS = 1e6;
    Geom::Affine _cached_transform, _cached_inverse_linear;
    Geom::Point _cached_origin, _cached_diagonal;
    double _cached_min_scale;
};

/* Hatch manipulation */
class HatchKnotHolderEntity : public KnotHolderEntity {
  public:
    HatchKnotHolderEntity(bool fill)
        : KnotHolderEntity()
        , _fill(fill)
    {
    }
    bool knot_missing() const override;
    void knot_ungrabbed(Geom::Point const &p, Geom::Point const &origin, guint state) override{};

  protected:
    // true if the entity tracks fill, false for stroke
    bool _fill;
    SPHatch *_hatch() const;
};

class HatchKnotHolderEntityXY : public HatchKnotHolderEntity {
  public:
    HatchKnotHolderEntityXY(bool fill)
        : HatchKnotHolderEntity(fill)
    {
    }
    Geom::Point knot_get() const override;
    void knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state) override;
};

class HatchKnotHolderEntityAngle : public HatchKnotHolderEntity {
  public:
    HatchKnotHolderEntityAngle(bool fill)
        : HatchKnotHolderEntity(fill)
    {
    }
    Geom::Point knot_get() const override;
    void knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state) override;
};

class HatchKnotHolderEntityScale : public HatchKnotHolderEntity {
  public:
    HatchKnotHolderEntityScale(bool fill)
        : HatchKnotHolderEntity(fill)
    {
    }
    Geom::Point knot_get() const override;
    void knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state) override;
};


/* Filter manipulation */
class FilterKnotHolderEntity : public KnotHolderEntity {
  public:
    FilterKnotHolderEntity(bool topleft) : KnotHolderEntity(), _topleft(topleft) {}
    Geom::Point knot_get() const override;
    void knot_ungrabbed(Geom::Point const &p, Geom::Point const &origin, guint state) override {};
    void knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state) override;
private:
    bool _topleft; // true for topleft point, false for bottomright
};

class BlurKnotHolderEntity : public KnotHolderEntity {
  public:
    BlurKnotHolderEntity(int direction) : KnotHolderEntity(), _dir(direction) {}
    void on_created() override;
    void update_knot() override;
    Geom::Point knot_get() const override;
    void knot_ungrabbed(Geom::Point const &p, Geom::Point const &origin, guint state) override {};
    void knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state) override;

  private:
    SPGaussianBlur *_blur() const;
    Geom::Point _pos() const;

    int _dir;
    CanvasItemPtr<Inkscape::CanvasItemCurve> _line;
    Inkscape::auto_connection _watch_filter;
    Inkscape::auto_connection _watch_blur;
};

#endif /* !SEEN_KNOT_HOLDER_ENTITY_H */

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
