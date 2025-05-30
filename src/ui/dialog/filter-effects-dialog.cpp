// SPDX-License-Identifier: GPL-2.0-or-later
/**@file
 * Filter Effects dialog.
 */
/* Authors:
 *   Nicholas Bishop <nicholasbishop@gmail.org>
 *   Rodrigo Kumpera <kumpera@gmail.com>
 *   Felipe C. da S. Sanches <juca@members.fsf.org>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *   insaner
 *
 * Copyright (C) 2007 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "filter-effects-dialog.h"

#include <map>
#include <set>
#include <string>
#include <sstream>
#include <utility>
#include <vector>
#include <glibmm/convert.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/refptr.h>
#include <glibmm/stringutils.h>
#include <glibmm/ustring.h>
#include <gdkmm/display.h>
#include <gdkmm/general.h>
#include <gdkmm/rgba.h>
#include <gdkmm/seat.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/dragsource.h>
#include <gtkmm/entry.h>
#include <gtkmm/enums.h>
#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/frame.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/grid.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/paned.h>
#include <gtkmm/popover.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/searchentry2.h>
#include <gtkmm/sizegroup.h>
#include <gtkmm/snapshot.h>
#include <gtkmm/textview.h>
#include <gtkmm/treeviewcolumn.h>
#include <gtkmm/treeview.h>
#include <gtkmm/widget.h>
#include <pangomm/layout.h>
#include <sigc++/functors/mem_fun.h>

#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "filter-chemistry.h"
#include "filter-enums.h"
#include "inkscape-window.h"
#include "layer-manager.h"
#include "preferences.h"
#include "selection.h"
#include "style.h"
#include "display/nr-filter-types.h"
#include "object/filters/blend.h"
#include "object/filters/colormatrix.h"
#include "object/filters/componenttransfer-funcnode.h"
#include "object/filters/componenttransfer.h"
#include "object/filters/convolvematrix.h"
#include "object/filters/distantlight.h"
#include "object/filters/merge.h"
#include "object/filters/mergenode.h"
#include "object/filters/pointlight.h"
#include "object/filters/spotlight.h"
#include "selection-chemistry.h"
#include "ui/builder-utils.h"
#include "ui/column-menu-builder.h"
#include "ui/controller.h"
#include "ui/dialog/choose-file.h"
#include "ui/dialog/choose-file-utils.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "ui/util.h"
#include "ui/widget/color-picker.h"
#include "ui/widget/completion-popup.h"
#include "ui/widget/custom-tooltip.h"
#include "ui/widget/filter-effect-chooser.h"
#include "ui/widget/popover-menu-item.h"
#include "ui/widget/popover-menu.h"
#include "ui/widget/spin-scale.h"
#include "ui/widget/spinbutton.h"

using namespace Inkscape::Filters;

namespace Inkscape::UI::Dialog {

using Inkscape::UI::Widget::AttrWidget;
using Inkscape::UI::Widget::ComboBoxEnum;
using Inkscape::UI::Widget::DualSpinScale;
using Inkscape::UI::Widget::SpinScale;

constexpr int max_convolution_kernel_size = 10;

static Glib::ustring const prefs_path = "/dialogs/filters";

// Returns the number of inputs available for the filter primitive type
static int input_count(const SPFilterPrimitive* prim)
{
    if(!prim)
        return 0;
    else if(is<SPFeBlend>(prim) || is<SPFeComposite>(prim) || is<SPFeDisplacementMap>(prim))
        return 2;
    else if(is<SPFeMerge>(prim)) {
        // Return the number of feMergeNode connections plus an extra
        return (int) (prim->children.size() + 1);
    }
    else
        return 1;
}

class CheckButtonAttr : public Gtk::CheckButton, public AttrWidget
{
public:
    CheckButtonAttr(bool def, const Glib::ustring& label,
                    Glib::ustring  tv, Glib::ustring  fv,
                    const SPAttr a, char* tip_text)
        : Gtk::CheckButton(label),
          AttrWidget(a, def),
          _true_val(std::move(tv)), _false_val(std::move(fv))
    {
        signal_toggled().connect(signal_attr_changed().make_slot());
        if (tip_text) {
            set_tooltip_text(tip_text);
        }
    }

    Glib::ustring get_as_attribute() const override
    {
        return get_active() ? _true_val : _false_val;
    }

    void set_from_attribute(SPObject* o) override
    {
        const gchar* val = attribute_value(o);
        if(val) {
            if(_true_val == val)
                set_active(true);
            else if(_false_val == val)
                set_active(false);
        } else {
            set_active(get_default()->as_bool());
        }
    }
private:
    const Glib::ustring _true_val, _false_val;
};

class SpinButtonAttr : public Inkscape::UI::Widget::SpinButton, public AttrWidget
{
public:
    SpinButtonAttr(double lower, double upper, double step_inc,
                   double climb_rate, int digits, const SPAttr a, double def, char* tip_text)
        : Inkscape::UI::Widget::SpinButton(climb_rate, digits),
          AttrWidget(a, def)
    {
        if (tip_text) {
            set_tooltip_text(tip_text);
        }
        set_range(lower, upper);
        set_increments(step_inc, 0);

        signal_value_changed().connect(signal_attr_changed().make_slot());
    }

    Glib::ustring get_as_attribute() const override
    {
        const double val = get_value();

        if(get_digits() == 0)
            return Glib::Ascii::dtostr((int)val);
        else
            return Glib::Ascii::dtostr(val);
    }

    void set_from_attribute(SPObject* o) override
    {
        const gchar* val = attribute_value(o);
        if(val){
            set_value(Glib::Ascii::strtod(val));
        } else {
            set_value(get_default()->as_double());
        }
    }
};

template <typename T> class ComboWithTooltip
    : public ComboBoxEnum<T>
{
public:
    ComboWithTooltip(T const default_value, Util::EnumDataConverter<T> const &c,
                     SPAttr const a = SPAttr::INVALID,
                     Glib::ustring const &tip_text = {})
        : ComboBoxEnum<T>(default_value, c, a, false)
    {
        this->set_tooltip_text(tip_text);
    }
};

// Contains an arbitrary number of spin buttons that use separate attributes
class MultiSpinButton : public Gtk::Box
{
public:
    MultiSpinButton(double lower, double upper, double step_inc,
                    double climb_rate, int digits,
                    std::vector<SPAttr> const &attrs,
                    std::vector<double> const &default_values,
                    std::vector<char *> const &tip_text)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL)
    {
        g_assert(attrs.size()==default_values.size());
        g_assert(attrs.size()==tip_text.size());
        set_spacing(4);
        for(unsigned i = 0; i < attrs.size(); ++i) {
            unsigned index = attrs.size() - 1 - i;
            _spins.push_back(Gtk::make_managed<SpinButtonAttr>(lower, upper, step_inc, climb_rate, digits,
                                                               attrs[index], default_values[index], tip_text[index]));
            UI::pack_end(*this, *_spins.back(), true, true);
            _spins.back()->set_width_chars(3); // allow spin buttons to shrink to save space
        }
    }

    std::vector<SpinButtonAttr *> const &get_spinbuttons() const
    {
        return _spins;
    }

private:
    std::vector<SpinButtonAttr*> _spins;
};

// Contains two spinbuttons that describe a NumberOptNumber
class DualSpinButton : public Gtk::Box, public AttrWidget
{
public:
    DualSpinButton(char* def, double lower, double upper, double step_inc,
                   double climb_rate, int digits, const SPAttr a, char* tt1, char* tt2)
        : AttrWidget(a, def), //TO-DO: receive default num-opt-num as parameter in the constructor
          Gtk::Box(Gtk::Orientation::HORIZONTAL),
          _s1(climb_rate, digits), _s2(climb_rate, digits)
    {
        if (tt1) {
            _s1.set_tooltip_text(tt1);
        }
        if (tt2) {
            _s2.set_tooltip_text(tt2);
        }
        _s1.set_range(lower, upper);
        _s2.set_range(lower, upper);
        _s1.set_increments(step_inc, 0);
        _s2.set_increments(step_inc, 0);

        _s1.signal_value_changed().connect(signal_attr_changed().make_slot());
        _s2.signal_value_changed().connect(signal_attr_changed().make_slot());

        set_spacing(4);
        UI::pack_end(*this, _s2, true, true);
        UI::pack_end(*this, _s1, true, true);
    }

    Inkscape::UI::Widget::SpinButton& get_spinbutton1()
    {
        return _s1;
    }

    Inkscape::UI::Widget::SpinButton& get_spinbutton2()
    {
        return _s2;
    }

    Glib::ustring get_as_attribute() const override
    {
        double v1 = _s1.get_value();
        double v2 = _s2.get_value();

        if(_s1.get_digits() == 0) {
            v1 = (int)v1;
            v2 = (int)v2;
        }

        return Glib::Ascii::dtostr(v1) + " " + Glib::Ascii::dtostr(v2);
    }

    void set_from_attribute(SPObject* o) override
    {
        const gchar* val = attribute_value(o);
        NumberOptNumber n;
        if(val) {
            n.set(val);
        } else {
            n.set(get_default()->as_charptr());
        }
        _s1.set_value(n.getNumber());
        _s2.set_value(n.getOptNumber());

    }
private:
    Inkscape::UI::Widget::SpinButton _s1, _s2;
};

class ColorButton : public Widget::ColorPicker, public AttrWidget
{
public:
    ColorButton(unsigned int def, const SPAttr a, char* tip_text)
        : ColorPicker(_("Select color"), tip_text ? tip_text : "", Colors::Color(0x000000ff), false, false),
          AttrWidget(a, def)
    {
        connectChanged([this](Colors::Color const &color){ signal_attr_changed().emit(); });
        if (tip_text) {
            set_tooltip_text(tip_text);
        }
        setColor(Colors::Color(0xffffffff));
    }

    Glib::ustring get_as_attribute() const override
    {
        return get_current_color().toString(false);
    }

    void set_from_attribute(SPObject* o) override
    {
        const gchar* val = attribute_value(o);
        if (auto color = Colors::Color::parse(val)) {
            setColor(*color);
        } else {
            setColor(Colors::Color(get_default()->as_uint()));
        }
    }
};

// Used for tableValue in feComponentTransfer
class EntryAttr : public Gtk::Entry, public AttrWidget
{
public:
    EntryAttr(const SPAttr a, char* tip_text)
        : AttrWidget(a)
    {
        set_width_chars(3); // let it get narrow
        signal_changed().connect(signal_attr_changed().make_slot());
        if (tip_text) {
            set_tooltip_text(tip_text);
        }
    }

    // No validity checking is done
    Glib::ustring get_as_attribute() const override
    {
        return get_text();
    }

    void set_from_attribute(SPObject* o) override
    {
        const gchar* val = attribute_value(o);
        if(val) {
            set_text( val );
        } else {
            set_text( "" );
        }
    }
};

/* Displays/Edits the matrix for feConvolveMatrix or feColorMatrix */
class FilterEffectsDialog::MatrixAttr : public Gtk::Frame, public AttrWidget
{
public:
    MatrixAttr(const SPAttr a, char* tip_text = nullptr)
        : AttrWidget(a), _locked(false)
    {
        _model = Gtk::ListStore::create(_columns);
        _tree.set_model(_model);
        _tree.set_headers_visible(false);
        set_child(_tree);
        if (tip_text) {
            _tree.set_tooltip_text(tip_text);
        }
    }

    std::vector<double> get_values() const
    {
        std::vector<double> vec;
        for(const auto & iter : _model->children()) {
            for(unsigned c = 0; c < _tree.get_columns().size(); ++c)
                vec.push_back(iter[_columns.cols[c]]);
        }
        return vec;
    }

    void set_values(const std::vector<double>& v)
    {
        unsigned i = 0;
        for (auto &&iter : _model->children()) {
            for(unsigned c = 0; c < _tree.get_columns().size(); ++c) {
                if(i >= v.size())
                    return;
                iter[_columns.cols[c]] = v[i];
                ++i;
            }
        }
    }

    Glib::ustring get_as_attribute() const override
    {
        // use SVGOStringStream to output SVG-compatible doubles
        Inkscape::SVGOStringStream os;

        for(const auto & iter : _model->children()) {
            for(unsigned c = 0; c < _tree.get_columns().size(); ++c) {
                os << iter[_columns.cols[c]] << " ";
            }
        }

        return os.str();
    }

    void set_from_attribute(SPObject* o) override
    {
        if(o) {
            if(is<SPFeConvolveMatrix>(o)) {
                auto conv = cast<SPFeConvolveMatrix>(o);
                int cols, rows;
                cols = (int)conv->get_order().getNumber();
                if (cols > max_convolution_kernel_size)
                    cols = max_convolution_kernel_size;
                rows = conv->get_order().optNumIsSet() ? (int)conv->get_order().getOptNumber() : cols;
                update(o, rows, cols);
            }
            else if(is<SPFeColorMatrix>(o))
                update(o, 4, 5);
        }
    }
private:
    class MatrixColumns : public Gtk::TreeModel::ColumnRecord
    {
    public:
        MatrixColumns()
        {
            cols.resize(max_convolution_kernel_size);
            for(auto & col : cols)
                add(col);
        }
        std::vector<Gtk::TreeModelColumn<double> > cols;
    };

    void update(SPObject* o, const int rows, const int cols)
    {
        if(_locked)
            return;

        _model->clear();

        _tree.remove_all_columns();

        std::vector<gdouble> const *values = nullptr;
        if(is<SPFeColorMatrix>(o))
            values = &cast<SPFeColorMatrix>(o)->get_values();
        else if(is<SPFeConvolveMatrix>(o))
            values = &cast<SPFeConvolveMatrix>(o)->get_kernel_matrix();
        else
            return;

        if(o) {
            for(int i = 0; i < cols; ++i) {
                _tree.append_column_numeric_editable("", _columns.cols[i], "%.2f");
                dynamic_cast<Gtk::CellRendererText &>(*_tree.get_column_cell_renderer(i))
                    .signal_edited().connect(
                        sigc::mem_fun(*this, &MatrixAttr::rebind));
            }

            int ndx = 0;
            for(int r = 0; r < rows; ++r) {
                Gtk::TreeRow row = *(_model->append());
                // Default to identity matrix
                for(int c = 0; c < cols; ++c, ++ndx)
                    row[_columns.cols[c]] = ndx < (int)values->size() ? (*values)[ndx] : (r == c ? 1 : 0);
            }
        }
    }

    void rebind(const Glib::ustring&, const Glib::ustring&)
    {
        _locked = true;
        signal_attr_changed()();
        _locked = false;
    }

    bool _locked;
    Gtk::TreeView _tree;
    Glib::RefPtr<Gtk::ListStore> _model;
    MatrixColumns _columns;
};

// Displays a matrix or a slider for feColorMatrix
class FilterEffectsDialog::ColorMatrixValues : public Gtk::Frame, public AttrWidget
{
public:
    ColorMatrixValues()
        : AttrWidget(SPAttr::VALUES),
          // TRANSLATORS: this dialog is accessible via menu Filters - Filter editor
          _matrix(SPAttr::VALUES, _("This matrix determines a linear transform on color space. Each line affects one of the color components. Each column determines how much of each color component from the input is passed to the output. The last column does not depend on input colors, so can be used to adjust a constant component value.")),
          _saturation("", 1, 0, 1, 0.1, 0.01, 2, SPAttr::VALUES),
          _angle("", 0, 0, 360, 0.1, 0.01, 1, SPAttr::VALUES),
          _label(C_("Label", "None"), Gtk::Align::START)
    {
        _matrix.signal_attr_changed().connect(signal_attr_changed().make_slot());
        _saturation.signal_attr_changed().connect(signal_attr_changed().make_slot());
        _angle.signal_attr_changed().connect(signal_attr_changed().make_slot());

        _label.set_sensitive(false);

        add_css_class("flat");
    }

