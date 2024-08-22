// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Bitmap tracing settings dialog - second implementation.
 */
/* Authors:
 *   Marc Jeanmougin <marc.jeanmougin@telecom-paristech.fr>
 *   PBS <pbs3141@gmail.com>
 *
 * Copyright (C) 2019-2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "tracedialog.h"

#include <glibmm/i18n.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/notebook.h>
#include <gtkmm/radiobutton.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/progressbar.h>
#include <gtkmm/stack.h>

#include "desktop.h"
#include "io/resource.h"
#include "selection.h"
#include "trace/autotrace/inkscape-autotrace.h"
#include "trace/depixelize/inkscape-depixelize.h"
#include "trace/potrace/inkscape-potrace.h"
#include "ui/util.h"

// This maps the column ids in the glade file to useful enums
static const std::map<std::string, Inkscape::Trace::Potrace::TraceType> trace_types = {
    {"SS_BC", Inkscape::Trace::Potrace::TraceType::BRIGHTNESS},
    {"SS_ED", Inkscape::Trace::Potrace::TraceType::CANNY},
    {"SS_CQ", Inkscape::Trace::Potrace::TraceType::QUANT},
    {"SS_AT", Inkscape::Trace::Potrace::TraceType::AUTOTRACE_SINGLE},
    {"SS_CT", Inkscape::Trace::Potrace::TraceType::AUTOTRACE_CENTERLINE},

    {"MS_BS", Inkscape::Trace::Potrace::TraceType::BRIGHTNESS_MULTI},
    {"MS_C",  Inkscape::Trace::Potrace::TraceType::QUANT_COLOR},
    {"MS_BW", Inkscape::Trace::Potrace::TraceType::QUANT_MONO},
    {"MS_AT", Inkscape::Trace::Potrace::TraceType::AUTOTRACE_MULTI},
};

namespace Inkscape {
namespace UI {
namespace Dialog {

enum class EngineType
{
    Potrace,
    Autotrace,
    Depixelize
};

struct TraceData
{
    std::unique_ptr<Trace::TracingEngine> engine;
    bool sioxEnabled;
};

class TraceDialogImpl
    : public TraceDialog
{
public:
    TraceDialogImpl();
    ~TraceDialogImpl() override;

protected:
    void selectionModified(Selection *selection, unsigned flags) override;
    void selectionChanged(Selection *selection) override;

private:
    TraceData getTraceData() const;
    bool paintPreview(Cairo::RefPtr<Cairo::Context> const &cr);
    void setDefaults();
    void adjustParamsVisible();
    void onTraceClicked();
    void onAbortClicked();
    bool previewsEnabled() const;
    void schedulePreviewUpdate(int msecs, bool force = false);
    void updatePreview(bool force = false);
    void launchPreviewGeneration();

    // Handles to ongoing asynchronous computations.
    Trace::TraceFuture trace_future;
    Trace::TraceFuture preview_future;

    // Delayed preview generation.
    sigc::connection preview_timeout_conn;
    bool preview_pending_recompute = false;
    Glib::RefPtr<Gdk::Pixbuf> preview_image;

    Glib::RefPtr<Gtk::Builder> builder;
    Glib::RefPtr<Gtk::Adjustment> MS_scans, PA_curves, PA_islands, PA_sparse1, PA_sparse2, SS_AT_ET_T, SS_AT_FI_T, SS_BC_T, SS_CQ_T, SS_ED_T, optimize, smooth, speckles;
    Gtk::ComboBoxText *CBT_SS, *CBT_MS;
    Gtk::CheckButton *CB_invert, *CB_MS_smooth, *CB_MS_stack, *CB_MS_rb, *CB_speckles, *CB_smooth, *CB_optimize, *CB_PA_optimize, /* *CB_live,*/ *CB_SIOX;
    Gtk::CheckButton* CB_SIOX1;
    Gtk::CheckButton* CB_speckles1;
    Gtk::CheckButton* CB_smooth1;
    Gtk::CheckButton* CB_optimize1;
    Gtk::RadioButton *RB_PA_voronoi;
    Gtk::Button *B_RESET, *B_STOP, *B_OK, *B_Update;
    Gtk::Box *mainBox;
    Gtk::Notebook *choice_tab;
    Gtk::DrawingArea *previewArea;
    Gtk::Box* orient_box;
    Gtk::Frame* _preview_frame;
    Gtk::Grid* _param_grid;
    Gtk::CheckButton* _live_preview;
    Gtk::Stack *stack;
    Gtk::ProgressBar *progressbar;
    Gtk::Box *boxchild1, *boxchild2;
};

