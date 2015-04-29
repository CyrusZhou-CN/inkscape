/*
 * Released under GNU GPL, read the file 'COPYING' for more information
 */
#include <gtkmm.h>
#include "live_effects/lpe-bspline.h"
#include "ui/widget/scalar.h"
#include "display/curve.h"
#include "helper/geom.h"
#include "helper/geom-curves.h"
#include "helper/geom-satellite.h"
#include "helper/geom-satellite-enum.h"
#include "helper/geom-pathinfo.h"
#include "sp-path.h"
#include "svg/svg.h"
#include "xml/repr.h"
#include "knotholder.h"
// TODO due to internal breakage in glibmm headers, this must be last:
#include <glibmm/i18n.h>

using namespace Geom;
namespace Inkscape {
namespace LivePathEffect {

const double HANDLE_CUBIC_GAP = 0.01;
const double NO_POWER = 0.0;
const double DEFAULT_START_POWER = 0.3334;
const double DEFAULT_END_POWER = 0.6667;

LPEBSpline::LPEBSpline(LivePathEffectObject *lpeobject)
    : Effect(lpeobject),
      satellites_param(_("pair_array_param"), _("pair_array_param"), "satellites_param", &wr, this),
      steps(_("Steps with CTRL:"), _("Change number of steps with CTRL pressed"), "steps", &wr, this, 2),
      helper_size(_("Helper size:"), _("Helper size"), "helper_size", &wr, this, 0),
      ignoreCusp(_("Ignore cusp nodes"), _("Change ignoring cusp nodes"), "ignoreCusp", &wr, this, true),
      onlySelected(_("Change only selected nodes"), _("Change only selected nodes"), "onlySelected", &wr, this, false),
      weight(_("Change weight:"), _("Change weight of the effect"), "weight", &wr, this, defaultStartPower),
      pointwise(NULL)
{
    registerParameter(&satellites_param);
    registerParameter(&weight);
    registerParameter(&steps);
    registerParameter(&helper_size);
    registerParameter(&ignore_cusp);
    registerParameter(&only_selected);

    weight.param_set_range(NO_POWER, 1);
    weight.param_set_increments(0.1, 0.1);
    weight.param_set_digits(4);

    steps.param_set_range(1, 10);
    steps.param_set_increments(1, 1);
    steps.param_set_digits(0);

    helper_size.param_set_range(0.0, 999.0);
    helper_size.param_set_increments(5, 5);
    helper_size.param_set_digits(2);
}

LPEBSpline::~LPEBSpline() {
}

void LPEBSpline::doBeforeEffect (SPLPEItem const* lpeitem)
{
    if(!hp.empty()) {
        hp.clear();
    }
    SPLPEItem *splpeitem = const_cast<SPLPEItem *>(lpeitem);
    SPShape *shape = dynamic_cast<SPShape *>(splpeitem);
    if (shape) {
        SPCurve *c = shape->getCurve();
        SPPath *path = dynamic_cast<SPPath *>(shape);
        if (path) {
            c = path->get_original_curve();
        }
        //mandatory call
        satellites_param.setEffectType(effectType());

        PathVector const &original_pathv =
            pathv_to_linear_and_cubic_beziers(c->get_pathvector());
        Piecewise<D2<SBasis> > pwd2_in = paths_to_pw(original_pathv);
        pwd2_in = remove_short_cuts(pwd2_in, 0.01);
        std::vector<Geom::Satellite> sats = satellites_param.data();
        if(sats.empty()){
            doOnApply(lpeitem);
            sats = satellites_param.data();
        }
        if (satellites_param.knoth) {
            satellites_param.knoth->update_knots();
        }
        if (pointwise && c->get_segment_count() != sats.size()) {
            pointwise->recalculateForNewPwd2(pwd2_in, original_pathv);
        } else {
            pointwise = new Pointwise(pwd2_in, sats);
        }
        satellites_param.setPointwise(pointwise);
    } else {
        g_warning("LPE Fillet can only be applied to shapes (not groups).");
    }
}


void LPEBSpline::doOnApply(SPLPEItem const* lpeitem)
{
    SPLPEItem *splpeitem = const_cast<SPLPEItem *>(lpeitem);
    SPShape *shape = dynamic_cast<SPShape *>(splpeitem);
    if (shape) {
        PathVector const &original_pathv =
            pathv_to_linear_and_cubic_beziers(shape->getCurve()->get_pathvector());
        Piecewise<D2<SBasis> > pwd2_in = paths_to_pw(original_pathv);
        pwd2_in = remove_short_cuts(pwd2_in, 0.01);
        int global_counter = 0;
        std::vector<Geom::Satellite> satellites;
        Geom::CubicBezier const *cubic = NULL;
        for (PathVector::const_iterator path_it = original_pathv.begin();
                path_it != original_pathv.end(); ++path_it) {
            if (path_it->empty()) {
                continue;
            }
            Geom::Path::const_iterator curve_it1 = path_it->begin();
            Geom::Path::const_iterator curve_endit = path_it->end_default();
            if (path_it->closed()) {
                const Curve &closingline = path_it->back_closed();
                // the closing line segment is always of type
                // LineSegment.
                if (are_near(closingline.initialPoint(), closingline.finalPoint())) {
                    // closingline.isDegenerate() did not work, because it only checks for
                    // *exact* zero length, which goes wrong for relative coordinates and
                    // rounding errors...
                    // the closing line segment has zero-length. So stop before that one!
                    curve_endit = path_it->end_open();
                }
            }
            Geom::Path::const_iterator curve_end = curve_endit;
            --curve_end;
            int counter = 0;
            while (curve_it1 != curve_endit) {
                bool active = true;
                bool hidden = false;
                bool mirror = true;
                bool flexible = true;
                double weight = 0;
                if (counter == 0) {
                    if (!path_it->closed()) {
                        active = false;
                    }
                }
                cubic = dynamic_cast<Geom::CubicBezier const *>(&*curve_it1);
                if (cubic) {
                    if(!are_near((*cubic)[0],(*cubic)[1])) {
                        weight = 1/3.0;
                    }
                }
                Satellite satellite(BS, flexible, active, mirror, hidden, weight, 0.0,0);
                satellites.push_back(satellite);
                ++curve_it1;
                counter++;
                global_counter++;
            }
        }
        pointwise = new Pointwise(pwd2_in, satellites);
        satellites_param.setPointwise(pointwise);
    } else {
        g_warning("LPE BSPliner can only be applied to shapes (not groups).");
        SPLPEItem *item = const_cast<SPLPEItem *>(lpeitem);
        item->removeCurrentPathEffect(false);
    }

}

void LPEBSpline::doEffect(SPCurve *curve)
{

    if (curve->get_segment_count() < 1) {
        return;
    }
    // Make copy of old path as it is changed during processing
    std::vector<Geom::Satellite> sats = satellites_param.data();
    Pathinfo path_info(pointwise->getPwd2());
    Geom::PathVector const original_pathv = curve->get_pathvector();

    curve->reset();
    size_t counter = 0;
    for (Geom::PathVector::const_iterator path_it = original_pathv.begin();
            path_it != original_pathv.end(); ++path_it) {
        if (path_it->empty()) {
            continue;
        }
        hp.push_back(*path_it);
        Geom::Path::const_iterator curve_it1 = path_it->begin();
        Geom::Path::const_iterator curve_it2 = ++(path_it->begin());
        Geom::Path::const_iterator curve_endit = path_it->end_default();
        SPCurve *curve_n = new SPCurve();
        Geom::Point previousNode(0, 0);
        Geom::Point node(0, 0);
        Geom::Point point_at1(0, 0);
        Geom::Point point_at2(0, 0);
        Geom::Point next_point_at1(0, 0);
        Geom::D2<Geom::SBasis> sbasis_in;
        Geom::D2<Geom::SBasis> sbasis_out;
        Geom::D2<Geom::SBasis> sbasis_helper;
        Geom::CubicBezier const *cubic = NULL;
        if (path_it->closed()) {
            // if the path is closed, maybe we have to stop a bit earlier because the
            // closing line segment has zerolength.
            const Geom::Curve &closingline =
                path_it->back_closed(); // the closing line segment is always of type
            // Geom::LineSegment.
            if (are_near(closingline.initialPoint(), closingline.finalPoint())) {
                // closingline.isDegenerate() did not work, because it only checks for
                // *exact* zero length, which goes wrong for relative coordinates and
                // rounding errors...
                // the closing line segment has zero-length. So stop before that one!
                curve_endit = path_it->end_open();
            }
        }
        curve_n->moveto(curve_it1->initialPoint());
        while (curve_it1 != curve_endit) {
            SPCurve *in = new SPCurve();
            in->moveto(curve_it1->initialPoint());
            in->lineto(curve_it1->finalPoint());
            
            cubic = dynamic_cast<Geom::CubicBezier const *>(&*curve_it1);
            SBasisIn = in->first_segment()->toSBasis();
            double weight_1 = sats[counter].amount;
            pointAt1 = SBasisIn.valueAt(weight_1);
            double weight_2 = 1;
            pointAt2 = SBasisIn.valueAt(weight_2);
            if(sats.size() > counter + 1){
                weight_2 = sats[counter + 1].amount;
                pointAt2 = SBasisIn.valueAt(1-weight_2);
            }
            in->reset();
            delete in;
            if ( curve_it2 != curve_endit ) {
                SPCurve *out = new SPCurve();
                out->moveto(curve_it2->initialPoint());
                out->lineto(curve_it2->finalPoint());
                SBasisOut = out->first_segment()->toSBasis();
                nextPointAt1 = SBasisOut.valueAt(0);
                if(sats.size() > counter + 1){
                    nextPointAt1 = SBasisOut.valueAt(weight_2);
                }
                out->reset();
                delete out;
            }
            if (path_it->closed() && curve_it2 == curve_endit) {
                SPCurve *start = new SPCurve();
                start->moveto(path_it->begin()->initialPoint());
                start->lineto(path_it->begin()->finalPoint());
                Geom::D2<Geom::SBasis> SBasisStart = start->first_segment()->toSBasis();
                weight_1 = sats[path_info.first(counter)].amount;
                SPCurve *lineHelper = new SPCurve();
                lineHelper->moveto(SBasisStart.valueAt(weight_1));
                start->reset();
                delete start;
                SPCurve *end = new SPCurve();
                end->moveto(curve_it1->initialPoint());
                end->lineto(curve_it1->finalPoint());
                Geom::D2<Geom::SBasis> SBasisEnd = end->first_segment()->toSBasis();
                lineHelper->lineto(SBasisEnd.valueAt(1-weight_1));
                end->reset();
                delete end;
                SBasisHelper = lineHelper->first_segment()->toSBasis();
                lineHelper->reset();
                delete lineHelper;
                node = SBasisHelper.valueAt(0.5);
                nCurve->curveto(pointAt1, SBasisHelper.valueAt(1), node);
                nCurve->move_endpoints(node, node);

            } else if ( curve_it2 == curve_endit) {
                curve_n->curveto(point_at1, point_at2, curve_it1->finalPoint());
                curve_n->move_endpoints(path_it->begin()->initialPoint(), curve_it1->finalPoint());
            } else {
                SPCurve *line_helper = new SPCurve();
                line_helper->moveto(point_at2);
                line_helper->lineto(next_point_at1);
                sbasis_helper = line_helper->first_segment()->toSBasis();
                line_helper->reset();
                delete line_helper;
                previousNode = node;
                node = SBasisHelper.valueAt(0.5);
                weight_1 = sats[path_info.first(counter)].amount;
                nCurve->curveto(pointAt1, pointAt2, node);
            }
            if(!are_near(node,curve_it1->finalPoint()) && helper_size > 0.0) {
                drawHandle(node, helper_size);
            }
            ++curve_it1;
            ++curve_it2;
            counter++;
        }
        //y cerramos la curva
        if (path_it->closed()) {
            curve_n->closepath_current();
        }
        curve->append(curve_n, false);
        curve_n->reset();
        delete curve_n;
    }
    if(helper_size > 0.0) {
        Geom::PathVector const pathv = curve->get_pathvector();
        hp.push_back(pathv[0]);
    }
}

void
LPEBSpline::drawHandle(Geom::Point p, double helper_size)
{
    char const * svgd = "M 1,0.5 A 0.5,0.5 0 0 1 0.5,1 0.5,0.5 0 0 1 0,0.5 0.5,0.5 0 0 1 0.5,0 0.5,0.5 0 0 1 1,0.5 Z";
    Geom::PathVector pathv = sp_svg_read_pathv(svgd);
    Geom::Affine aff = Geom::Affine();
    aff *= Geom::Scale(helper_size);
    pathv *= aff;
    pathv += p - Geom::Point(0.5*helper_size, 0.5*helper_size);
    hp.push_back(pathv[0]);
}

void
LPEBSpline::addCanvasIndicators(SPLPEItem const */*lpeitem*/, std::vector<Geom::PathVector> &hp_vec)
{
    hp_vec.push_back(hp);
}
Gtk::Widget *LPEBSpline::newWidget()
{
    // use manage here, because after deletion of Effect object, others might
    // still be pointing to this widget.
    Gtk::VBox *vbox = Gtk::manage(new Gtk::VBox(Effect::newWidget()));

    vbox->set_border_width(5);
    std::vector<Parameter *>::iterator it = param_vector.begin();
    while (it != param_vector.end()) {
        if ((*it)->widget_is_visible) {
            Parameter *param = *it;
            Gtk::Widget *widg = dynamic_cast<Gtk::Widget *>(param->param_newWidget());
            /*
            if (param->param_key == "weight") {
                Gtk::HBox * buttons = Gtk::manage(new Gtk::HBox(true,0));
                Gtk::Button *default_weight =
                    Gtk::manage(new Gtk::Button(Glib::ustring(_("Default weight"))));
                default_weight->signal_clicked()
                .connect(sigc::mem_fun(*this, &LPEBSpline::toDefaultWeight));
                buttons->pack_start(*default_weight, true, true, 2);
                Gtk::Button *make_cusp =
                    Gtk::manage(new Gtk::Button(Glib::ustring(_("Make cusp"))));
                make_cusp->signal_clicked()
                .connect(sigc::mem_fun(*this, &LPEBSpline::toMakeCusp));
                buttons->pack_start(*make_cusp, true, true, 2);
                vbox->pack_start(*buttons, true, true, 2);
            }
            if (param->param_key == "weight" || param->param_key == "steps") {
                Inkscape::UI::Widget::Scalar *widg_registered =
                    Gtk::manage(dynamic_cast<Inkscape::UI::Widget::Scalar *>(widg));
                widg_registered->signal_value_changed()
                .connect(sigc::mem_fun(*this, &LPEBSpline::toWeight));
                widg = dynamic_cast<Gtk::Widget *>(widg_registered);
                if (widg) {
                    Gtk::HBox * hbox_weight_steps = dynamic_cast<Gtk::HBox *>(widg);
                    std::vector< Gtk::Widget* > childList = hbox_weight_steps->get_children();
                    Gtk::Entry* entry_widget = dynamic_cast<Gtk::Entry *>(childList[1]);
                    entry_widget->set_width_chars(6);
                }
            }
            if (param->param_key == "only_selected") {
                Gtk::CheckButton *widg_registered =
                    Gtk::manage(dynamic_cast<Gtk::CheckButton *>(widg));
                widg = dynamic_cast<Gtk::Widget *>(widg_registered);
            }
            if (param->param_key == "ignore_cusp") {
                Gtk::CheckButton *widg_registered =
                    Gtk::manage(dynamic_cast<Gtk::CheckButton *>(widg));
                widg = dynamic_cast<Gtk::Widget *>(widg_registered);
            }
            */
            Glib::ustring *tip = param->param_getTooltip();
            if (widg) {
                vbox->pack_start(*widg, true, true, 2);
                if (tip) {
                    widg->set_tooltip_text(*tip);
                } else {
                    widg->set_tooltip_text("");
                    widg->set_has_tooltip(false);
                }
            }
        }

        ++it;
    }
    return dynamic_cast<Gtk::Widget *>(vbox);
}

void LPEBSpline::toDefaultWeight()
{
    changeWeight(DEFAULT_START_POWER);
}

void LPEBSpline::toMakeCusp()
{
    changeWeight(NO_POWER);
}

void LPEBSpline::toWeight()
{
    changeWeight(weight);
}

void LPEBSpline::changeWeight(double weight_ammount)
{
    SPPath *path = dynamic_cast<SPPath *>(sp_lpe_item);
    if(path) {
        SPCurve *curve = path->get_curve_for_edit();
<       gchar *str = sp_svg_write_path(curve->get_pathvector());
        path->getRepr()->setAttribute("inkscape:original-d", str);
    }
}

void LPEBSpline::doBSplineFromWidget(SPCurve *curve, double weight_ammount)
{
    using Geom::X;
    using Geom::Y;

    if (curve->get_segment_count() < 1)
        return;
    // Make copy of old path as it is changed during processing
    Geom::PathVector const original_pathv = curve->get_pathvector();
    curve->reset();

    for (Geom::PathVector::const_iterator path_it = original_pathv.begin();
            path_it != original_pathv.end(); ++path_it) {

        if (path_it->empty()) {
            continue;
        }
        Geom::Path::const_iterator curve_it1 = path_it->begin();
        Geom::Path::const_iterator curve_it2 = ++(path_it->begin());
        Geom::Path::const_iterator curve_endit = path_it->end_default();

        SPCurve *curve_n = new SPCurve();
        Geom::Point point_at0(0, 0);
        Geom::Point point_at1(0, 0);
        Geom::Point point_at2(0, 0);
        Geom::Point point_at3(0, 0);
        Geom::D2<Geom::SBasis> sbasis_in;
        Geom::D2<Geom::SBasis> sbasis_out;
        Geom::CubicBezier const *cubic = NULL;
        if (path_it->closed()) {
            // if the path is closed, maybe we have to stop a bit earlier because the
            // closing line segment has zerolength.
            const Geom::Curve &closingline =
                path_it->back_closed(); // the closing line segment is always of type
            // Geom::LineSegment.
            if (are_near(closingline.initialPoint(), closingline.finalPoint())) {
                // closingline.isDegenerate() did not work, because it only checks for
                // *exact* zero length, which goes wrong for relative coordinates and
                // rounding errors...
                // the closing line segment has zero-length. So stop before that one!
                curve_endit = path_it->end_open();
            }
        }
        curve_n->moveto(curve_it1->initialPoint());
        while (curve_it1 != curve_endit) {
            SPCurve *in = new SPCurve();
            in->moveto(curve_it1->initialPoint());
            in->lineto(curve_it1->finalPoint());
            cubic = dynamic_cast<Geom::CubicBezier const *>(&*curve_it1);
            point_at0 = in->first_segment()->initialPoint();
            point_at3 = in->first_segment()->finalPoint();
            sbasis_in = in->first_segment()->toSBasis();
            if (!only_selected) {
                if (cubic) {
                    if (!ignore_cusp || !Geom::are_near((*cubic)[1], point_at0)) {
                        point_at1 = sbasis_in.valueAt(weight_ammount);
                        if (weight_ammount != NO_POWER) {
                            point_at1 =
                                Geom::Point(point_at1[X] + HANDLE_CUBIC_GAP, point_at1[Y] + HANDLE_CUBIC_GAP);
                        }
                    } else {
                        point_at1 = in->first_segment()->initialPoint();
                    }
                    if (!ignore_cusp || !Geom::are_near((*cubic)[2], point_at3)) {
                        point_at2 = sbasis_in.valueAt(1 - weight_ammount);
                        if (weight_ammount != NO_POWER) {
                            point_at2 =
                                Geom::Point(point_at2[X] + HANDLE_CUBIC_GAP, point_at2[Y] + HANDLE_CUBIC_GAP);
                        }
                    } else {
                        point_at2 = in->first_segment()->finalPoint();
                    }
                } else {
                    if (!ignore_cusp && weight_ammount != NO_POWER) {
                        point_at1 = sbasis_in.valueAt(weight_ammount);
                        if (weight_ammount != NO_POWER) {
                            point_at1 =
                                Geom::Point(point_at1[X] + HANDLE_CUBIC_GAP, point_at1[Y] + HANDLE_CUBIC_GAP);
                        }
                        point_at2 = sbasis_in.valueAt(1 - weight_ammount);
                        if (weight_ammount != NO_POWER) {
                            point_at2 =
                                Geom::Point(point_at2[X] + HANDLE_CUBIC_GAP, point_at2[Y] + HANDLE_CUBIC_GAP);
                        }
                    } else {
                        point_at1 = in->first_segment()->initialPoint();
                        point_at2 = in->first_segment()->finalPoint();
                    }
                }
            } else {
                if (cubic) {
                    if (!ignore_cusp || !Geom::are_near((*cubic)[1], point_at0)) {
                        if (isNodePointSelected(point_at0)) {
                            point_at1 = sbasis_in.valueAt(weight_ammount);
                            if (weight_ammount != NO_POWER) {
                                point_at1 =
                                    Geom::Point(point_at1[X] + HANDLE_CUBIC_GAP, point_at1[Y] + HANDLE_CUBIC_GAP);
                            }
                        } else {
                            point_at1 = (*cubic)[1];
                        }
                    } else {
                        point_at1 = in->first_segment()->initialPoint();
                    }
                    if (!ignore_cusp || !Geom::are_near((*cubic)[2], point_at3)) {
                        if (isNodePointSelected(point_at3)) {
                            point_at2 = sbasis_in.valueAt(1 - weight_ammount);
                            if (weight_ammount != NO_POWER) {
                                point_at2 =
                                    Geom::Point(point_at2[X] + HANDLE_CUBIC_GAP, point_at2[Y] + HANDLE_CUBIC_GAP);
                            }
                        } else {
                            point_at2 = (*cubic)[2];
                        }
                    } else {
                        point_at2 = in->first_segment()->finalPoint();
                    }
                } else {
                    if (!ignore_cusp && weight_ammount != NO_POWER) {
                        if (isNodePointSelected(point_at0)) {
                            point_at1 = sbasis_in.valueAt(weight_ammount);
                            point_at1 =
                                Geom::Point(point_at1[X] + HANDLE_CUBIC_GAP, point_at1[Y] + HANDLE_CUBIC_GAP);
                        } else {
                            point_at1 = in->first_segment()->initialPoint();
                        }
                        if (isNodePointSelected(point_at3)) {
                            point_at2 = sbasis_in.valueAt(weight_ammount);
                            point_at2 =
                                Geom::Point(point_at2[X] + HANDLE_CUBIC_GAP, point_at2[Y] + HANDLE_CUBIC_GAP);
                        } else {
                            point_at2 = in->first_segment()->finalPoint();
                        }
                    } else {
                        point_at1 = in->first_segment()->initialPoint();
                        point_at2 = in->first_segment()->finalPoint();
                    }
                }
            }
            in->reset();
            delete in;
            curve_n->curveto(point_at1, point_at2, point_at3);
            ++curve_it1;
            ++curve_it2;
        }
        if (path_it->closed()) {
            curve_n->move_endpoints(path_it->begin()->initialPoint(),
                                   path_it->begin()->initialPoint());
        } else {
            curve_n->move_endpoints(path_it->begin()->initialPoint(), point_at3);
        }
        if (path_it->closed()) {
            curve_n->closepath_current();
        }
        curve->append(curve_n, false);
        curve_n->reset();
        delete curve_n;
    }
}

}; //namespace LivePathEffect
}; /* namespace Inkscape */

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