    void set_from_attribute(SPObject* o) override
    {
        if(is<SPFeColorMatrix>(o)) {
            auto col = cast<SPFeColorMatrix>(o);
            unset_child();

            switch(col->get_type()) {
                case COLORMATRIX_SATURATE:
                    set_child(_saturation);
                    _saturation.set_from_attribute(o);
                    break;

                case COLORMATRIX_HUEROTATE:
                    set_child(_angle);
                    _angle.set_from_attribute(o);
                    break;

                case COLORMATRIX_LUMINANCETOALPHA:
                    set_child(_label);
                    break;

                case COLORMATRIX_MATRIX:
                default:
                    set_child(_matrix);
                    _matrix.set_from_attribute(o);
                    break;
            }
        }
    }

    Glib::ustring get_as_attribute() const override
    {
        const Widget* w = get_child();
        if(w == &_label)
            return "";
        if (auto attrw = dynamic_cast<const AttrWidget *>(w))
            return attrw->get_as_attribute();
        g_assert_not_reached();
        return "";
    }

private:
    MatrixAttr _matrix;
    SpinScale _saturation;
    SpinScale _angle;
    Gtk::Label _label;
};

//Displays a chooser for feImage input
//It may be a filename or the id for an SVG Element
//described in xlink:href syntax
class FileOrElementChooser : public Gtk::Box, public AttrWidget
{
public:
    FileOrElementChooser(FilterEffectsDialog& d, const SPAttr a)
        : AttrWidget(a)
        , _dialog(d)
        , Gtk::Box(Gtk::Orientation::HORIZONTAL)
    {
        set_spacing(3);
        UI::pack_start(*this, _entry, true, true);
        UI::pack_start(*this, _fromFile, false, false);
        UI::pack_start(*this, _fromSVGElement, false, false);

        _fromFile.set_image_from_icon_name("document-open");
        _fromFile.set_tooltip_text(_("Choose image file"));
        _fromFile.signal_clicked().connect(sigc::mem_fun(*this, &FileOrElementChooser::select_file));

        _fromSVGElement.set_label(_("SVG Element"));
        _fromSVGElement.set_tooltip_text(_("Use selected SVG element"));
        _fromSVGElement.signal_clicked().connect(sigc::mem_fun(*this, &FileOrElementChooser::select_svg_element));

        _entry.set_width_chars(1);
        _entry.signal_changed().connect(signal_attr_changed().make_slot());

        set_visible(true);
    }

    // Returns the element in xlink:href form.
    Glib::ustring get_as_attribute() const override
    {
        return _entry.get_text();
    }


    void set_from_attribute(SPObject* o) override
    {
        const gchar* val = attribute_value(o);
        if(val) {
            _entry.set_text(val);
        } else {
            _entry.set_text("");
        }
    }

private:
    void select_svg_element() {
        Inkscape::Selection* sel = _dialog.getDesktop()->getSelection();
        if (sel->isEmpty()) return;
        Inkscape::XML::Node* node = sel->xmlNodes().front();
        if (!node || !node->matchAttributeName("id")) return;

        std::ostringstream xlikhref;
        xlikhref << "#" << node->attribute("id");
        _entry.set_text(xlikhref.str());
    }

    void select_file(){

        // Get the current directory for finding files.
        std::string open_path;
        get_start_directory(open_path, "/dialogs/open/path");

        // Create a dialog.
        auto window = _dialog.getDesktop()->getInkscapeWindow();
        auto filters = create_open_filters();
        auto file = choose_file_open(_("Select an image to be used as input."), window, filters, open_path);

        if (!file) {
            return; // Cancel
        }

        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setString("/dialogs/open/path", file->get_path());

        _entry.set_text(file->get_parse_name());
    }

    Gtk::Entry _entry;
    Gtk::Button _fromFile;
    Gtk::Button _fromSVGElement;
    FilterEffectsDialog &_dialog;
};

class FilterEffectsDialog::Settings
{
public:
    typedef sigc::slot<void (const AttrWidget*)> SetAttrSlot;

    Settings(FilterEffectsDialog& d, Gtk::Box& b, SetAttrSlot slot, const int maxtypes)
        : _dialog(d), _set_attr_slot(std::move(slot)), _current_type(-1), _max_types(maxtypes)
    {
        _groups.resize(_max_types);
        _attrwidgets.resize(_max_types);
        _size_group = Gtk::SizeGroup::create(Gtk::SizeGroup::Mode::HORIZONTAL);

        for(int i = 0; i < _max_types; ++i) {
            _groups[i] = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 3);
            b.set_spacing(4);
            UI::pack_start(b, *_groups[i], UI::PackOptions::shrink);
        }
        //_current_type = 0;  If set to 0 then update_and_show() fails to update properly.
    }

    void show_current_only() {
        for (auto& group : _groups) {
            group->set_visible(false);
        }
        auto t = get_current_type();
        if (t >= 0) {
            _groups[t]->set_visible(true);
        }
    }

    // Show the active settings group and update all the AttrWidgets with new values
    void show_and_update(const int t, SPObject* ob)
    {
        if (t != _current_type) {
            type(t);

            for (auto& group : _groups) {
                group->set_visible(false);
            }
        }

        if (t >= 0) {
            _groups[t]->set_visible(true);
        }

        _dialog.set_attrs_locked(true);
        for(auto & i : _attrwidgets[_current_type])
            i->set_from_attribute(ob);
        _dialog.set_attrs_locked(false);
    }

    int get_current_type() const
    {
        return _current_type;
    }

    void type(const int t)
    {
        _current_type = t;
    }

    void add_no_params()
    {
        auto const lbl = Gtk::make_managed<Gtk::Label>(_("This SVG filter effect does not require any parameters."));
        lbl->set_wrap();
        lbl->set_wrap_mode(Pango::WrapMode::WORD);
        add_widget(lbl, "");
    }

    // LightSource
    LightSourceControl* add_lightsource();

    // Component Transfer Values
    ComponentTransferValues* add_componenttransfervalues(const Glib::ustring& label, SPFeFuncNode::Channel channel);

    // CheckButton
    CheckButtonAttr* add_checkbutton(bool def, const SPAttr attr, const Glib::ustring& label,
                                     const Glib::ustring& tv, const Glib::ustring& fv, char* tip_text = nullptr)
    {
        auto const cb = Gtk::make_managed<CheckButtonAttr>(def, label, tv, fv, attr, tip_text);
        add_widget(cb, "");
        add_attr_widget(cb);
        return cb;
    }

    // ColorButton
    ColorButton* add_color(unsigned int def, const SPAttr attr, const Glib::ustring& label, char* tip_text = nullptr)
    {
        auto const col = Gtk::make_managed<ColorButton>(def, attr, tip_text);
        add_widget(col, label);
        add_attr_widget(col);
        return col;
    }

    // Matrix
    MatrixAttr* add_matrix(const SPAttr attr, const Glib::ustring& label, char* tip_text)
    {
        auto const conv = Gtk::make_managed<MatrixAttr>(attr, tip_text);
        add_widget(conv, label);
        add_attr_widget(conv);
        return conv;
    }

    // ColorMatrixValues
    ColorMatrixValues* add_colormatrixvalues(const Glib::ustring& label)
    {
        auto const cmv = Gtk::make_managed<ColorMatrixValues>();
        add_widget(cmv, label);
        add_attr_widget(cmv);
        return cmv;
    }

    // SpinScale
    SpinScale* add_spinscale(double def, const SPAttr attr, const Glib::ustring& label,
                         const double lo, const double hi, const double step_inc, const double page_inc, const int digits, char* tip_text = nullptr)
    {
        Glib::ustring tip_text2;
        if (tip_text)
            tip_text2 = tip_text;
        auto const spinslider = Gtk::make_managed<SpinScale>("", def, lo, hi, step_inc, page_inc, digits, attr, tip_text2);
        add_widget(spinslider, label);
        add_attr_widget(spinslider);
        return spinslider;
    }

    // DualSpinScale
    DualSpinScale* add_dualspinscale(const SPAttr attr, const Glib::ustring& label,
                                     const double lo, const double hi, const double step_inc,
                                     const double climb, const int digits,
                                     const Glib::ustring tip_text1 = "",
                                     const Glib::ustring tip_text2 = "")
    {
        auto const dss = Gtk::make_managed<DualSpinScale>("", "", lo, lo, hi, step_inc, climb, digits, attr, tip_text1, tip_text2);
        add_widget(dss, label);
        add_attr_widget(dss);
        return dss;
    }

    // SpinButton
    SpinButtonAttr* add_spinbutton(double defalt_value, const SPAttr attr, const Glib::ustring& label,
                                       const double lo, const double hi, const double step_inc,
                                       const double climb, const int digits, char* tip = nullptr)
    {
        auto const sb = Gtk::make_managed<SpinButtonAttr>(lo, hi, step_inc, climb, digits, attr, defalt_value, tip);
        add_widget(sb, label);
        add_attr_widget(sb);
        return sb;
    }

    // DualSpinButton
    DualSpinButton* add_dualspinbutton(char* defalt_value, const SPAttr attr, const Glib::ustring& label,
                                       const double lo, const double hi, const double step_inc,
                                       const double climb, const int digits, char* tip1 = nullptr, char* tip2 = nullptr)
    {
        auto const dsb = Gtk::make_managed<DualSpinButton>(defalt_value, lo, hi, step_inc, climb, digits, attr, tip1, tip2);
        add_widget(dsb, label);
        add_attr_widget(dsb);
        return dsb;
    }

    // MultiSpinButton
    MultiSpinButton* add_multispinbutton(double def1, double def2, const SPAttr attr1, const SPAttr attr2,
                                         const Glib::ustring& label, const double lo, const double hi,
                                         const double step_inc, const double climb, const int digits, char* tip1 = nullptr, char* tip2 = nullptr)
    {
        auto const attrs          = std::vector{attr1, attr2};
        auto const default_values = std::vector{ def1,  def2};
        auto const tips           = std::vector{ tip1,  tip2};
        auto const msb = Gtk::make_managed<MultiSpinButton>(lo, hi, step_inc, climb, digits, attrs, default_values, tips);
        add_widget(msb, label);
        for (auto const i : msb->get_spinbuttons())
            add_attr_widget(i);
        return msb;
    }

    MultiSpinButton* add_multispinbutton(double def1, double def2, double def3, const SPAttr attr1, const SPAttr attr2,
                                         const SPAttr attr3, const Glib::ustring& label, const double lo,
                                         const double hi, const double step_inc, const double climb, const int digits, char* tip1 = nullptr, char* tip2 = nullptr, char* tip3 = nullptr)
    {
        auto const attrs          = std::vector{attr1, attr2, attr3};
        auto const default_values = std::vector{ def1,  def2,  def3};
        auto const tips           = std::vector{ tip1,  tip2,  tip3};
        auto const msb = Gtk::make_managed<MultiSpinButton>(lo, hi, step_inc, climb, digits, attrs, default_values, tips);
        add_widget(msb, label);
        for (auto const i : msb->get_spinbuttons())
            add_attr_widget(i);
        return msb;
    }

    // FileOrElementChooser
    FileOrElementChooser* add_fileorelement(const SPAttr attr, const Glib::ustring& label)
    {
        auto const foech = Gtk::make_managed<FileOrElementChooser>(_dialog, attr);
        add_widget(foech, label);
        add_attr_widget(foech);
        return foech;
    }

    // ComboBoxEnum
    template <typename T> ComboWithTooltip<T>* add_combo(T default_value, const SPAttr attr,
                                                         const Glib::ustring& label,
                                                         const Util::EnumDataConverter<T>& conv,
                                                         const Glib::ustring& tip_text = {})
    {
        auto const combo = Gtk::make_managed<ComboWithTooltip<T>>(default_value, conv, attr, tip_text);
        add_widget(combo, label);
        add_attr_widget(combo);
        return combo;
    }

    // Entry
    EntryAttr* add_entry(const SPAttr attr,
                         const Glib::ustring& label,
                         char* tip_text = nullptr)
    {
        auto const entry = Gtk::make_managed<EntryAttr>(attr, tip_text);
        add_widget(entry, label);
        add_attr_widget(entry);
        return entry;
    }

    Glib::RefPtr<Gtk::SizeGroup> _size_group;

private:
    void add_attr_widget(AttrWidget* a)
    {
        _attrwidgets[_current_type].push_back(a);
        a->signal_attr_changed().connect(sigc::bind(_set_attr_slot, a));
        // add_widget() takes a managed widget, so dtor will delete & disconnect
    }

    /* Adds a new settings widget using the specified label. The label will be formatted with a colon
       and all widgets within the setting group are aligned automatically. */
    void add_widget(Gtk::Widget* w, const Glib::ustring& label)
    {
        g_assert(w->is_managed_());

        auto const hb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
        hb->set_spacing(6);

        if (label != "") {
            auto const lbl = Gtk::make_managed<Gtk::Label>(label);
            lbl->set_xalign(0.0);
            UI::pack_start(*hb, *lbl, UI::PackOptions::shrink);
            _size_group->add_widget(*lbl);
        }

        UI::pack_start(*hb, *w, UI::PackOptions::expand_widget);
        UI::pack_start(*_groups[_current_type], *hb, UI::PackOptions::expand_widget);
    }

    std::vector<Gtk::Box*> _groups;
    FilterEffectsDialog& _dialog;
    SetAttrSlot _set_attr_slot;
    std::vector<std::vector< AttrWidget*> > _attrwidgets;
    int _current_type, _max_types;
};

// Displays sliders and/or tables for feComponentTransfer
class FilterEffectsDialog::ComponentTransferValues : public Gtk::Frame, public AttrWidget
{
public:
    ComponentTransferValues(FilterEffectsDialog& d, SPFeFuncNode::Channel channel)
        : AttrWidget(SPAttr::INVALID),
          _dialog(d),
          _settings(d, _box, sigc::mem_fun(*this, &ComponentTransferValues::set_func_attr), COMPONENTTRANSFER_TYPE_ERROR),
          _type(ComponentTransferTypeConverter, SPAttr::TYPE, false),
          _channel(channel),
          _funcNode(nullptr),
          _box(Gtk::Orientation::VERTICAL)
    {
        add_css_class("flat");

        set_child(_box);
        _box.prepend(_type);

        _type.signal_changed().connect(sigc::mem_fun(*this, &ComponentTransferValues::on_type_changed));

        _settings.type(COMPONENTTRANSFER_TYPE_LINEAR);
        _settings.add_spinscale(1, SPAttr::SLOPE,     _("Slope"),     -10, 10, 0.1, 0.01, 2);
        _settings.add_spinscale(0, SPAttr::INTERCEPT, _("Intercept"), -10, 10, 0.1, 0.01, 2);

        _settings.type(COMPONENTTRANSFER_TYPE_GAMMA);
        _settings.add_spinscale(1, SPAttr::AMPLITUDE, _("Amplitude"),   0, 10, 0.1, 0.01, 2);
        _settings.add_spinscale(1, SPAttr::EXPONENT,  _("Exponent"),    0, 10, 0.1, 0.01, 2);
        _settings.add_spinscale(0, SPAttr::OFFSET,    _("Offset"),    -10, 10, 0.1, 0.01, 2);

        _settings.type(COMPONENTTRANSFER_TYPE_TABLE);
        _settings.add_entry(SPAttr::TABLEVALUES,  _("Values"), _("List of stops with interpolated output"));

        _settings.type(COMPONENTTRANSFER_TYPE_DISCRETE);
        _settings.add_entry(SPAttr::TABLEVALUES,  _("Values"), _("List of discrete values for a step function"));

        //_settings.type(COMPONENTTRANSFER_TYPE_IDENTITY);
        _settings.type(-1); // Force update_and_show() to show/hide windows correctly
    }