enum class Page
{
    SingleScan,
    MultiScan,
    PixelArt
};

TraceData TraceDialogImpl::getTraceData() const
{
    auto current_page = static_cast<Page>(choice_tab->get_current_page());

    auto cb_siox = current_page == Page::SingleScan ? CB_SIOX : CB_SIOX1;
    bool enable_siox = cb_siox->get_active();

    auto trace_type_str = current_page == Page::SingleScan ? CBT_SS->get_active_id() : CBT_MS->get_active_id();
    auto it = trace_types.find(trace_type_str);
    assert(it != trace_types.end());
    auto trace_type = it->second;

    EngineType engine_type;
    if (current_page == Page::PixelArt) {
        engine_type = EngineType::Depixelize;
    } else {
        switch (trace_type) {
            case Inkscape::Trace::Potrace::TraceType::AUTOTRACE_SINGLE:
            case Inkscape::Trace::Potrace::TraceType::AUTOTRACE_CENTERLINE:
            case Inkscape::Trace::Potrace::TraceType::AUTOTRACE_MULTI:
                engine_type = EngineType::Autotrace;
                break;
            default:
                engine_type = EngineType::Potrace;
                break;
        }
    }

    auto setup_potrace = [&, this] {
        auto eng = std::make_unique<Trace::Potrace::PotraceTracingEngine>(
            trace_type, CB_invert->get_active(), (int)SS_CQ_T->get_value(), SS_BC_T->get_value(),
            0, // Brightness floor
            SS_ED_T->get_value(), (int)MS_scans->get_value(), CB_MS_stack->get_active(), CB_MS_smooth->get_active(),
            CB_MS_rb->get_active());

        auto cb_optimize = current_page == Page::SingleScan ? CB_optimize : CB_optimize1;
        eng->setOptiCurve(cb_optimize->get_active());
        eng->setOptTolerance(optimize->get_value());

        auto cb_smooth = current_page == Page::SingleScan ? CB_smooth : CB_smooth1;
        eng->setAlphaMax(cb_smooth->get_active() ? smooth->get_value() : 0);

        auto cb_speckles = current_page == Page::SingleScan ? CB_speckles : CB_speckles1;
        eng->setTurdSize(cb_speckles->get_active() ? (int)speckles->get_value() : 0);

        return eng;
    };

    auto setup_autotrace = [&, this] {
        auto eng = std::make_unique<Trace::Autotrace::AutotraceTracingEngine>();

        switch (trace_type) {
            case Inkscape::Trace::Potrace::TraceType::AUTOTRACE_SINGLE:
                eng->setColorCount(2);
                break;
            case Inkscape::Trace::Potrace::TraceType::AUTOTRACE_CENTERLINE:
                eng->setColorCount(2);
                eng->setCenterLine(true);
                eng->setPreserveWidth(true);
                break;
            case Inkscape::Trace::Potrace::TraceType::AUTOTRACE_MULTI:
                eng->setColorCount((int)MS_scans->get_value() + 1);
                break;
            default:
                assert(false);
                break;
        }

        eng->setFilterIterations((int)SS_AT_FI_T->get_value());
        eng->setErrorThreshold(SS_AT_ET_T->get_value());

        return eng;
    };

    auto setup_depixelize = [this] {
        return std::make_unique<Trace::Depixelize::DepixelizeTracingEngine>(
            RB_PA_voronoi->get_active() ? Inkscape::Trace::Depixelize::TraceType::VORONOI : Inkscape::Trace::Depixelize::TraceType::BSPLINES,
            PA_curves->get_value(), (int) PA_islands->get_value(),
            (int) PA_sparse1->get_value(), PA_sparse2->get_value(),
            CB_PA_optimize->get_active());
    };

    TraceData data;
    switch (engine_type) {
        case EngineType::Potrace:    data.engine = setup_potrace();    break;
        case EngineType::Autotrace:  data.engine = setup_autotrace();  break;
        case EngineType::Depixelize: data.engine = setup_depixelize(); break;
        default: assert(false); break;
    }
    data.sioxEnabled = enable_siox;

    return data;
}