    // FuncNode can be in any order so we must search to find correct one.
    SPFeFuncNode* find_node(SPFeComponentTransfer* ct)
    {
        SPFeFuncNode* funcNode = nullptr;
        bool found = false;
        for(auto& node: ct->children) {
            funcNode = cast<SPFeFuncNode>(&node);
            if( funcNode->channel == _channel ) {
                found = true;
                break;
            }
        }
        if( !found )
            funcNode = nullptr;

        return funcNode;
    }

    void set_func_attr(const AttrWidget* input)
    {
        _dialog.set_attr( _funcNode, input->get_attribute(), input->get_as_attribute().c_str());
    }

    // Set new type and update widget visibility
    void set_from_attribute(SPObject* o) override
    {
        // See componenttransfer.cpp
        if(is<SPFeComponentTransfer>(o)) {
            auto ct = cast<SPFeComponentTransfer>(o);

            _funcNode = find_node(ct);
            if( _funcNode ) {
                _type.set_from_attribute( _funcNode );
            } else {
                // Create <funcNode>
                SPFilterPrimitive* prim = _dialog._primitive_list.get_selected();
                if(prim) {
                    Inkscape::XML::Document *xml_doc = prim->document->getReprDoc();
                    Inkscape::XML::Node *repr = nullptr;
                    switch(_channel) {
                        case SPFeFuncNode::R:
                            repr = xml_doc->createElement("svg:feFuncR");
                            break;
                        case SPFeFuncNode::G:
                            repr = xml_doc->createElement("svg:feFuncG");
                            break;
                        case SPFeFuncNode::B:
                            repr = xml_doc->createElement("svg:feFuncB");
                            break;
                        case SPFeFuncNode::A:
                            repr = xml_doc->createElement("svg:feFuncA");
                            break;
                    }

                    //XML Tree being used directly here while it shouldn't be.
                    prim->getRepr()->appendChild(repr);
                    Inkscape::GC::release(repr);

                    // Now we should find it!
                    _funcNode = find_node(ct);
                    if( _funcNode ) {
                        _funcNode->setAttribute( "type", "identity" );
                    } else {
                        //std::cerr << "ERROR ERROR: feFuncX not found!" << std::endl;
                    }
                }
            }

            update();
        }
    }

private:
    void on_type_changed()
    {
        SPFilterPrimitive* prim = _dialog._primitive_list.get_selected();
        if(prim) {
            _funcNode->setAttributeOrRemoveIfEmpty("type", _type.get_as_attribute());

            SPFilter* filter = _dialog._filter_modifier.get_selected_filter();
            g_assert(filter);
            filter->requestModified(SP_OBJECT_MODIFIED_FLAG);

            DocumentUndo::done(prim->document, _("New transfer function type"), INKSCAPE_ICON("dialog-filters"));
            update();
        }
    }

    void update()
    {
        SPFilterPrimitive* prim = _dialog._primitive_list.get_selected();
        if(prim && _funcNode) {
            auto id = _type.get_selected_id();
            if (id.has_value()) {
                _settings.show_and_update(*id, _funcNode);
            }
        }
    }

public:
    Glib::ustring get_as_attribute() const override
    {
        return "";
    }

    FilterEffectsDialog& _dialog;
    Gtk::Box _box;
    Settings _settings;
    ComboBoxEnum<FilterComponentTransferType> _type;
    SPFeFuncNode::Channel _channel; // RGBA
    SPFeFuncNode* _funcNode;
};

// Settings for the three light source objects
class FilterEffectsDialog::LightSourceControl
    : public AttrWidget
    , public Gtk::Box
{
public:
    LightSourceControl(FilterEffectsDialog& d)
        : AttrWidget(SPAttr::INVALID),
          Gtk::Box(Gtk::Orientation::VERTICAL),
          _dialog(d),
          _settings(d, *this, sigc::mem_fun(_dialog, &FilterEffectsDialog::set_child_attr_direct), LIGHT_ENDSOURCE),
          _light_label(_("Light Source:")),
          _light_source(LightSourceConverter),
          _locked(false),
          _light_box(Gtk::Orientation::HORIZONTAL, 6)
    {
        _light_label.set_xalign(0.0);
        _settings._size_group->add_widget(_light_label);
        UI::pack_start(_light_box, _light_label, UI::PackOptions::shrink);
        UI::pack_start(_light_box, _light_source, UI::PackOptions::expand_widget);

        prepend(_light_box);
        _light_source.signal_changed().connect(sigc::mem_fun(*this, &LightSourceControl::on_source_changed));

        // FIXME: these range values are complete crap

        _settings.type(LIGHT_DISTANT);
        _settings.add_spinscale(0, SPAttr::AZIMUTH, _("Azimuth:"), 0, 360, 1, 1, 0, _("Direction angle for the light source on the XY plane, in degrees"));
        _settings.add_spinscale(0, SPAttr::ELEVATION, _("Elevation:"), 0, 360, 1, 1, 0, _("Direction angle for the light source on the YZ plane, in degrees"));

        _settings.type(LIGHT_POINT);
        _settings.add_multispinbutton(/*default x:*/ (double) 0, /*default y:*/ (double) 0, /*default z:*/ (double) 0, SPAttr::X, SPAttr::Y, SPAttr::Z, _("Location:"), -99999, 99999, 1, 100, 0, _("X coordinate"), _("Y coordinate"), _("Z coordinate"));

        _settings.type(LIGHT_SPOT);
        _settings.add_multispinbutton(/*default x:*/ (double) 0, /*default y:*/ (double) 0, /*default z:*/ (double) 0, SPAttr::X, SPAttr::Y, SPAttr::Z, _("Location:"), -99999, 99999, 1, 100, 0, _("X coordinate"), _("Y coordinate"), _("Z coordinate"));
        _settings.add_multispinbutton(/*default x:*/ (double) 0, /*default y:*/ (double) 0, /*default z:*/ (double) 0,
                                      SPAttr::POINTSATX, SPAttr::POINTSATY, SPAttr::POINTSATZ,
                                      _("Points at:"), -99999, 99999, 1, 100, 0, _("X coordinate"), _("Y coordinate"), _("Z coordinate"));
        _settings.add_spinscale(1, SPAttr::SPECULAREXPONENT, _("Specular Exponent:"), 0.1, 100, 0.1, 1, 1, _("Exponent value controlling the focus for the light source"));
        //TODO: here I have used 100 degrees as default value. But spec says that if not specified, no limiting cone is applied. So, there should be a way for the user to set a "no limiting cone" option.
        _settings.add_spinscale(100, SPAttr::LIMITINGCONEANGLE, _("Cone Angle:"), 0, 180, 1, 5, 0, _("This is the angle between the spot light axis (i.e. the axis between the light source and the point to which it is pointing at) and the spot light cone. No light is projected outside this cone."));

        _settings.type(-1); // Force update_and_show() to show/hide windows correctly
    }

private:
    Glib::ustring get_as_attribute() const override
    {
        return "";
    }

    void set_from_attribute(SPObject* o) override
    {
        if(_locked)
            return;

        _locked = true;

        SPObject* child = o->firstChild();

        if(is<SPFeDistantLight>(child))
            _light_source.set_active(0);
        else if(is<SPFePointLight>(child))
            _light_source.set_active(1);
        else if(is<SPFeSpotLight>(child))
            _light_source.set_active(2);
        else
            _light_source.set_active(-1);

        update();

        _locked = false;
    }

    void on_source_changed()
    {
        if(_locked)
            return;

        SPFilterPrimitive* prim = _dialog._primitive_list.get_selected();
        if(prim) {
            _locked = true;

            SPObject* child = prim->firstChild();
            const int ls = _light_source.get_selected();
            // Check if the light source type has changed
            if(!(ls == -1 && !child) &&
               !(ls == 0 && is<SPFeDistantLight>(child)) &&
               !(ls == 1 && is<SPFePointLight>(child)) &&
               !(ls == 2 && is<SPFeSpotLight>(child))) {
                if(child)
                    //XML Tree being used directly here while it shouldn't be.
                    sp_repr_unparent(child->getRepr());

                if(ls != -1) {
                    Inkscape::XML::Document *xml_doc = prim->document->getReprDoc();
                    Inkscape::XML::Node *repr = xml_doc->createElement(_light_source.get_as_attribute().c_str());
                    //XML Tree being used directly here while it shouldn't be.
                    prim->getRepr()->appendChild(repr);
                    Inkscape::GC::release(repr);
                }

                DocumentUndo::done(prim->document, _("New light source"), INKSCAPE_ICON("dialog-filters"));
                update();
            }

            _locked = false;
        }
    }

    void update()
    {
        set_visible(true);

        SPFilterPrimitive* prim = _dialog._primitive_list.get_selected();
        if (prim && prim->firstChild()) {
            auto id = _light_source.get_selected_id();
            if (id.has_value()) {
                _settings.show_and_update(*id, prim->firstChild());
            }
        }
        else {
            _settings.show_current_only();
        }
    }

    FilterEffectsDialog& _dialog;
    Settings _settings;
    Gtk::Box _light_box;
    Gtk::Label _light_label;
    ComboBoxEnum<LightSource> _light_source;
    bool _locked;
};

FilterEffectsDialog::ComponentTransferValues* FilterEffectsDialog::Settings::add_componenttransfervalues(const Glib::ustring& label, SPFeFuncNode::Channel channel)
{
    auto const ct = Gtk::make_managed<ComponentTransferValues>(_dialog, channel);
    add_widget(ct, label);
    add_attr_widget(ct);
    ct->set_margin_top(4);
    ct->set_margin_bottom(4);
    return ct;
}

FilterEffectsDialog::LightSourceControl* FilterEffectsDialog::Settings::add_lightsource()
{
    auto const ls = Gtk::make_managed<LightSourceControl>(_dialog);
    add_attr_widget(ls);
    add_widget(ls, "");
    return ls;
}

static std::unique_ptr<UI::Widget::PopoverMenu> create_popup_menu(Gtk::Widget &parent,
                                                                  sigc::slot<void ()> dup,
                                                                  sigc::slot<void ()> rem)
{
    auto menu = std::make_unique<UI::Widget::PopoverMenu>(Gtk::PositionType::RIGHT);

    auto mi = Gtk::make_managed<UI::Widget::PopoverMenuItem>(_("_Duplicate"), true);
    mi->signal_activate().connect(std::move(dup));
    menu->append(*mi);

    mi = Gtk::make_managed<UI::Widget::PopoverMenuItem>(_("_Remove"), true);
    mi->signal_activate().connect(std::move(rem));
    menu->append(*mi);

    return menu;
}

/*** FilterModifier ***/
FilterEffectsDialog::FilterModifier::FilterModifier(FilterEffectsDialog& d, Glib::RefPtr<Gtk::Builder> builder)
    :    Gtk::Box(Gtk::Orientation::VERTICAL),
         _builder(std::move(builder)),
         _list(get_widget<Gtk::TreeView>(_builder, "filter-list")),
         _dialog(d),
         _add(get_widget<Gtk::Button>(_builder, "btn-new")),
         _dup(get_widget<Gtk::Button>(_builder, "btn-dup")),
         _del(get_widget<Gtk::Button>(_builder, "btn-del")),
         _select(get_widget<Gtk::Button>(_builder, "btn-select")),
         _menu(create_menu()),
         _observer(std::make_unique<Inkscape::XML::SignalObserver>())
{
    _filters_model = Gtk::ListStore::create(_columns);
    _list.set_model(_filters_model);
    _cell_toggle.set_radio();
    _cell_toggle.set_active(true);
    const int selcol = _list.append_column("", _cell_toggle);
    Gtk::TreeViewColumn* col = _list.get_column(selcol - 1);
    if(col)
       col->add_attribute(_cell_toggle.property_active(), _columns.sel);
    _list.append_column_editable(_("_Filter"), _columns.label);
    dynamic_cast<Gtk::CellRendererText &>(*_list.get_column(1)->get_first_cell()).
        signal_edited().connect(sigc::mem_fun(*this, &FilterEffectsDialog::FilterModifier::on_name_edited));

    _list.append_column(_("Used"), _columns.count);
    _list.get_column(2)->set_sizing(Gtk::TreeViewColumn::Sizing::AUTOSIZE);
    _list.get_column(2)->set_expand(false);
    _list.get_column(2)->set_reorderable(true);

    _list.get_column(1)->set_resizable(true);
    _list.get_column(1)->set_sizing(Gtk::TreeViewColumn::Sizing::FIXED);
    _list.get_column(1)->set_expand(true);

    _list.set_reorderable(false);
    _list.enable_model_drag_dest(Gdk::DragAction::MOVE);

#if 0 // on_filter_move() was commented-out in GTK3, so itʼs removed for GTK4. FIXME if you can...!
    _list.signal_drag_drop().connect( sigc::mem_fun(*this, &FilterModifier::on_filter_move), false );
#endif

    _add.signal_clicked().connect([this]{ add_filter(); });
    _del.signal_clicked().connect([this]{ remove_filter(); });
    _dup.signal_clicked().connect([this]{ duplicate_filter(); });
    _select.signal_clicked().connect([this]{ select_filter_elements(); });

    _cell_toggle.signal_toggled().connect(sigc::mem_fun(*this, &FilterModifier::on_selection_toggled));

    auto const click = Gtk::GestureClick::create();
    click->set_button(3); // right
    click->signal_released().connect(Controller::use_state(sigc::mem_fun(*this, &FilterModifier::filter_list_click_released), *click));
    _list.add_controller(click);

    _list.get_selection()->signal_changed().connect(sigc::mem_fun(*this, &FilterModifier::on_filter_selection_changed));
    _observer->signal_changed().connect(signal_filter_changed().make_slot());
}

// Update each filter's sel property based on the current object selection;
//  If the filter is not used by any selected object, sel = 0,
//  otherwise sel is set to the total number of filters in use by selected objects
//  If only one filter is in use, it is selected
void FilterEffectsDialog::FilterModifier::update_selection(Selection *sel)
{
    if (!sel) {
        return;
    }

    std::set<SPFilter*> used;

    for (auto obj : sel->items()) {
        SPStyle *style = obj->style;
        if (!style || !obj) {
            continue;
        }

        if (style->filter.set && style->getFilter()) {
            //TODO: why is this needed?
            obj->bbox_valid = FALSE;
            used.insert(style->getFilter());
        }
    }

    const int size = used.size();

    for (auto &&item : _filters_model->children()) {
        if (used.count(item[_columns.filter])) {
            // If only one filter is in use by the selection, select it
            if (size == 1) {
                _list.get_selection()->select(item.get_iter());
            }
            item[_columns.sel] = size;
        } else {
            item[_columns.sel] = 0;
        }
    }
    update_counts();
    _signal_filters_updated.emit();
}

std::unique_ptr<UI::Widget::PopoverMenu> FilterEffectsDialog::FilterModifier::create_menu()
{
    auto menu = std::make_unique<UI::Widget::PopoverMenu>(Gtk::PositionType::BOTTOM);
    auto append = [&](Glib::ustring const &text, auto const mem_fun)
    {
        auto &item = *Gtk::make_managed<UI::Widget::PopoverMenuItem>(text, true);
        item.signal_activate().connect(sigc::mem_fun(*this, mem_fun));
        menu->append(item);
    };
    append(_("_Duplicate"            ), &FilterModifier::duplicate_filter      );
    append(_("_Remove"               ), &FilterModifier::remove_filter         );
    append(_("R_ename"               ), &FilterModifier::rename_filter         );
    append(_("Select Filter Elements"), &FilterModifier::select_filter_elements);
    return menu;
}

void FilterEffectsDialog::FilterModifier::on_filter_selection_changed()
{
    _observer->set(get_selected_filter());
    signal_filter_changed()();
}

void FilterEffectsDialog::FilterModifier::on_name_edited(const Glib::ustring& path, const Glib::ustring& text)
{
    if (auto iter = _filters_model->get_iter(path)) {
        SPFilter* filter = (*iter)[_columns.filter];
        filter->setLabel(text.c_str());
        DocumentUndo::done(filter->document, _("Rename filter"), INKSCAPE_ICON("dialog-filters"));
        if (iter) {
            (*iter)[_columns.label] = text;
        }
    }
}