bool TraceDialogImpl::paintPreview(Cairo::RefPtr<Cairo::Context> const &cr)
{
    if (preview_image) {
        int width = preview_image->get_width();
        int height = preview_image->get_height();
        Gtk::Allocation const &allocation = previewArea->get_allocation();
        double scaleFX = (double)allocation.get_width() / width;
        double scaleFY = (double)allocation.get_height() / height;
        double scaleFactor = std::min(scaleFX, scaleFY);
        int newWidth = (double)width * scaleFactor;
        int newHeight = (double)height * scaleFactor;
        int offsetX = (allocation.get_width() - newWidth) / 2;
        int offsetY = (allocation.get_height() - newHeight) / 2;
        cr->scale(scaleFactor, scaleFactor);
        Gdk::Cairo::set_source_pixbuf(cr, preview_image, offsetX / scaleFactor, offsetY / scaleFactor);
        cr->paint();
    } else {
        cr->set_source_rgba(0, 0, 0, 0);
        cr->paint();
    }

    return false;
}

void TraceDialogImpl::selectionChanged(Inkscape::Selection *selection)
{
    updatePreview();
}

void TraceDialogImpl::selectionModified(Selection *selection, unsigned flags)
{
    auto mask = SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_PARENT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG;
    if ((flags & mask) == mask) {
        // All flags set - preview instantly.
        updatePreview();
    } else if (flags & mask) {
        // At least one flag set - preview after a long delay.
        schedulePreviewUpdate(1000);
    }
}

void TraceDialogImpl::setDefaults()
{
    MS_scans->set_value(8);
    PA_curves->set_value(1);
    PA_islands->set_value(5);
    PA_sparse1->set_value(4);
    PA_sparse2->set_value(1);
    SS_AT_FI_T->set_value(4);
    SS_AT_ET_T->set_value(2);
    SS_BC_T->set_value(0.45);
    SS_CQ_T->set_value(64);
    SS_ED_T->set_value(.65);
    optimize->set_value(0.2);
    smooth->set_value(1);
    speckles->set_value(2);
    CB_invert->set_active(false);
    CB_MS_smooth->set_active(true);
    CB_MS_stack->set_active(true);
    CB_MS_rb->set_active(false);
    CB_speckles->set_active(true);
    CB_smooth->set_active(true);
    CB_optimize->set_active(true);
    CB_speckles1->set_active(true);
    CB_smooth1->set_active(true);
    CB_optimize1->set_active(true);
    CB_PA_optimize->set_active(false);
    CB_SIOX->set_active(false);
    CB_SIOX1->set_active(false);
}

void TraceDialogImpl::onAbortClicked()
{
    if (!trace_future) {
        // Not tracing; nothing to cancel.
        return;
    }

    stack->set_visible_child(*boxchild1);
    if (auto desktop = getDesktop()) desktop->clearWaitingCursor();
    trace_future.cancel();
}

void TraceDialogImpl::onTraceClicked()
{
    if (trace_future) {
        // Still tracing; wait for either finished or cancelled.
        return;
    }

    // Attempt to fire off the tracer.
    auto data = getTraceData();
    trace_future = Trace::trace(std::move(data.engine), data.sioxEnabled,
        // On progress:
        [this] (double progress) {
            progressbar->set_fraction(progress);
        },
        // On completion without cancelling:
        [this] {
            progressbar->set_fraction(1.0);
            stack->set_visible_child(*boxchild1);
            if (auto desktop = getDesktop()) desktop->clearWaitingCursor();
            trace_future.cancel();
        }
    );

    if (trace_future) {
        // Put the UI into the tracing state.
        if (auto desktop = getDesktop()) desktop->setWaitingCursor();
        stack->set_visible_child(*boxchild2);
        progressbar->set_fraction(0.0);
    }
}