#if 0 // on_filter_move() was commented-out in GTK3, so itʼs removed for GTK4. FIXME if you can...!
bool FilterEffectsDialog::FilterModifier::on_filter_move(const Glib::RefPtr<Gdk::DragContext>& /*context*/, int /*x*/, int /*y*/, guint /*time*/) {

//const Gtk::TreeModel::Path& /*path*/) {
/* The code below is bugged. Use of "object->getRepr()->setPosition(0)" is dangerous!
   Writing back the reordered list to XML (reordering XML nodes) should be implemented differently.
   Note that the dialog does also not update its list of filters when the order is manually changed
   using the XML dialog
  for (auto i = _model->children().begin(); i != _model->children().end(); ++i) {
      SPObject* object = (*i)[_columns.filter];
      if(object && object->getRepr()) ;
        object->getRepr()->setPosition(0);
  }
*/
  return false;
}
#endif

void FilterEffectsDialog::FilterModifier::on_selection_toggled(const Glib::ustring& path)
{
    Gtk::TreeModel::iterator iter = _filters_model->get_iter(path);
    selection_toggled(iter, false);
}

void FilterEffectsDialog::FilterModifier::selection_toggled(Gtk::TreeModel::iterator iter, bool toggle) {
    if (!iter) return;

    SPDesktop *desktop = _dialog.getDesktop();
    SPDocument *doc = desktop->getDocument();
    Inkscape::Selection *sel = desktop->getSelection();
    SPFilter* filter = (*iter)[_columns.filter];

    /* If this filter is the only one used in the selection, unset it */
    if ((*iter)[_columns.sel] == 1 && toggle) {
        filter = nullptr;
    }

    for (auto item : sel->items()) {
        SPStyle *style = item->style;
        g_assert(style != nullptr);

        if (filter && filter->valid_for(item)) {
            sp_style_set_property_url(item, "filter", filter, false);
        } else {
            ::remove_filter(item, false);
        }

        item->requestDisplayUpdate((SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG ));
    }

    update_selection(sel);
    DocumentUndo::done(doc, _("Apply filter"), INKSCAPE_ICON("dialog-filters"));
}

void FilterEffectsDialog::FilterModifier::update_counts()
{
    for (auto&& item : _filters_model->children()) {
        SPFilter* f = item[_columns.filter];
        item[_columns.count] = f->getRefCount();
    }
}

static Glib::ustring get_filter_name(SPFilter* filter) {
    if (!filter) return Glib::ustring();

    if (auto label = filter->label()) {
        return label;
    }
    else if (auto id = filter->getId()) {
        return id;
    }
    else {
        return _("filter");
    }
}

/* Add all filters in the document to the combobox.
   Keeps the same selection if possible, otherwise selects the first element */
void FilterEffectsDialog::FilterModifier::update_filters()
{
    auto document = _dialog.getDocument();
    if (!document) return; // no document at shut down

    std::vector<SPObject *> filters = document->getResourceList("filter");

    _filters_model->clear();
    SPFilter* first = nullptr;

    for (auto filter : filters) {
        Gtk::TreeModel::Row row = *_filters_model->append();
        auto f = cast<SPFilter>(filter);
        row[_columns.filter] = f;
        row[_columns.label] = get_filter_name(f);
        if (!first) {
            first = f;
        }
    }

    update_selection(_dialog.getSelection());
    if (first) {
        select_filter(first);
    }
    _dialog.update_filter_general_settings_view();
    _dialog.update_settings_view();
}

bool FilterEffectsDialog::FilterModifier::is_selected_filter_active() {
    if (auto&& sel = _list.get_selection()) {
        if (Gtk::TreeModel::iterator it = sel->get_selected()) {
            return (*it)[_columns.sel] > 0;
        }
    }

    return false;
}

bool FilterEffectsDialog::FilterModifier::filters_present() const {
    return !_filters_model->children().empty();
}

void FilterEffectsDialog::FilterModifier::toggle_current_filter() {
    if (auto&& sel = _list.get_selection()) {
        selection_toggled(sel->get_selected(), true);
    }
}

SPFilter* FilterEffectsDialog::FilterModifier::get_selected_filter()
{
    if(_list.get_selection()) {
        Gtk::TreeModel::iterator i = _list.get_selection()->get_selected();
        if(i)
            return (*i)[_columns.filter];
    }

    return nullptr;
}

void FilterEffectsDialog::FilterModifier::select_filter(const SPFilter* filter)
{
    if (!filter) return;

    for (auto &&item : _filters_model->children()) {
        if (item[_columns.filter] == filter) {
            _list.get_selection()->select(item.get_iter());
            break;
        }
    }
}

Gtk::EventSequenceState
FilterEffectsDialog::FilterModifier::filter_list_click_released(Gtk::GestureClick const & /*click*/,
                                                                int /*n_press*/,
                                                                double const x, double const y)
{
    const bool sensitive = get_selected_filter() != nullptr;
    auto const &items = _menu->get_items();
    items.at(0)->set_sensitive(sensitive);
    items.at(1)->set_sensitive(sensitive);
    items.at(3)->set_sensitive(sensitive);
    _dialog._popoverbin.setPopover(_menu.get());
    _menu->popup_at(_list, x, y);
    return Gtk::EventSequenceState::CLAIMED;
}

void FilterEffectsDialog::FilterModifier::add_filter()
{
    SPDocument* doc = _dialog.getDocument();
    SPFilter* filter = new_filter(doc);

    const int count = _filters_model->children().size();
    std::ostringstream os;
    os << _("filter") << count;
    filter->setLabel(os.str().c_str());

    update_filters();

    select_filter(filter);

    DocumentUndo::done(doc, _("Add filter"), INKSCAPE_ICON("dialog-filters"));
}

void FilterEffectsDialog::FilterModifier::remove_filter()
{
    SPFilter *filter = get_selected_filter();

    if(filter) {
        auto desktop = _dialog.getDesktop();
        SPDocument* doc = filter->document;

        // Delete all references to this filter
        auto all = get_all_items(desktop->layerManager().currentRoot(), desktop, false, false, true);
        for (auto item : all) {
            if (!item) {
                continue;
            }
            if (!item->style) {
                continue;
            }

            const SPIFilter *ifilter = &(item->style->filter);
            if (ifilter && ifilter->href) {
                const SPObject *obj = ifilter->href->getObject();
                if (obj && obj == (SPObject *)filter) {
                    ::remove_filter(item, false);
                }
            }
        }

        //XML Tree being used directly here while it shouldn't be.
        sp_repr_unparent(filter->getRepr());

        DocumentUndo::done(doc, _("Remove filter"), INKSCAPE_ICON("dialog-filters"));

        update_filters();
    
        // select first filter to avoid empty dialog after filter deletion
        auto &&filters = _filters_model->children();
        if (!filters.empty()) {
            _list.get_selection()->select(filters[0].get_iter());
        }
    }
}

void FilterEffectsDialog::FilterModifier::duplicate_filter()
{
    SPFilter* filter = get_selected_filter();

    if (filter) {
        Inkscape::XML::Node *repr = filter->getRepr();
        Inkscape::XML::Node *parent = repr->parent();
        repr = repr->duplicate(repr->document());
        parent->appendChild(repr);

        DocumentUndo::done(filter->document, _("Duplicate filter"), INKSCAPE_ICON("dialog-filters"));

        update_filters();
    }
}

void FilterEffectsDialog::FilterModifier::rename_filter()
{
    _list.set_cursor(_filters_model->get_path(_list.get_selection()->get_selected()), *_list.get_column(1), true);
}

void FilterEffectsDialog::FilterModifier::select_filter_elements()
{
    SPFilter *filter = get_selected_filter();
    auto desktop = _dialog.getDesktop();

    if(!filter)
        return;

    std::vector<SPItem*> items;
    auto all = get_all_items(desktop->layerManager().currentRoot(), desktop, false, false, true);
    for(SPItem *item: all) {
        if (!item->style) {
            continue;
        }

        SPIFilter const &ifilter = item->style->filter;
        if (ifilter.href) {
            const SPObject *obj = ifilter.href->getObject();
            if (obj && obj == (SPObject *)filter) {
                items.push_back(item);
            }
        }
    }
    desktop->getSelection()->setList(items);
}

FilterEffectsDialog::CellRendererConnection::CellRendererConnection()
    : Glib::ObjectBase(typeid(CellRendererConnection))
    , _primitive(*this, "primitive", nullptr)
{}

Glib::PropertyProxy<void*> FilterEffectsDialog::CellRendererConnection::property_primitive()
{
    return _primitive.get_proxy();
}

void FilterEffectsDialog::CellRendererConnection::get_preferred_width_vfunc(Gtk::Widget& widget,
                                                                            int& minimum_width,
                                                                            int& natural_width) const
{
    auto& primlist = dynamic_cast<PrimitiveList&>(widget);
    int count = primlist.get_inputs_count();
    minimum_width = natural_width = size_w * primlist.primitive_count() + primlist.get_input_type_width() * count;
}

void FilterEffectsDialog::CellRendererConnection::get_preferred_width_for_height_vfunc(Gtk::Widget& widget,
                                                                                       int /* height */,
                                                                                       int& minimum_width,
                                                                                       int& natural_width) const
{
    get_preferred_width(widget, minimum_width, natural_width);
}

void FilterEffectsDialog::CellRendererConnection::get_preferred_height_vfunc(Gtk::Widget& widget,
                                                                             int& minimum_height,
                                                                             int& natural_height) const
{
    // Scale the height depending on the number of inputs, unless it's
    // the first primitive, in which case there are no connections.
    auto prim = reinterpret_cast<SPFilterPrimitive*>(_primitive.get_value());
    minimum_height = natural_height = size_h * input_count(prim);
}

void FilterEffectsDialog::CellRendererConnection::get_preferred_height_for_width_vfunc(Gtk::Widget& widget,
                                                                                       int /* width */,
                                                                                       int& minimum_height,
                                                                                       int& natural_height) const
{
    get_preferred_height(widget, minimum_height, natural_height);
}

/*** PrimitiveList ***/
FilterEffectsDialog::PrimitiveList::PrimitiveList(FilterEffectsDialog& d)
    : Glib::ObjectBase{"FilterEffectsDialogPrimitiveList"}
    , WidgetVfuncsClassInit{}
    , Gtk::TreeView{}
    , _dialog(d)
    , _in_drag(0)
    , _observer(std::make_unique<Inkscape::XML::SignalObserver>())
{
    _inputs_count = FPInputConverter._length;

    auto const click = Gtk::GestureClick::create();
    click->set_button(0); // any
    click->set_propagation_phase(Gtk::PropagationPhase::TARGET);
    click->signal_pressed().connect(Controller::use_state(sigc::mem_fun(*this, &PrimitiveList::on_click_pressed), *click));
    click->signal_released().connect(Controller::use_state(sigc::mem_fun(*this, &PrimitiveList::on_click_released), *click));
    add_controller(click);

    auto const motion = Gtk::EventControllerMotion::create();
    motion->set_propagation_phase(Gtk::PropagationPhase::TARGET);
    motion->signal_motion().connect(sigc::mem_fun(*this, &PrimitiveList::on_motion_motion));
    add_controller(motion);

    _model = Gtk::ListStore::create(_columns);

    set_reorderable(true);

    auto const drag = Gtk::DragSource::create();
    drag->signal_drag_end().connect(sigc::mem_fun(*this, &PrimitiveList::on_drag_end));
    add_controller(drag);

    set_model(_model);
    append_column(_("_Effect"), _columns.type);
    get_column(0)->set_resizable(true);
    set_headers_visible(false);

    _observer->signal_changed().connect(signal_primitive_changed().make_slot());
    get_selection()->signal_changed().connect(sigc::mem_fun(*this, &PrimitiveList::on_primitive_selection_changed));
    signal_primitive_changed().connect(sigc::mem_fun(*this, &PrimitiveList::queue_draw));

    init_text();

    int cols_count = append_column(_("Connections"), _connection_cell);
    Gtk::TreeViewColumn* col = get_column(cols_count - 1);
    if(col)
        col->add_attribute(_connection_cell.property_primitive(), _columns.primitive);
}

void FilterEffectsDialog::PrimitiveList::css_changed(GtkCssStyleChange *)
{
    bg_color = get_color_with_class(*this, "theme_bg_color");
}

// Sets up a vertical Pango context/layout, and returns the largest
// width needed to render the FilterPrimitiveInput labels.
void FilterEffectsDialog::PrimitiveList::init_text()
{
    // Set up a vertical context+layout
    Glib::RefPtr<Pango::Context> context = create_pango_context();
    const Pango::Matrix matrix = {0, -1, 1, 0, 0, 0};
    context->set_matrix(matrix);
    _vertical_layout = Pango::Layout::create(context);

    // Store the maximum height and width of the an input type label
    // for later use in drawing and measuring.
    _input_type_height = _input_type_width = 0;
    for(unsigned int i = 0; i < FPInputConverter._length; ++i) {
        _vertical_layout->set_text(_(FPInputConverter.get_label((FilterPrimitiveInput)i).c_str()));
        int fontw, fonth;
        _vertical_layout->get_pixel_size(fontw, fonth);
        if(fonth > _input_type_width)
            _input_type_width = fonth;
        if (fontw > _input_type_height)
            _input_type_height = fontw;
    }
}

sigc::signal<void ()>& FilterEffectsDialog::PrimitiveList::signal_primitive_changed()
{
    return _signal_primitive_changed;
}

void FilterEffectsDialog::PrimitiveList::on_primitive_selection_changed()
{
    _observer->set(get_selected());
    signal_primitive_changed()();
}

/* Add all filter primitives in the current to the list.
   Keeps the same selection if possible, otherwise selects the first element */
void FilterEffectsDialog::PrimitiveList::update()
{
    SPFilter* f = _dialog._filter_modifier.get_selected_filter();
    const SPFilterPrimitive* active_prim = get_selected();
    _model->clear();

    if(f) {
        bool active_found = false;
        _dialog._primitive_box->set_sensitive(true);
        _dialog.update_filter_general_settings_view();
        for(auto& prim_obj: f->children) {
            auto prim = cast<SPFilterPrimitive>(&prim_obj);
            if(!prim) {
                break;
            }
            Gtk::TreeModel::Row row = *_model->append();
            row[_columns.primitive] = prim;

            //XML Tree being used directly here while it shouldn't be.
            row[_columns.type_id] = FPConverter.get_id_from_key(prim->getRepr()->name());
            row[_columns.type] = _(FPConverter.get_label(row[_columns.type_id]).c_str());

            if (prim->getId()) {
                row[_columns.id] =  Glib::ustring(prim->getId());
            }

            if(prim == active_prim) {
                get_selection()->select(row.get_iter());
                active_found = true;
            }
        }

        if(!active_found && _model->children().begin())
            get_selection()->select(_model->children().begin());

        columns_autosize();

        int width, height;
        get_size_request(width, height);
        if (height == -1) {
               // Need to account for the height of the input type text (rotated text) as well as the
               // column headers.  Input type text height determined in init_text() by measuring longest
               // string. Column header height determined by mapping y coordinate of visible
               // rectangle to widget coordinates.
               Gdk::Rectangle vis;
               int vis_x, vis_y;
               get_visible_rect(vis);
               convert_tree_to_widget_coords(vis.get_x(), vis.get_y(), vis_x, vis_y);
               set_size_request(width, _input_type_height + 2 + vis_y);
        }
    } else {
        _dialog._primitive_box->set_sensitive(false);
        set_size_request(-1, -1);
    }
}

void FilterEffectsDialog::PrimitiveList::set_menu(sigc::slot<void ()> dup, sigc::slot<void ()> rem)
{
    _primitive_menu = create_popup_menu(_dialog, std::move(dup), std::move(rem));
}

SPFilterPrimitive* FilterEffectsDialog::PrimitiveList::get_selected()
{
    if(_dialog._filter_modifier.get_selected_filter()) {
        Gtk::TreeModel::iterator i = get_selection()->get_selected();
        if(i)
            return (*i)[_columns.primitive];
    }

    return nullptr;
}

void FilterEffectsDialog::PrimitiveList::select(SPFilterPrimitive* prim)
{
    for (auto &&item : _model->children()) {
        if (item[_columns.primitive] == prim) {
            get_selection()->select(item.get_iter());
            break;
        }
    }
}

void FilterEffectsDialog::PrimitiveList::remove_selected()
{
    if (SPFilterPrimitive* prim = get_selected()) {
        _observer->set(nullptr);
        _model->erase(get_selection()->get_selected());

        //XML Tree being used directly here while it shouldn't be.
        sp_repr_unparent(prim->getRepr());

        DocumentUndo::done(_dialog.getDocument(), _("Remove filter primitive"), INKSCAPE_ICON("dialog-filters"));

        update();
    }
}

void draw_connection_node(const Cairo::RefPtr<Cairo::Context>& cr,
                          std::vector<Geom::Point> const &points,
                          Gdk::RGBA const &fill, Gdk::RGBA const &stroke);

void FilterEffectsDialog::PrimitiveList::snapshot_vfunc(Glib::RefPtr<Gtk::Snapshot> const &snapshot)
{
    parent_type::snapshot_vfunc(snapshot);

    auto const cr = snapshot->append_cairo(get_allocation());

    cr->set_line_width(1.0);
    // In GTK+ 3, the draw function receives the widget window, not the
    // bin_window (i.e., just the area under the column headers).  We
    // therefore translate the origin of our coordinate system to account for this
    int x_origin, y_origin;
    convert_bin_window_to_widget_coords(0,0,x_origin,y_origin);
    cr->translate(x_origin, y_origin);

    auto const fg_color = get_color();
    auto bar_color = mix_colors(bg_color, fg_color, 0.06);
     // color of connector arrow heads and effect separator lines
    auto mid_color = mix_colors(bg_color, fg_color, 0.16);

    SPFilterPrimitive* prim = get_selected();
    int row_count = get_model()->children().size();

    static constexpr int fwidth = CellRendererConnection::size_w;
    Gdk::Rectangle rct, vis;
    Gtk::TreeModel::iterator row = get_model()->children().begin();
    int text_start_x = 0;
    if(row) {
        get_cell_area(get_model()->get_path(row), *get_column(1), rct);
        get_visible_rect(vis);
        text_start_x = rct.get_x() + rct.get_width() - get_input_type_width() * _inputs_count + 1;

        auto w = get_input_type_width();
        auto h = vis.get_height();
        cr->save();
        // erase selection color from selected item
        Gdk::Cairo::set_source_rgba(cr, bg_color);
        cr->rectangle(text_start_x + 1, 0, w * _inputs_count, h);
        cr->fill();
        auto const text_color = change_alpha(fg_color, 0.7);

        // draw vertical bars corresponding to possible filter inputs
        for(unsigned int i = 0; i < _inputs_count; ++i) {
            _vertical_layout->set_text(_(FPInputConverter.get_label((FilterPrimitiveInput)i).c_str()));
            const int x = text_start_x + w * i;
            cr->save();

            Gdk::Cairo::set_source_rgba(cr, bar_color);
            cr->rectangle(x + 1, 0, w - 2, h);
            cr->fill();

            Gdk::Cairo::set_source_rgba(cr, text_color);
            cr->move_to(x + w, 5);
            cr->rotate_degrees(90);
            _vertical_layout->show_in_cairo_context(cr);

            cr->restore();
        }

        cr->restore();
        cr->rectangle(vis.get_x(), 0, vis.get_width(), vis.get_height());
        cr->clip();
    }

    int row_index = 0;
    for(; row != get_model()->children().end(); ++row, ++row_index) {
        get_cell_area(get_model()->get_path(row), *get_column(1), rct);
        const int x = rct.get_x(), y = rct.get_y(), h = rct.get_height();

        // Check mouse state
        double mx{}, my{};
        Gdk::ModifierType mask;
        auto const display = get_display();
        auto seat = display->get_default_seat();
        auto device = seat->get_pointer();
        auto const surface = dynamic_cast<Gtk::Native &>(*get_root()).get_surface();
        g_assert(surface);
        surface->get_device_position(device, mx, my, mask);

        cr->set_line_width(1);

        // Outline the bottom of the connection area
        const int outline_x = x + fwidth * (row_count - row_index);
        cr->save();

        Gdk::Cairo::set_source_rgba(cr, mid_color);

        cr->move_to(vis.get_x(), y + h);
        cr->line_to(outline_x, y + h);
        // Side outline
        cr->line_to(outline_x, y - 1);

        cr->stroke();
        cr->restore();

        std::vector<Geom::Point> con_poly;
        int con_drag_y = 0;
        int con_drag_x = 0;
        bool inside;
        const SPFilterPrimitive* row_prim = (*row)[_columns.primitive];
        const int inputs = input_count(row_prim);

        if(is<SPFeMerge>(row_prim)) {
            for(int i = 0; i < inputs; ++i) {
                inside = do_connection_node(row, i, con_poly, mx, my);

                draw_connection_node(cr, con_poly, inside ? fg_color : mid_color, fg_color);

                if(_in_drag == (i + 1)) {
                    con_drag_y = con_poly[2].y();
                    con_drag_x = con_poly[2].x();
                }

                if(_in_drag != (i + 1) || row_prim != prim) {
                    draw_connection(cr, row, SPAttr::INVALID, text_start_x, outline_x,
                                    con_poly[2].y(), row_count, i, fg_color, mid_color);
                }
            }
        }
        else {
            // Draw "in" shape
            inside = do_connection_node(row, 0, con_poly, mx, my);
            con_drag_y = con_poly[2].y();
            con_drag_x = con_poly[2].x();

            draw_connection_node(cr, con_poly, inside ? fg_color : mid_color, fg_color);

            // Draw "in" connection
            if(_in_drag != 1 || row_prim != prim) {
                draw_connection(cr, row, SPAttr::IN_, text_start_x, outline_x,
                                con_poly[2].y(), row_count, -1, fg_color, mid_color);
            }

            if(inputs == 2) {
                // Draw "in2" shape
                inside = do_connection_node(row, 1, con_poly, mx, my);
                if(_in_drag == 2) {
                    con_drag_y = con_poly[2].y();
                    con_drag_x = con_poly[2].x();
                }

                draw_connection_node(cr, con_poly, inside ? fg_color : mid_color, fg_color);

                // Draw "in2" connection
                if(_in_drag != 2 || row_prim != prim) {
                    draw_connection(cr, row, SPAttr::IN2, text_start_x, outline_x,
                                    con_poly[2].y(), row_count, -1, fg_color, mid_color);
                }
            }
        }

        // Draw drag connection
        if(row_prim == prim && _in_drag) {
            cr->save();
            Gdk::Cairo::set_source_rgba(cr, fg_color);
            cr->move_to(con_drag_x, con_drag_y);
            cr->line_to(mx, con_drag_y);
            cr->line_to(mx, my);
            cr->stroke();
            cr->restore();
          }
    }
}

void FilterEffectsDialog::PrimitiveList::draw_connection(const Cairo::RefPtr<Cairo::Context>& cr,
                                                         const Gtk::TreeModel::iterator& input, const SPAttr attr,
                                                         const int text_start_x, const int x1, const int y1,
                                                         const int row_count, const int pos,
                                                         const Gdk::RGBA fg_color, const Gdk::RGBA mid_color)
{
    cr->save();

    int src_id = 0;
    Gtk::TreeModel::iterator res = find_result(input, attr, src_id, pos); 

    const bool is_first = input == get_model()->children().begin();
    const bool is_selected = (get_selection()->get_selected())
                             ? input == get_selection()->get_selected()
                             : false; // if get_selected() is invalid, comparison crashes in gtk
    const bool is_merge = is<SPFeMerge>((SPFilterPrimitive*)(*input)[_columns.primitive]);
    const bool use_default = !res && !is_merge;
    int arc_radius = 4;

    if (is_selected) {
        cr->set_line_width(2.5);
        arc_radius = 6;
    }

    if(res == input || (use_default && is_first)) {
        // Draw straight connection to a standard input
        // Draw a lighter line for an implicit connection to a standard input
        const int tw = get_input_type_width();
        gint end_x = text_start_x + tw * src_id + 1;

        if(use_default && is_first) {
            Gdk::Cairo::set_source_rgba(cr, fg_color);
            cr->set_dash(std::vector<double> {1.0, 1.0}, 0);
        } else {
            Gdk::Cairo::set_source_rgba(cr, fg_color);
        }

        // draw a half-circle touching destination band
        cr->move_to(x1, y1);
        cr->line_to(end_x, y1);
        cr->stroke();
        cr->arc(end_x, y1, arc_radius, M_PI / 2, M_PI * 1.5);
        cr->fill();
    }
    else {
        // Draw an 'L'-shaped connection to another filter primitive
        // If no connection is specified, draw a light connection to the previous primitive
        if(use_default) {
                res = input;
                --res;
        }

        if(res) {
            Gdk::Rectangle rct;

            get_cell_area(get_model()->get_path(_model->children().begin()), *get_column(1), rct);
            static constexpr int fheight = CellRendererConnection::size_h;
            static constexpr int fwidth  = CellRendererConnection::size_w;

            get_cell_area(get_model()->get_path(res), *get_column(1), rct);
            const int row_index = find_index(res);
            const int x2 = rct.get_x() + fwidth * (row_count - row_index) - fwidth / 2;
            const int y2 = rct.get_y() + rct.get_height();

            // Draw a bevelled 'L'-shaped connection
            Gdk::Cairo::set_source_rgba(cr, fg_color);
            cr->move_to(x1, y1);
            cr->line_to(x2 - fwidth/4, y1);
            cr->line_to(x2, y1 - fheight/4);
            cr->line_to(x2, y2);
            cr->stroke();
        }
    }
    cr->restore();
}

// Draw the triangular outline of the connection node, and fill it
// if desired
void draw_connection_node(const Cairo::RefPtr<Cairo::Context>& cr,
                          std::vector<Geom::Point> const &points,
                          Gdk::RGBA const &fill, Gdk::RGBA const &stroke)
{
    cr->save();
    cr->move_to(points[0].x() + 0.5, points[0].y() + 0.5);
    cr->line_to(points[1].x() + 0.5, points[1].y() + 0.5);
    cr->line_to(points[2].x() + 0.5, points[2].y() + 0.5);
    cr->line_to(points[0].x() + 0.5, points[0].y() + 0.5);
    cr->close_path();

    Gdk::Cairo::set_source_rgba(cr, fill);
    cr->fill_preserve();
    cr->set_line_width(1);
    Gdk::Cairo::set_source_rgba(cr, stroke);
    cr->stroke();

    cr->restore();
}

// Creates a triangle outline of the connection node and returns true if (x,y) is inside the node
bool FilterEffectsDialog::PrimitiveList::do_connection_node(const Gtk::TreeModel::iterator& row, const int input,
                                                            std::vector<Geom::Point> &points,
                                                            const int ix, const int iy)
{
    Gdk::Rectangle rct;
    const int icnt = input_count((*row)[_columns.primitive]);

    get_cell_area(get_model()->get_path(_model->children().begin()), *get_column(1), rct);
    static constexpr int fheight = CellRendererConnection::size_h;
    static constexpr int fwidth  = CellRendererConnection::size_w;

    get_cell_area(_model->get_path(row), *get_column(1), rct);
    const float h = rct.get_height() / icnt;

    const int x = rct.get_x() + fwidth * (_model->children().size() - find_index(row));
    // this is how big arrowhead appears:
    const int con_w = (int)(fwidth * 0.70f);
    const int con_h = (int)(fheight * 0.35f);
    const int con_y = (int)(rct.get_y() + (h / 2) - con_h + (input * h));
    points.clear();
    points.emplace_back(x, con_y);
    points.emplace_back(x, con_y + con_h * 2);
    points.emplace_back(x - con_w, con_y + con_h);

    return ix >= x - h && iy >= con_y && ix <= x && iy <= points[1].y();
}

const Gtk::TreeModel::iterator FilterEffectsDialog::PrimitiveList::find_result(const Gtk::TreeModel::iterator& start,
                                                                    const SPAttr attr, int& src_id,
                                                                    const int pos)
{
    SPFilterPrimitive* prim = (*start)[_columns.primitive];
    Gtk::TreeModel::iterator target = _model->children().end();
    int image = 0;

    if(is<SPFeMerge>(prim)) {
        int c = 0;
        bool found = false;
        for (auto& o: prim->children) {
            if(c == pos && is<SPFeMergeNode>(&o)) {
                image = cast<SPFeMergeNode>(&o)->get_in();
                found = true;
            }
            ++c;
        }
        if(!found)
            return target;
    }
    else {
        if(attr == SPAttr::IN_)
            image = prim->get_in();
        else if(attr == SPAttr::IN2) {
            if(is<SPFeBlend>(prim))
                image = cast<SPFeBlend>(prim)->get_in2();
            else if(is<SPFeComposite>(prim))
                image = cast<SPFeComposite>(prim)->get_in2();
            else if(is<SPFeDisplacementMap>(prim))
                image = cast<SPFeDisplacementMap>(prim)->get_in2();
            else
                return target;
        }
        else
            return target;
    }

    if(image >= 0) {
        for(Gtk::TreeModel::iterator i = _model->children().begin();
            i != start; ++i) {
            if(((SPFilterPrimitive*)(*i)[_columns.primitive])->get_out() == image)
                target = i;
        }
        return target;
    }
    else if(image < -1) {
        src_id = -(image + 2);
        return start;
    }

    return target;
}

int FilterEffectsDialog::PrimitiveList::find_index(const Gtk::TreeModel::iterator& target)
{
    int i = 0;
    for (auto iter = _model->children().begin(); iter != target; ++iter, ++i) {}
    return i;
}

static std::pair<int, int> widget_to_bin_window(Gtk::TreeView const &tree_view, int const wx, int const wy)
{
    int bx, by;
    tree_view.convert_widget_to_bin_window_coords(wx, wy, bx, by);
    return {bx, by};
}

Gtk::EventSequenceState
FilterEffectsDialog::PrimitiveList::on_click_pressed(Gtk::GestureClick const & /*click*/,
                                                     int /*n_press*/,
                                                     double const wx, double const wy)
{
    Gtk::TreePath path;
    Gtk::TreeViewColumn* col;
    auto const [x, y] = widget_to_bin_window(*this, wx, wy);
    int cx, cy;

    _drag_prim = nullptr;

    if(get_path_at_pos(x, y, path, col, cx, cy)) {
        Gtk::TreeModel::iterator iter = _model->get_iter(path);
        std::vector<Geom::Point> points;

        _drag_prim = (*iter)[_columns.primitive];
        const int icnt = input_count(_drag_prim);

        for(int i = 0; i < icnt; ++i) {
            if(do_connection_node(_model->get_iter(path), i, points, x, y)) {
                _in_drag = i + 1;
                break;
            }
        }

        queue_draw();
    }

    if(_in_drag) {
        _scroll_connection = Glib::signal_timeout().connect(sigc::mem_fun(*this, &PrimitiveList::on_scroll_timeout), 150);
        _autoscroll_x = 0;
        _autoscroll_y = 0;
        get_selection()->select(path);
        return Gtk::EventSequenceState::CLAIMED;
    }

    return Gtk::EventSequenceState::NONE;
}