TraceDialogImpl::TraceDialogImpl()
{
    Glib::ustring const req_widgets[] = { "MS_scans",    "PA_curves", "PA_islands",  "PA_sparse1", "PA_sparse2",
                                        "SS_AT_FI_T", "SS_AT_ET_T",     "SS_BC_T",   "SS_CQ_T",     "SS_ED_T",
                                        "optimize",    "smooth",    "speckles",    "CB_invert",  "CB_MS_smooth",
                                        "CB_MS_stack", "CB_MS_rb",  "CB_speckles", "CB_smooth",  "CB_optimize",
                                        "CB_speckles1", "CB_smooth1",  "CB_optimize1", "CB_SIOX1",
                                        "CB_PA_optimize", /*"CB_live",*/ "CB_SIOX", "CBT_SS",    "CBT_MS",
                                        "B_RESET",     "B_STOP",    "B_OK",         "mainBox",   "choice_tab",
                                        /*"choice_scan",*/ "previewArea", "_live_preview",
                                        "stack", "progressbar", "boxchild1", "boxchild2" };
    auto gladefile = get_filename_string(Inkscape::IO::Resource::UIS, "dialog-trace.glade");
    try {
        builder = Gtk::Builder::create_from_file(gladefile);
    } catch (Glib::Error const &) {
        g_warning("Trace dialog: Glade file loading failed");
        return;
    }

    for (auto &w : req_widgets) {
        auto test = builder->get_object(w);
        if (!test) {
            g_warning("Trace dialog: Required widget %s does not exist", w.c_str());
            return;
        }
    }

#define GET_O(name) name = Glib::RefPtr<Gtk::Adjustment>::cast_dynamic(builder->get_object(#name));
    GET_O(MS_scans)
    GET_O(PA_curves)
    GET_O(PA_islands)
    GET_O(PA_sparse1)
    GET_O(PA_sparse2)
    GET_O(SS_AT_FI_T)
    GET_O(SS_AT_ET_T)
    GET_O(SS_BC_T)
    GET_O(SS_CQ_T)
    GET_O(SS_ED_T)
    GET_O(optimize)
    GET_O(smooth)
    GET_O(speckles)
#undef GET_O
#define GET_W(name) builder->get_widget(#name, name);
    GET_W(CB_invert)
    GET_W(CB_MS_smooth)
    GET_W(CB_MS_stack)
    GET_W(CB_MS_rb)
    GET_W(CB_speckles)
    GET_W(CB_smooth)
    GET_W(CB_optimize)
    GET_W(CB_speckles1)
    GET_W(CB_smooth1)
    GET_W(CB_optimize1)
    GET_W(CB_PA_optimize)
    GET_W(CB_SIOX)
    GET_W(CB_SIOX1)
    GET_W(RB_PA_voronoi)
    GET_W(CBT_SS)
    GET_W(CBT_MS)
    GET_W(B_RESET)
    GET_W(B_STOP)
    GET_W(B_OK)
    GET_W(B_Update)
    GET_W(mainBox)
    GET_W(choice_tab)
    GET_W(previewArea)
    GET_W(orient_box)
    GET_W(_preview_frame)
    GET_W(_param_grid)
    GET_W(_live_preview)
    GET_W(stack)
    GET_W(progressbar)
    GET_W(boxchild1)
    GET_W(boxchild2)
#undef GET_W
    add(*mainBox);

    Inkscape::Preferences* prefs = Inkscape::Preferences::get();

    _live_preview->set_active(prefs->getBool(getPrefsPath() + "liveUpdate", true));

    B_Update->signal_clicked().connect([=] { updatePreview(true); });
    B_OK->signal_clicked().connect(sigc::mem_fun(*this, &TraceDialogImpl::onTraceClicked));
    B_STOP->signal_clicked().connect(sigc::mem_fun(*this, &TraceDialogImpl::onAbortClicked));
    B_RESET->signal_clicked().connect(sigc::mem_fun(*this, &TraceDialogImpl::setDefaults));
    previewArea->signal_draw().connect(sigc::mem_fun(*this, &TraceDialogImpl::paintPreview));

    // attempt at making UI responsive: relocate preview to the right or bottom of dialog depending on dialog size
    signal_size_allocate().connect([=] (Gtk::Allocation const &alloc) {
        // skip bogus sizes
        if (alloc.get_width() < 10 || alloc.get_height() < 10) return;
        // ratio: is dialog wide or is it tall?
        double const ratio = alloc.get_width() / static_cast<double>(alloc.get_height());
        // g_warning("size alloc: %d x %d - %f", alloc.get_width(), alloc.get_height(), ratio);
        double constexpr hysteresis = 0.01;
        if (ratio < 1 - hysteresis) {
            // narrow/tall
            choice_tab->set_valign(Gtk::ALIGN_START);
            orient_box->set_orientation(Gtk::ORIENTATION_VERTICAL);
        }
        else if (ratio > 1 + hysteresis) {
            // wide/short
            orient_box->set_orientation(Gtk::ORIENTATION_HORIZONTAL);
            choice_tab->set_valign(Gtk::ALIGN_FILL);
        }
    });

    CBT_SS->signal_changed().connect([=] { adjustParamsVisible(); });
    adjustParamsVisible();

    // watch for changes, but only in params that can impact preview bitmap
    for (auto adj : {SS_BC_T, SS_ED_T, SS_CQ_T, SS_AT_FI_T, SS_AT_ET_T, /* optimize, smooth, speckles,*/ MS_scans, PA_curves, PA_islands, PA_sparse1, PA_sparse2 }) {
        adj->signal_value_changed().connect([=] { updatePreview(); });
    }
    for (auto checkbtn : {CB_invert, CB_MS_rb, /* CB_MS_smooth, CB_MS_stack, CB_optimize1, CB_optimize, */ CB_PA_optimize, CB_SIOX1, CB_SIOX, /* CB_smooth1, CB_smooth, CB_speckles1, CB_speckles, */ _live_preview}) {
        checkbtn->signal_toggled().connect([=] { updatePreview(); });
    }
    for (auto combo : {CBT_SS, CBT_MS}) {
        combo->signal_changed().connect([=] { updatePreview(); });
    }
    choice_tab->signal_switch_page().connect([=] (Gtk::Widget*, unsigned) { updatePreview(); });

    signal_set_focus_child().connect([=] (Gtk::Widget *w) {
        if (w) updatePreview();
    });
}