void FilterEffectsDialog::PrimitiveList::on_motion_motion(double wx, double wy)
{
    const int speed = 10;
    const int limit = 15;

    auto const [x, y] = widget_to_bin_window(*this, wx, wy);

    Gdk::Rectangle vis;
    get_visible_rect(vis);
    int vis_x, vis_y;
    int vis_x2, vis_y2;
    convert_widget_to_tree_coords(vis.get_x(), vis.get_y(), vis_x2, vis_y2);
    convert_tree_to_widget_coords(vis.get_x(), vis.get_y(), vis_x, vis_y);
    const int top = vis_y + vis.get_height();
    const int right_edge = vis_x + vis.get_width();

    // When autoscrolling during a connection drag, set the speed based on
    // where the mouse is in relation to the edges.
    if (y < vis_y)
        _autoscroll_y = -(int)(speed + (vis_y - y) / 5);
    else if (y < vis_y + limit)
        _autoscroll_y = -speed;
    else if (y > top)
        _autoscroll_y = (int)(speed + (y - top) / 5);
    else if (y > top - limit)
        _autoscroll_y = speed;
    else
        _autoscroll_y = 0;

    double const e2 = x - vis_x2 / 2;
    // horizontal scrolling
    if(e2 < vis_x)
        _autoscroll_x = -(int)(speed + (vis_x - e2) / 5);
    else if(e2 < vis_x + limit)
        _autoscroll_x = -speed;
    else if(e2 > right_edge)
        _autoscroll_x = (int)(speed + (e2 - right_edge) / 5);
    else if(e2 > right_edge - limit)
        _autoscroll_x = speed;
    else
        _autoscroll_x = 0;

    queue_draw();
}

Gtk::EventSequenceState
FilterEffectsDialog::PrimitiveList::on_click_released(Gtk::GestureClick const &click,
                                                      int /*n_press*/,
                                                      double const wx, double const wy)
{
    _scroll_connection.disconnect();

    auto const prim = get_selected();
    if(_in_drag && prim) {
        auto const [x, y] = widget_to_bin_window(*this, wx, wy);
        Gtk::TreePath path;
        Gtk::TreeViewColumn* col;
        int cx, cy;
        if (get_path_at_pos(x, y, path, col, cx, cy)) {
            auto const selected_iter = get_selection()->get_selected();
            g_assert(selected_iter);

            auto const target_iter = _model->get_iter(path);
            g_assert(target_iter);

            auto const target = target_iter->get_value(_columns.primitive);
            g_assert(target);

            col = get_column(1);
            g_assert(col);

            char const *in_val = nullptr;
            Glib::ustring result;

            Gdk::Rectangle rct;
            get_cell_area(path, *col, rct);
            const int twidth = get_input_type_width();
            const int sources_x = rct.get_width() - twidth * _inputs_count;
            if(cx > sources_x) {
                int src = (cx - sources_x) / twidth;
                if (src < 0) {
                    src = 0;
                } else if(src >= static_cast<int>(_inputs_count)) {
                    src = _inputs_count - 1;
                }
                result = FPInputConverter.get_key((FilterPrimitiveInput)src);
                in_val = result.c_str();
            } else {
                // Ensure that the target comes before the selected primitive
                for (auto iter = _model->children().begin(); iter != selected_iter; ++iter) {
                    if(iter == target_iter) {
                        Inkscape::XML::Node *repr = target->getRepr();
                        // Make sure the target has a result
                        const gchar *gres = repr->attribute("result");
                        if(!gres) {
                            result = cast<SPFilter>(prim->parent)->get_new_result_name();
                            repr->setAttributeOrRemoveIfEmpty("result", result);
                            in_val = result.c_str();
                        }
                        else
                            in_val = gres;
                        break;
                    }
                }
            }

            if(is<SPFeMerge>(prim)) {
                int c = 1;
                bool handled = false;
                for (auto& o: prim->children) {
                    if(c == _in_drag && is<SPFeMergeNode>(&o)) {
                        // If input is null, delete it
                        if(!in_val) {
                            //XML Tree being used directly here while it shouldn't be.
                            sp_repr_unparent(o.getRepr());
                            DocumentUndo::done(prim->document, _("Remove merge node"), INKSCAPE_ICON("dialog-filters"));
                            selected_iter->set_value(_columns.primitive, prim);
                        } else {
                            _dialog.set_attr(&o, SPAttr::IN_, in_val);
                        }
                        handled = true;
                        break;
                    }
                    ++c;
                }

                // Add new input?
                if(!handled && c == _in_drag && in_val) {
                    Inkscape::XML::Document *xml_doc = prim->document->getReprDoc();
                    Inkscape::XML::Node *repr = xml_doc->createElement("svg:feMergeNode");
                    repr->setAttribute("inkscape:collect", "always");

                    //XML Tree being used directly here while it shouldn't be.
                    prim->getRepr()->appendChild(repr);
                    auto node = cast<SPFeMergeNode>(prim->document->getObjectByRepr(repr));
                    Inkscape::GC::release(repr);
                    _dialog.set_attr(node, SPAttr::IN_, in_val);
                    selected_iter->set_value(_columns.primitive, prim);
                }
            }
            else {
                if(_in_drag == 1)
                    _dialog.set_attr(prim, SPAttr::IN_, in_val);
                else if(_in_drag == 2)
                    _dialog.set_attr(prim, SPAttr::IN2, in_val);
            }
        }

        _in_drag = 0;
        queue_draw();

        _dialog.update_settings_view();
    }

    if (click.get_current_button() == 3) {
        bool const sensitive = prim != nullptr;
        _primitive_menu->set_sensitive(sensitive);
        _dialog._popoverbin.setPopover(_primitive_menu.get());
        _primitive_menu->popup_at(*this, wx + 4, wy);
        return Gtk::EventSequenceState::CLAIMED;
    }

    return Gtk::EventSequenceState::NONE;
}

// Checks all of prim's inputs, removes any that use result
static void check_single_connection(SPFilterPrimitive* prim, const int result)
{
    if (prim && (result >= 0)) {
        if (prim->get_in() == result) {
            prim->removeAttribute("in");
        }

        if (auto blend = cast<SPFeBlend>(prim)) {
            if (blend->get_in2() == result) {
                prim->removeAttribute("in2");
            }
        } else if (auto comp = cast<SPFeComposite>(prim)) {
            if (comp->get_in2() == result) {
                prim->removeAttribute("in2");
            }
        } else if (auto disp = cast<SPFeDisplacementMap>(prim)) {
            if (disp->get_in2() == result) {
                prim->removeAttribute("in2");
            }
        }
    }
}

// Remove any connections going to/from prim_iter that forward-reference other primitives
void FilterEffectsDialog::PrimitiveList::sanitize_connections(const Gtk::TreeModel::iterator& prim_iter)
{
    SPFilterPrimitive *prim = (*prim_iter)[_columns.primitive];
    bool before = true;

    for (auto iter = _model->children().begin(); iter != _model->children().end(); ++iter) {
        if(iter == prim_iter)
            before = false;
        else {
            SPFilterPrimitive* cur_prim = (*iter)[_columns.primitive];
            if(before)
                check_single_connection(cur_prim, prim->get_out());
            else
                check_single_connection(prim, cur_prim->get_out());
        }
    }
}

// Reorder the filter primitives to match the list order
void FilterEffectsDialog::PrimitiveList::on_drag_end(Glib::RefPtr<Gdk::Drag> const &/*&drag*/,
                                                     bool /*delete_data*/)
{
    SPFilter* filter = _dialog._filter_modifier.get_selected_filter();
    g_assert(filter);

    int ndx = 0;
    for (auto iter = _model->children().begin(); iter != _model->children().end(); ++iter, ++ndx) {
        SPFilterPrimitive* prim = (*iter)[_columns.primitive];
        if (prim && prim == _drag_prim) {
            prim->getRepr()->setPosition(ndx);
            break;
        }
    }

    for (auto iter = _model->children().begin(); iter != _model->children().end(); ++iter) {
        SPFilterPrimitive* prim = (*iter)[_columns.primitive];
        if (prim && prim == _drag_prim) {
            sanitize_connections(iter);
            get_selection()->select(iter);
            break;
        }
    }

    filter->requestModified(SP_OBJECT_MODIFIED_FLAG);
    DocumentUndo::done(filter->document, _("Reorder filter primitive"), INKSCAPE_ICON("dialog-filters"));
}

static void autoscroll(Glib::RefPtr<Gtk::Adjustment> const &a, double const delta)
{
    auto v = a->get_value() + delta;
    v = std::clamp(v, 0.0, a->get_upper() - a->get_page_size());
    a->set_value(v);
}

// If a connection is dragged towards the top or bottom of the list, the list should scroll to follow.
bool FilterEffectsDialog::PrimitiveList::on_scroll_timeout()
{
    if (!(_autoscroll_y || _autoscroll_x)) return true;

    auto &scrolled_window = dynamic_cast<Gtk::ScrolledWindow &>(*get_parent());

    if(_autoscroll_y) {
        autoscroll(scrolled_window.get_vadjustment(), _autoscroll_y);
    }

    if(_autoscroll_x) {
        autoscroll(scrolled_window.get_hadjustment(), _autoscroll_x);
    }

    queue_draw();
    return true;
}

int FilterEffectsDialog::PrimitiveList::primitive_count() const
{
    return _model->children().size();
}

int FilterEffectsDialog::PrimitiveList::get_input_type_width() const
{
    // Maximum font height calculated in initText() and stored in _input_type_width.
    // Add 2 to font height to account for rectangle around text.
    return _input_type_width + 2;
}

int FilterEffectsDialog::PrimitiveList::get_inputs_count() const {
    return _inputs_count;
}

void FilterEffectsDialog::PrimitiveList::set_inputs_count(int count) {
    _inputs_count = count;
    queue_allocate();
    queue_draw();
}

enum class EffectCategory { Effect, Compose, Colors, Generation };

const Glib::ustring& get_category_name(EffectCategory category) {
    static const std::map<EffectCategory, Glib::ustring> category_names = {
        { EffectCategory::Effect,     _("Effect") },
        { EffectCategory::Compose,    _("Compositing") },
        { EffectCategory::Colors,     _("Color editing") },
        { EffectCategory::Generation, _("Generating") },
    };
    return category_names.at(category);
}

struct EffectMetadata {
    EffectCategory category;
    Glib::ustring icon_name;
    Glib::ustring tooltip;
};

static const std::map<Inkscape::Filters::FilterPrimitiveType, EffectMetadata>& get_effects() {
    static std::map<Inkscape::Filters::FilterPrimitiveType, EffectMetadata> effects = {
    { NR_FILTER_GAUSSIANBLUR,      { EffectCategory::Effect,     "feGaussianBlur-icon",
        _("Uniformly blurs its input. Commonly used together with Offset to create a drop shadow effect.") }},
    { NR_FILTER_MORPHOLOGY,        { EffectCategory::Effect,     "feMorphology-icon",
        _("Provides erode and dilate effects. For single-color objects erode makes the object thinner and dilate makes it thicker.") }},
    { NR_FILTER_OFFSET,            { EffectCategory::Effect,     "feOffset-icon",
        _("Offsets the input by an user-defined amount. Commonly used for drop shadow effects.") }},
    { NR_FILTER_CONVOLVEMATRIX,    { EffectCategory::Effect,     "feConvolveMatrix-icon",
        _("Performs a convolution on the input image enabling effects like blur, sharpening, embossing and edge detection.") }},
    { NR_FILTER_DISPLACEMENTMAP,   { EffectCategory::Effect,     "feDisplacementMap-icon",
        _("Displaces pixels from the first input using the second as a map of displacement intensity. Classical examples are whirl and pinch effects.") }},
    { NR_FILTER_TILE,              { EffectCategory::Effect,     "feTile-icon",
        _("Tiles a region with an input graphic. The source tile is defined by the filter primitive subregion of the input.") }},
    { NR_FILTER_COMPOSITE,         { EffectCategory::Compose,    "feComposite-icon",
        _("Composites two images using one of the Porter-Duff blending modes or the arithmetic mode described in SVG standard.") }},
    { NR_FILTER_BLEND,             { EffectCategory::Compose,    "feBlend-icon",
        _("Provides image blending modes, such as screen, multiply, darken and lighten.") }},
    { NR_FILTER_MERGE,             { EffectCategory::Compose,    "feMerge-icon",
        _("Merges multiple inputs using normal alpha compositing. Equivalent to using several Blend primitives in 'normal' mode or several Composite primitives in 'over' mode.") }},
    { NR_FILTER_COLORMATRIX,       { EffectCategory::Colors,     "feColorMatrix-icon",
        _("Modifies pixel colors based on a transformation matrix. Useful for adjusting color hue and saturation.") }},
    { NR_FILTER_COMPONENTTRANSFER, { EffectCategory::Colors,     "feComponentTransfer-icon",
        _("Manipulates color components according to particular transfer functions. Useful for brightness and contrast adjustment, color balance, and thresholding.") }},
    { NR_FILTER_DIFFUSELIGHTING,   { EffectCategory::Colors,     "feDiffuseLighting-icon",
        _("Creates \"embossed\" shadings.  The input's alpha channel is used to provide depth information: higher opacity areas are raised toward the viewer and lower opacity areas recede away from the viewer.") }},
    { NR_FILTER_SPECULARLIGHTING,  { EffectCategory::Colors,     "feSpecularLighting-icon",
        _("Creates \"embossed\" shadings.  The input's alpha channel is used to provide depth information: higher opacity areas are raised toward the viewer and lower opacity areas recede away from the viewer.") }},
    { NR_FILTER_FLOOD,             { EffectCategory::Generation, "feFlood-icon",
        _("Fills the region with a given color and opacity. Often used as input to other filters to apply color to a graphic.") }},
    { NR_FILTER_IMAGE,             { EffectCategory::Generation, "feImage-icon",
        _("Fills the region with graphics from an external file or from another portion of the document.") }},
    { NR_FILTER_TURBULENCE,        { EffectCategory::Generation, "feTurbulence-icon",
        _("Renders Perlin noise, which is useful to generate textures such as clouds, fire, smoke, marble or granite.") }},
    };
    return effects;
}

// populate popup with filter effects and completion list for a search box
void FilterEffectsDialog::add_effects(Inkscape::UI::Widget::CompletionPopup& popup, bool symbolic) {
    auto& menu = popup.get_menu();

    struct Effect {
        Inkscape::Filters::FilterPrimitiveType type;
        Glib::ustring label;
        EffectCategory category;
        Glib::ustring icon_name;
        Glib::ustring tooltip;
    };
    std::vector<Effect> effects;
    effects.reserve(get_effects().size());
    for (auto&& effect : get_effects()) {
        effects.push_back({
            effect.first,
            _(FPConverter.get_label(effect.first).c_str()),
            effect.second.category,
            effect.second.icon_name,
            effect.second.tooltip
        });
    }
    std::sort(begin(effects), end(effects), [=](auto&& a, auto&& b) {
        if (a.category != b.category) {
            return a.category < b.category;
        }
        return a.label < b.label;
    });

    popup.clear_completion_list();

    // 2-column menu
    Inkscape::UI::ColumnMenuBuilder<EffectCategory> builder{menu, 2, Gtk::IconSize::LARGE};
    for (auto const &effect : effects) {
        // build popup menu
        auto const &type = effect.type;
        auto const menuitem = builder.add_item(effect.label, effect.category, effect.tooltip,
                                               effect.icon_name, true, true,
                                               [=, this]{ add_filter_primitive(type); });
        auto const id = static_cast<int>(type);
        menuitem->signal_query_tooltip().connect([=, this] (int x, int y, bool kbd, const Glib::RefPtr<Gtk::Tooltip>& tooltipw) {
            return sp_query_custom_tooltip(this, x, y, kbd, tooltipw, id, effect.tooltip, effect.icon_name);
        }, false); // before
        if (builder.new_section()) {
            builder.set_section(get_category_name(effect.category));
        }
        // build completion list
        popup.add_to_completion_list(id, effect.label, effect.icon_name + (symbolic ? "-symbolic" : ""));
    }
    if (symbolic) {
        menu.add_css_class("symbolic");
    }
}

/*** FilterEffectsDialog ***/

FilterEffectsDialog::FilterEffectsDialog()
    : DialogBase("/dialogs/filtereffects", "FilterEffects"),
    _builder(create_builder("dialog-filter-editor.glade")),
    _paned(get_widget<Gtk::Paned>(_builder, "paned")),
    _main_grid(get_widget<Gtk::Grid>(_builder, "main")),
    _params_box(get_widget<Gtk::Box>(_builder, "params")),
    _search_box(get_widget<Gtk::Box>(_builder, "search")),
    _search_wide_box(get_widget<Gtk::Box>(_builder, "search-wide")),
    _filter_wnd(get_widget<Gtk::ScrolledWindow>(_builder, "filter")),
    _cur_filter_btn(get_widget<Gtk::CheckButton>(_builder, "label"))
    , _add_primitive_type(FPConverter)
    , _add_primitive(_("Add Effect:"))
    , _empty_settings("", Gtk::Align::CENTER)
    , _no_filter_selected(_("No filter selected"), Gtk::Align::START)
    , _settings_initialized(false)
    , _locked(false)
    , _attr_lock(false)
    , _filter_modifier(*this, _builder)
    , _primitive_list(*this)
    , _settings_effect(Gtk::Orientation::VERTICAL)
    , _settings_filter(Gtk::Orientation::VERTICAL)
{
    _settings = std::make_unique<Settings>(*this, _settings_effect,
                                           [this](auto const a){ set_attr_direct(a); },
                                           NR_FILTER_ENDPRIMITIVETYPE);
    _cur_effect_name = &get_widget<Gtk::Label>(_builder, "cur-effect");
    _settings->_size_group->add_widget(*_cur_effect_name);
    _filter_general_settings = std::make_unique<Settings>(*this, _settings_filter,
                                                          [this](auto const a){ set_filternode_attr(a); }, 1);

    // Initialize widget hierarchy
    _primitive_box = &get_widget<Gtk::ScrolledWindow>(_builder, "filter");
    _primitive_list.set_enable_search(false);
    _primitive_box->set_child(_primitive_list);

    auto symbolic = Inkscape::Preferences::get()->getBool("/theme/symbolicIcons", true);
    add_effects(_effects_popup, symbolic);
    _effects_popup.get_entry().set_placeholder_text(_("Add effect"));
    _effects_popup.on_match_selected().connect(
        [this](int const id){ add_filter_primitive(static_cast<FilterPrimitiveType>(id)); });
    UI::pack_start(_search_box, _effects_popup);

    _settings_effect.set_valign(Gtk::Align::FILL);
    _params_box.append(_settings_effect);

    _settings_filter.set_margin(5);
    get_widget<Gtk::Popover>(_builder, "gen-settings").set_child(_settings_filter);

    get_widget<Gtk::Popover>(_builder, "info-popover").signal_show().connect([this]{
        if (auto prim = _primitive_list.get_selected()) {
            if (prim->getRepr()) {
                auto id = FPConverter.get_id_from_key(prim->getRepr()->name());
                const auto& effect = get_effects().at(id);
                get_widget<Gtk::Image>(_builder, "effect-icon").set_from_icon_name(effect.icon_name);
                auto buffer = get_widget<Gtk::TextView>(_builder, "effect-info").get_buffer();
                buffer->set_text("");
                buffer->insert_markup(buffer->begin(), effect.tooltip);
                get_widget<Gtk::TextView>(_builder, "effect-desc").get_buffer()->set_text("");
            }
        }
    });

    _primitive_list.signal_primitive_changed().connect([this]{ update_settings_view(); });

    _cur_filter_toggle = _cur_filter_btn.signal_toggled().connect([this]{
        _filter_modifier.toggle_current_filter();
    });

    auto update_checkbox = [this]{
        auto active = _filter_modifier.is_selected_filter_active();
        _cur_filter_toggle.block();
        _cur_filter_btn.set_active(active);
        _cur_filter_toggle.unblock();
    };

    auto update_widgets = [=, this]{
        auto& opt = get_widget<Gtk::MenuButton>(_builder, "filter-opt");
        _primitive_list.update();
        Glib::ustring name;
        if (auto filter = _filter_modifier.get_selected_filter()) {
            name = get_filter_name(filter);
            _effects_popup.set_sensitive();
            _cur_filter_btn.set_sensitive(); // ideally this should also be selection-dependent
            opt.set_sensitive();
        } else {
            name = "-";
            _effects_popup.set_sensitive(false);
            _cur_filter_btn.set_sensitive(false);
            opt.set_sensitive(false);
        }
        get_widget<Gtk::Label>(_builder, "filter-name").set_label(name);
        update_checkbox();
        update_settings_view();
    };

    //TODO: adding animated GIFs to the info popup once they are ready:
    // auto a = Gdk::PixbufAnimation::create_from_file("/Users/mike/blur-effect.gif");
    // get_widget<Gtk::Image>(_builder, "effect-image").property_pixbuf_animation().set_value(a);

    init_settings_widgets();

    _filter_modifier.signal_filter_changed().connect([=](){
        update_widgets();
    });

    _filter_modifier.signal_filters_updated().connect([=](){
        update_checkbox();
    });

    _add_primitive.signal_clicked().connect(sigc::mem_fun(*this, &FilterEffectsDialog::add_primitive));
    _primitive_list.set_menu(sigc::mem_fun(*this, &FilterEffectsDialog::duplicate_primitive),
                             sigc::mem_fun(_primitive_list, &PrimitiveList::remove_selected));

    get_widget<Gtk::Button>(_builder, "new-filter").signal_clicked().connect([this]{ _filter_modifier.add_filter(); });
    append(_bin);
    _bin.set_expand(true);
    _bin.set_child(_popoverbin);
    _popoverbin.setChild(&_main_grid);

    get_widget<Gtk::Button>(_builder, "dup-btn").signal_clicked().connect([this]{ duplicate_primitive(); });
    get_widget<Gtk::Button>(_builder, "del-btn").signal_clicked().connect([this]{ _primitive_list.remove_selected(); });
    // get_widget<Gtk::Button>(_builder, "info-btn").signal_clicked().connect([this]{ /* todo */ });

    _show_sources = &get_widget<Gtk::ToggleButton>(_builder, "btn-connect");
    auto set_inputs = [this](bool const all){
        int count = all ? FPInputConverter._length : 2;
        _primitive_list.set_inputs_count(count);
        // full rebuild: this is what it takes to make cell renderer new min width into account to adjust scrollbar
        _primitive_list.update();
    };
    auto show_all_sources = Inkscape::Preferences::get()->getBool(prefs_path + "/dialogs/filters/showAllSources", false);
    _show_sources->set_active(show_all_sources);
    set_inputs(show_all_sources);
    _show_sources->signal_toggled().connect([=, this]{
        bool const show_all = _show_sources->get_active();
        set_inputs(show_all);
        Inkscape::Preferences::get()->setBool(prefs_path + "/dialogs/filters/showAllSources", show_all);
    });

    _paned.set_position(Inkscape::Preferences::get()->getIntLimited(prefs_path + "/handlePos", 200, 10, 9999));
    _paned.property_position().signal_changed().connect([this]{
        Inkscape::Preferences::get()->setInt(prefs_path + "/handlePos", _paned.get_position());
    });

    _primitive_list.update();

    // reading minimal width at this point should reflect space needed for fitting effect parameters panel
    Gtk::Requisition minimum_size, natural_size;
    get_preferred_size(minimum_size, natural_size);
    int min_width = minimum_size.get_width();
    _effects_popup.get_preferred_size(minimum_size, natural_size);
    auto const min_effects = minimum_size.get_width();
    // calculate threshold/minimum width of filters dialog in horizontal layout;
    // use this size to decide where transition from vertical to horizontal layout is;
    // if this size is too small dialog can get stuck in horizontal layout - users won't be able
    // to make it narrow again, due to min dialog size enforced by GTK
    int threshold_width = min_width + min_effects * 3;

    // two alternative layout arrangements depending on the dialog size;
    // one is tall and narrow with widgets in one column, while the other
    // is for wide dialogs with filter parameters and effects side by side
    _bin.connectBeforeResize([=, this] (int width, int height, int baseline) {
        if (width < 10 || height < 10) return;

        double const ratio = width / static_cast<double>(height);

        constexpr double hysteresis = 0.01;
        if (ratio < 1 - hysteresis || width <= threshold_width) {
            // make narrow/tall
            if (!_narrow_dialog) {
                _main_grid.remove(_filter_wnd);
                _search_wide_box.remove(_effects_popup);
                _paned.set_start_child(_filter_wnd);
                UI::pack_start(_search_box, _effects_popup);
                _paned.set_size_request();
                get_widget<Gtk::Box>(_builder, "connect-box-wide").remove(*_show_sources);
                get_widget<Gtk::Box>(_builder, "connect-box").append(*_show_sources);
                _narrow_dialog = true;
            }
        } else if (ratio > 1 + hysteresis && width > threshold_width) {
            // make wide/short
            if (_narrow_dialog) {
                _paned.property_start_child().set_value(nullptr);
                _search_box.remove(_effects_popup);
                _main_grid.attach(_filter_wnd, 2, 1, 1, 2);
                UI::pack_start(_search_wide_box, _effects_popup);
                _paned.set_size_request(min_width);
                get_widget<Gtk::Box>(_builder, "connect-box").remove(*_show_sources);
                get_widget<Gtk::Box>(_builder, "connect-box-wide").append(*_show_sources);
                _narrow_dialog = false;
            }
        }
    });

    update_widgets();
    update();
    update_settings_view();
}

FilterEffectsDialog::~FilterEffectsDialog() = default;

void FilterEffectsDialog::documentReplaced()
{
    _resource_changed.disconnect();
    if (auto document = getDocument()) {
        auto const update_filters = [this]{ _filter_modifier.update_filters(); };
        _resource_changed = document->connectResourcesChanged("filter", update_filters);
        update_filters();
    }
}

void FilterEffectsDialog::selectionChanged(Inkscape::Selection *selection)
{
    if (selection) {
        _filter_modifier.update_selection(selection);
    }
}

void FilterEffectsDialog::selectionModified(Inkscape::Selection *selection, guint flags)
{
    if (flags & (SP_OBJECT_MODIFIED_FLAG |
                 SP_OBJECT_PARENT_MODIFIED_FLAG |
                 SP_OBJECT_STYLE_MODIFIED_FLAG)) {
        _filter_modifier.update_selection(selection);
    }
}

void FilterEffectsDialog::set_attrs_locked(const bool l)
{
    _locked = l;
}