TraceDialogImpl::~TraceDialogImpl()
{
    Inkscape::Preferences* prefs = Inkscape::Preferences::get();
    prefs->setBool(getPrefsPath() + "liveUpdate", _live_preview->get_active());
    preview_timeout_conn.disconnect();
}

bool TraceDialogImpl::previewsEnabled() const
{
    return _live_preview->get_active() && is_widget_effectively_visible(this);
}

void TraceDialogImpl::schedulePreviewUpdate(int msecs, bool force)
{
    if (!previewsEnabled() && !force) {
        return;
    }

    // Restart timeout.
    preview_timeout_conn.disconnect();
    preview_timeout_conn = Glib::signal_timeout().connect([this] {
        updatePreview(true);
        return false;
    }, msecs);
}

void TraceDialogImpl::updatePreview(bool force)
{
    if (!previewsEnabled() && !force) {
        return;
    }

    preview_timeout_conn.disconnect();

    if (preview_future) {
        // Preview generation already running - flag for recomputation when finished.
        preview_pending_recompute = true;
        return;
    }

    preview_pending_recompute = false;

    auto data = getTraceData();
    preview_future = Trace::preview(std::move(data.engine), data.sioxEnabled,
        // On completion:
        [this] (Glib::RefPtr<Gdk::Pixbuf> result) {
            preview_image = std::move(result);
            previewArea->queue_draw();
            preview_future.cancel();

            // Recompute if invalidated during computation.
            if (preview_pending_recompute) {
                updatePreview();
            }
        }
    );

    if (!preview_future) {
        // On instant failure:
        preview_image.reset();
        previewArea->queue_draw();
    }
}

void TraceDialogImpl::adjustParamsVisible()
{
    int constexpr start_row = 2;
    int option = CBT_SS->get_active_row_number();
    if (option >= 3) option = 3;
    int show1 = start_row + option;
    int show2 = show1;
    if (option == 3) ++show2;

    for (int row = start_row; row < start_row + 5; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (auto widget = _param_grid->get_child_at(col, row)) {
                if (row == show1 || row == show2) {
                    widget->set_visible(true);
                } else {
                    widget->set_visible(false);
                }
            }
        }
    }
}

std::unique_ptr<TraceDialog> TraceDialog::create()
{
    return std::make_unique<TraceDialogImpl>();
}

} // namespace Dialog
} // namespace UI
} // namespace Inkscape