void FilterEffectsDialog::init_settings_widgets()
{
    // TODO: Find better range/climb-rate/digits values for the SpinScales,
    //       most of the current values are complete guesses!

    _empty_settings.set_sensitive(false);
    UI::pack_start(_settings_effect, _empty_settings);

    _no_filter_selected.set_sensitive(false);
    UI::pack_start(_settings_filter, _no_filter_selected);
    _settings_initialized = true;

    _filter_general_settings->type(0);
    auto _region_auto = _filter_general_settings->add_checkbutton(true, SPAttr::AUTO_REGION, _("Automatic Region"), "true", "false", _("If unset, the coordinates and dimensions won't be updated automatically."));
    _region_pos = _filter_general_settings->add_multispinbutton(/*default x:*/ (double) -0.1, /*default y:*/ (double) -0.1, SPAttr::X, SPAttr::Y, _("Coordinates:"), -100, 100, 0.01, 0.1, 2, _("X coordinate of the left corners of filter effects region"), _("Y coordinate of the upper corners of filter effects region"));
    _region_size = _filter_general_settings->add_multispinbutton(/*default width:*/ (double) 1.2, /*default height:*/ (double) 1.2, SPAttr::WIDTH, SPAttr::HEIGHT, _("Dimensions:"), 0, 1000, 0.01, 0.1, 2, _("Width of filter effects region"), _("Height of filter effects region"));
    _region_auto->signal_attr_changed().connect( sigc::bind(sigc::mem_fun(*this, &FilterEffectsDialog::update_automatic_region), _region_auto));

    _settings->type(NR_FILTER_BLEND);
    _settings->add_combo(SP_CSS_BLEND_NORMAL, SPAttr::MODE, _("Mode:"), SPBlendModeConverter);

    _settings->type(NR_FILTER_COLORMATRIX);
    ComboBoxEnum<FilterColorMatrixType>* colmat = _settings->add_combo(COLORMATRIX_MATRIX, SPAttr::TYPE, _("Type:"), ColorMatrixTypeConverter, _("Indicates the type of matrix operation. The keyword 'matrix' indicates that a full 5x4 matrix of values will be provided. The other keywords represent convenience shortcuts to allow commonly used color operations to be performed without specifying a complete matrix."));
    _color_matrix_values = _settings->add_colormatrixvalues(_("Value(s):"));
    colmat->signal_attr_changed().connect(sigc::mem_fun(*this, &FilterEffectsDialog::update_color_matrix));

    _settings->type(NR_FILTER_COMPONENTTRANSFER);
    // TRANSLATORS: Abbreviation for red color channel in RGBA
    _settings->add_componenttransfervalues(C_("color", "R:"), SPFeFuncNode::R);
    // TRANSLATORS: Abbreviation for green color channel in RGBA
    _settings->add_componenttransfervalues(C_("color", "G:"), SPFeFuncNode::G);
    // TRANSLATORS: Abbreviation for blue color channel in RGBA
    _settings->add_componenttransfervalues(C_("color", "B:"), SPFeFuncNode::B);
    // TRANSLATORS: Abbreviation for alpha channel in RGBA
    _settings->add_componenttransfervalues(C_("color", "A:"), SPFeFuncNode::A);

    _settings->type(NR_FILTER_COMPOSITE);
    _settings->add_combo(COMPOSITE_OVER, SPAttr::OPERATOR, _("Operator:"), CompositeOperatorConverter);
    _k1 = _settings->add_spinscale(0, SPAttr::K1, _("K1:"), -10, 10, 0.1, 0.01, 2, _("If the arithmetic operation is chosen, each result pixel is computed using the formula k1*i1*i2 + k2*i1 + k3*i2 + k4 where i1 and i2 are the pixel values of the first and second inputs respectively."));
    _k2 = _settings->add_spinscale(0, SPAttr::K2, _("K2:"), -10, 10, 0.1, 0.01, 2, _("If the arithmetic operation is chosen, each result pixel is computed using the formula k1*i1*i2 + k2*i1 + k3*i2 + k4 where i1 and i2 are the pixel values of the first and second inputs respectively."));
    _k3 = _settings->add_spinscale(0, SPAttr::K3, _("K3:"), -10, 10, 0.1, 0.01, 2, _("If the arithmetic operation is chosen, each result pixel is computed using the formula k1*i1*i2 + k2*i1 + k3*i2 + k4 where i1 and i2 are the pixel values of the first and second inputs respectively."));
    _k4 = _settings->add_spinscale(0, SPAttr::K4, _("K4:"), -10, 10, 0.1, 0.01, 2, _("If the arithmetic operation is chosen, each result pixel is computed using the formula k1*i1*i2 + k2*i1 + k3*i2 + k4 where i1 and i2 are the pixel values of the first and second inputs respectively."));

    _settings->type(NR_FILTER_CONVOLVEMATRIX);
    _convolve_order = _settings->add_dualspinbutton((char*)"3", SPAttr::ORDER, _("Size:"), 1, max_convolution_kernel_size, 1, 1, 0, _("width of the convolve matrix"), _("height of the convolve matrix"));
    _convolve_target = _settings->add_multispinbutton(/*default x:*/ (double) 0, /*default y:*/ (double) 0, SPAttr::TARGETX, SPAttr::TARGETY, _("Target:"), 0, max_convolution_kernel_size - 1, 1, 1, 0, _("X coordinate of the target point in the convolve matrix. The convolution is applied to pixels around this point."), _("Y coordinate of the target point in the convolve matrix. The convolution is applied to pixels around this point."));
    //TRANSLATORS: for info on "Kernel", see http://en.wikipedia.org/wiki/Kernel_(matrix)
    _convolve_matrix = _settings->add_matrix(SPAttr::KERNELMATRIX, _("Kernel:"), _("This matrix describes the convolve operation that is applied to the input image in order to calculate the pixel colors at the output. Different arrangements of values in this matrix result in various possible visual effects. An identity matrix would lead to a motion blur effect (parallel to the matrix diagonal) while a matrix filled with a constant non-zero value would lead to a common blur effect."));
    _convolve_order->signal_attr_changed().connect(sigc::mem_fun(*this, &FilterEffectsDialog::convolve_order_changed));
    _settings->add_spinscale(0, SPAttr::DIVISOR, _("Divisor:"), 0, 1000, 1, 0.1, 2, _("After applying the kernelMatrix to the input image to yield a number, that number is divided by divisor to yield the final destination color value. A divisor that is the sum of all the matrix values tends to have an evening effect on the overall color intensity of the result."));
    _settings->add_spinscale(0, SPAttr::BIAS, _("Bias:"), -10, 10, 0.1, 0.5, 2, _("This value is added to each component. This is useful to define a constant value as the zero response of the filter."));
    _settings->add_combo(CONVOLVEMATRIX_EDGEMODE_NONE, SPAttr::EDGEMODE, _("Edge Mode:"), ConvolveMatrixEdgeModeConverter, _("Determines how to extend the input image as necessary with color values so that the matrix operations can be applied when the kernel is positioned at or near the edge of the input image."));
    _settings->add_checkbutton(false, SPAttr::PRESERVEALPHA, _("Preserve Alpha"), "true", "false", _("If set, the alpha channel won't be altered by this filter primitive."));

    _settings->type(NR_FILTER_DIFFUSELIGHTING);
    _settings->add_color(/*default: white*/ 0xffffffff, SPAttr::LIGHTING_COLOR, _("Diffuse Color:"), _("Defines the color of the light source"));
    _settings->add_spinscale(1, SPAttr::SURFACESCALE, _("Surface Scale:"), -5, 5, 0.01, 0.001, 3, _("This value amplifies the heights of the bump map defined by the input alpha channel"));
    _settings->add_spinscale(1, SPAttr::DIFFUSECONSTANT, _("Constant:"), 0, 5, 0.1, 0.01, 2, _("This constant affects the Phong lighting model."));
    // deprecated (https://developer.mozilla.org/en-US/docs/Web/SVG/Attribute/kernelUnitLength)
    // _settings->add_dualspinscale(SPAttr::KERNELUNITLENGTH, _("Kernel Unit Length:"), 0.01, 10, 1, 0.01, 1);
    _settings->add_lightsource();

    _settings->type(NR_FILTER_DISPLACEMENTMAP);
    _settings->add_spinscale(0, SPAttr::SCALE, _("Scale:"), 0, 100, 1, 0.01, 1, _("This defines the intensity of the displacement effect."));
    _settings->add_combo(DISPLACEMENTMAP_CHANNEL_ALPHA, SPAttr::XCHANNELSELECTOR, _("X displacement:"), DisplacementMapChannelConverter, _("Color component that controls the displacement in the X direction"));
    _settings->add_combo(DISPLACEMENTMAP_CHANNEL_ALPHA, SPAttr::YCHANNELSELECTOR, _("Y displacement:"), DisplacementMapChannelConverter, _("Color component that controls the displacement in the Y direction"));

    _settings->type(NR_FILTER_FLOOD);
    _settings->add_color(/*default: black*/ 0, SPAttr::FLOOD_COLOR, _("Color:"), _("The whole filter region will be filled with this color."));
    _settings->add_spinscale(1, SPAttr::FLOOD_OPACITY, _("Opacity:"), 0, 1, 0.1, 0.01, 2);

    _settings->type(NR_FILTER_GAUSSIANBLUR);
    _settings->add_dualspinscale(SPAttr::STDDEVIATION, _("Size:"), 0, 100, 1, 0.01, 2, _("The standard deviation for the blur operation."));

    _settings->type(NR_FILTER_MERGE);
    _settings->add_no_params();

    _settings->type(NR_FILTER_MORPHOLOGY);
    _settings->add_combo(MORPHOLOGY_OPERATOR_ERODE, SPAttr::OPERATOR, _("Operator:"), MorphologyOperatorConverter, _("Erode: performs \"thinning\" of input image.\nDilate: performs \"fattening\" of input image."));
    _settings->add_dualspinscale(SPAttr::RADIUS, _("Radius:"), 0, 100, 1, 0.01, 1);

    _settings->type(NR_FILTER_IMAGE);
    _settings->add_fileorelement(SPAttr::XLINK_HREF, _("Source of Image:"));
    _image_x = _settings->add_entry(SPAttr::X, _("Position X:"), _("Position X"));
    _image_x->signal_attr_changed().connect(sigc::mem_fun(*this, &FilterEffectsDialog::image_x_changed));
    //This is commented out because we want the default empty value of X or Y and couldn't get it from SpinButton
    //_image_y = _settings->add_spinbutton(0, SPAttr::Y, _("Y:"), -DBL_MAX, DBL_MAX, 1, 1, 5, _("Y"));
    _image_y = _settings->add_entry(SPAttr::Y, _("Position Y:"), _("Position Y"));
    _image_y->signal_attr_changed().connect(sigc::mem_fun(*this, &FilterEffectsDialog::image_y_changed));
    _settings->add_entry(SPAttr::WIDTH, _("Width:"), _("Width"));
    _settings->add_entry(SPAttr::HEIGHT, _("Height:"), _("Height"));

    _settings->type(NR_FILTER_OFFSET);
    _settings->add_checkbutton(false, SPAttr::PRESERVEALPHA, _("Preserve Alpha"), "true", "false", _("If set, the alpha channel won't be altered by this filter primitive."));
    _settings->add_spinscale(0, SPAttr::DX, _("Delta X:"), -100, 100, 1, 0.01, 2, _("This is how far the input image gets shifted to the right"));
    _settings->add_spinscale(0, SPAttr::DY, _("Delta Y:"), -100, 100, 1, 0.01, 2, _("This is how far the input image gets shifted downwards"));

    _settings->type(NR_FILTER_SPECULARLIGHTING);
    _settings->add_color(/*default: white*/ 0xffffffff, SPAttr::LIGHTING_COLOR, _("Specular Color:"), _("Defines the color of the light source"));
    _settings->add_spinscale(1, SPAttr::SURFACESCALE, _("Surface Scale:"), -5, 5, 0.1, 0.01, 2, _("This value amplifies the heights of the bump map defined by the input alpha channel"));
    _settings->add_spinscale(1, SPAttr::SPECULARCONSTANT, _("Constant:"), 0, 5, 0.1, 0.01, 2, _("This constant affects the Phong lighting model."));
    _settings->add_spinscale(1, SPAttr::SPECULAREXPONENT, _("Exponent:"), 1, 50, 1, 0.01, 1, _("Exponent for specular term, larger is more \"shiny\"."));
    // deprecated (https://developer.mozilla.org/en-US/docs/Web/SVG/Attribute/kernelUnitLength)
    // _settings->add_dualspinscale(SPAttr::KERNELUNITLENGTH, _("Kernel Unit Length:"), 0.01, 10, 1, 0.01, 1);
    _settings->add_lightsource();

    _settings->type(NR_FILTER_TILE);
    // add some filter primitive attributes: https://drafts.fxtf.org/filter-effects/#feTileElement
    // issue: https://gitlab.com/inkscape/inkscape/-/issues/1417
    _settings->add_entry(SPAttr::X, _("Position X:"), _("Position X"));
    _settings->add_entry(SPAttr::Y, _("Position Y:"), _("Position Y"));
    _settings->add_entry(SPAttr::WIDTH, _("Width:"), _("Width"));
    _settings->add_entry(SPAttr::HEIGHT, _("Height:"), _("Height"));

    _settings->type(NR_FILTER_TURBULENCE);
//    _settings->add_checkbutton(false, SPAttr::STITCHTILES, _("Stitch Tiles"), "stitch", "noStitch");
    _settings->add_combo(TURBULENCE_TURBULENCE, SPAttr::TYPE, _("Type:"), TurbulenceTypeConverter, _("Indicates whether the filter primitive should perform a noise or turbulence function."));
    _settings->add_dualspinscale(SPAttr::BASEFREQUENCY, _("Size:"), 0.001, 10, 0.001, 0.1, 3);
    _settings->add_spinscale(1, SPAttr::NUMOCTAVES, _("Detail:"), 1, 10, 1, 1, 0);
    _settings->add_spinscale(0, SPAttr::SEED, _("Seed:"), 0, 1000, 1, 1, 0, _("The starting number for the pseudo random number generator."));
}

void FilterEffectsDialog::add_filter_primitive(Filters::FilterPrimitiveType type) {
    if (auto filter = _filter_modifier.get_selected_filter()) {
        SPFilterPrimitive* prim = filter_add_primitive(filter, type);
        _primitive_list.select(prim);
        DocumentUndo::done(filter->document, _("Add filter primitive"), INKSCAPE_ICON("dialog-filters"));
    }
}

void FilterEffectsDialog::add_primitive()
{
    auto id = _add_primitive_type.get_selected_id();
    if (id.has_value()) {
        add_filter_primitive(*id);
    }
}

void FilterEffectsDialog::duplicate_primitive()
{
    SPFilter* filter = _filter_modifier.get_selected_filter();
    SPFilterPrimitive* origprim = _primitive_list.get_selected();

    if (filter && origprim) {
        Inkscape::XML::Node *repr;
        repr = origprim->getRepr()->duplicate(origprim->getRepr()->document());
        filter->getRepr()->appendChild(repr);

        DocumentUndo::done(filter->document, _("Duplicate filter primitive"), INKSCAPE_ICON("dialog-filters"));

        _primitive_list.update();
    }
}

void FilterEffectsDialog::convolve_order_changed()
{
    _convolve_matrix->set_from_attribute(_primitive_list.get_selected());
    // MultiSpinButtons orders widgets backwards: so use index 1 and 0
    _convolve_target->get_spinbuttons()[1]->get_adjustment()->set_upper(_convolve_order->get_spinbutton1().get_value() - 1);
    _convolve_target->get_spinbuttons()[0]->get_adjustment()->set_upper(_convolve_order->get_spinbutton2().get_value() - 1);
}

bool number_or_empy(const Glib::ustring& text) {
    if (text.empty()) {
        return true;
    }
    double n = g_strtod(text.c_str(), nullptr);
    if (n == 0.0 && strcmp(text.c_str(), "0") != 0 && strcmp(text.c_str(), "0.0") != 0) {
        return false;
    }
    else {
        return true;
    }
}

void FilterEffectsDialog::image_x_changed()
{
    if (number_or_empy(_image_x->get_text())) {
        _image_x->set_from_attribute(_primitive_list.get_selected());
    }
}

void FilterEffectsDialog::image_y_changed()
{
    if (number_or_empy(_image_y->get_text())) {
        _image_y->set_from_attribute(_primitive_list.get_selected());
    }
}

void FilterEffectsDialog::set_attr_direct(const AttrWidget* input)
{
    set_attr(_primitive_list.get_selected(), input->get_attribute(), input->get_as_attribute().c_str());
}

void FilterEffectsDialog::set_filternode_attr(const AttrWidget* input)
{
    if(!_locked) {
        _attr_lock = true;
        SPFilter *filter = _filter_modifier.get_selected_filter();
        const gchar* name = (const gchar*)sp_attribute_name(input->get_attribute());
        if (filter && name && filter->getRepr()){
            filter->setAttributeOrRemoveIfEmpty(name, input->get_as_attribute());
            filter->requestModified(SP_OBJECT_MODIFIED_FLAG);
        }
        _attr_lock = false;
    }
}

void FilterEffectsDialog::set_child_attr_direct(const AttrWidget* input)
{
    set_attr(_primitive_list.get_selected()->firstChild(), input->get_attribute(), input->get_as_attribute().c_str());
}

void FilterEffectsDialog::set_attr(SPObject* o, const SPAttr attr, const gchar* val)
{
    if(!_locked) {
        _attr_lock = true;

        SPFilter *filter = _filter_modifier.get_selected_filter();
        const gchar* name = (const gchar*)sp_attribute_name(attr);
        if(filter && name && o) {
            update_settings_sensitivity();

            o->setAttribute(name, val);
            filter->requestModified(SP_OBJECT_MODIFIED_FLAG);

            Glib::ustring undokey = "filtereffects:";
            undokey += name;
            DocumentUndo::maybeDone(filter->document, undokey.c_str(), _("Set filter primitive attribute"), INKSCAPE_ICON("dialog-filters"));
        }

        _attr_lock = false;
    }
}

void FilterEffectsDialog::update_filter_general_settings_view()
{
    if(_settings_initialized != true) return;

    if(!_locked) {
        _attr_lock = true;

        SPFilter* filter = _filter_modifier.get_selected_filter();

        if(filter) {
            _filter_general_settings->show_and_update(0, filter);
            _no_filter_selected.set_visible(false);
        } else {
            UI::get_children(_settings_filter).at(0)->set_visible(false);
            _no_filter_selected.set_visible(true);
        }

        _attr_lock = false;
    }
}

void FilterEffectsDialog::update_settings_view()
{
    update_settings_sensitivity();

    if (_attr_lock)
        return;

    // selected effect parameters

    for (auto const i : UI::get_children(_settings_effect)) {
        i->set_visible(false);
    }

    SPFilterPrimitive* prim = _primitive_list.get_selected();
    auto& header = get_widget<Gtk::Box>(_builder, "effect-header");
    SPFilter* filter = _filter_modifier.get_selected_filter();
    bool present = _filter_modifier.filters_present();

    if (prim && prim->getRepr()) {
        //XML Tree being used directly here while it shouldn't be.
        auto id = FPConverter.get_id_from_key(prim->getRepr()->name());
        _settings->show_and_update(id, prim);
        _empty_settings.set_visible(false);
        _cur_effect_name->set_text(_(FPConverter.get_label(id).c_str()));
        header.set_visible(true);
    }
    else {
        if (filter) {
            _empty_settings.set_text(_("Add effect from the search bar"));
        }
        else if (present) {
            _empty_settings.set_text(_("Select a filter"));
        }
        else {
            _empty_settings.set_text(_("No filters in the document"));
        }
        _empty_settings.set_visible(true);
        _cur_effect_name->set_text(Glib::ustring());
        header.set_visible(false);
    }

    // current filter parameters (area size)

    UI::get_children(_settings_filter).at(0)->set_visible(false);
    _no_filter_selected.set_visible(true);

    if (filter) {
        _filter_general_settings->show_and_update(0, filter);
        _no_filter_selected.set_visible(false);
    }
}

void FilterEffectsDialog::update_settings_sensitivity()
{
    SPFilterPrimitive* prim = _primitive_list.get_selected();
    const bool use_k = is<SPFeComposite>(prim) && cast<SPFeComposite>(prim)->get_composite_operator() == COMPOSITE_ARITHMETIC;
    _k1->set_sensitive(use_k);
    _k2->set_sensitive(use_k);
    _k3->set_sensitive(use_k);
    _k4->set_sensitive(use_k);
}

void FilterEffectsDialog::update_color_matrix()
{
    _color_matrix_values->set_from_attribute(_primitive_list.get_selected());
}

void FilterEffectsDialog::update_automatic_region(Gtk::CheckButton *btn)
{
    bool automatic = btn->get_active();
    _region_pos->set_sensitive(!automatic);
    _region_size->set_sensitive(!automatic);
}

} // namespace Inkscape::UI::Dialog

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
