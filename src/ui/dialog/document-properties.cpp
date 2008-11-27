/** @file
 * @brief Document properties dialog, Gtkmm-style
 */
/* Authors:
 *   bulia byak <buliabyak@users.sf.net>
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Jon Phillips <jon@rejon.org>
 *   Ralf Stephan <ralf@ark.in-berlin.de> (Gtkmm)
 *   Diederik van Lierop <mail@diedenrezi.nl>
 *
 * Copyright (C) 2006-2008 Johan Engelen  <johan@shouraizou.nl>
 * Copyright (C) 2000 - 2008 Authors
 *
 * Released under GNU GPL.  Read the file 'COPYING' for more information
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "display/canvas-grid.h"
#include "document-properties.h"
#include "document.h"
#include "desktop-handles.h"
#include "desktop.h"
#include <gtkmm.h>
#include "helper/units.h"
#include "inkscape.h"
#include "io/sys.h"
#include "preferences.h"
#include "sp-namedview.h"
#include "sp-object-repr.h"
#include "sp-root.h"
#include "ui/widget/color-picker.h"
#include "ui/widget/scalar-unit.h"
#include "verbs.h"
#include "widgets/icon.h"
#include "xml/node-event-vector.h"
#include "xml/repr.h"

#if ENABLE_LCMS
#include <lcms.h>
//#include "color-profile-fns.h"
#include "color-profile.h"
#endif // ENABLE_LCMS

using std::pair;

namespace Inkscape {
namespace UI {
namespace Dialog {

#define SPACE_SIZE_X 15
#define SPACE_SIZE_Y 10

#define INKSCAPE_ICON_GRID_XY     "grid_xy"
#define INKSCAPE_ICON_GRID_AXONOM "grid_axonom"


//===================================================

//---------------------------------------------------

static void on_child_added(Inkscape::XML::Node *repr, Inkscape::XML::Node *child, Inkscape::XML::Node *ref, void * data);
static void on_child_removed(Inkscape::XML::Node *repr, Inkscape::XML::Node *child, Inkscape::XML::Node *ref, void * data);
static void on_repr_attr_changed (Inkscape::XML::Node *, gchar const *, gchar const *, gchar const *, bool, gpointer);

static Inkscape::XML::NodeEventVector const _repr_events = {
    on_child_added, /* child_added */
    on_child_removed, /* child_removed */
    on_repr_attr_changed,
    NULL, /* content_changed */
    NULL  /* order_changed */
};


DocumentProperties &
DocumentProperties::getInstance()
{
    DocumentProperties &instance = *new DocumentProperties();
    instance.init();

    return instance;
}

DocumentProperties::DocumentProperties()
    : UI::Widget::Panel ("", "/dialogs/documentoptions", SP_VERB_DIALOG_NAMEDVIEW),
      _page_page(1, 1, true, true), _page_guides(1, 1),
      _page_snap(1, 1), _page_snap_dtls(1, 1), _page_cms(1, 1),
    //---------------------------------------------------------------
      _rcb_canb(_("Show page _border"), _("If set, rectangular page border is shown"), "showborder", _wr, false),
      _rcb_bord(_("Border on _top of drawing"), _("If set, border is always on top of the drawing"), "borderlayer", _wr, false),
      _rcb_shad(_("_Show border shadow"), _("If set, page border shows a shadow on its right and lower side"), "inkscape:showpageshadow", _wr, false),
      _rcp_bg(_("Back_ground:"), _("Background color"), _("Color and transparency of the page background (also used for bitmap export)"), "pagecolor", "inkscape:pageopacity", _wr),
      _rcp_bord(_("Border _color:"), _("Page border color"), _("Color of the page border"), "bordercolor", "borderopacity", _wr),
      _rum_deflt(_("Default _units:"), "inkscape:document-units", _wr),
      _page_sizer(_wr),
    //---------------------------------------------------------------
      //General snap options
      _rcb_sgui(_("Show _guides"), _("Show or hide guides"), "showguides", _wr),
      _rcbsng(_("_Snap guides while dragging"), _("While dragging a guide, snap to object nodes or bounding box corners ('Snap to nodes' or 'snap to bounding box corners' must be enabled in the 'Snap' tab; only a small part of the guide near the cursor will snap)"),
                  "inkscape:snap-guide", _wr),
      _rcp_gui(_("Guide co_lor:"), _("Guideline color"), _("Color of guidelines"), "guidecolor", "guideopacity", _wr),
      _rcp_hgui(_("_Highlight color:"), _("Highlighted guideline color"), _("Color of a guideline when it is under mouse"), "guidehicolor", "guidehiopacity", _wr),
    //---------------------------------------------------------------
      _rcbs(_("_Enable snapping"), _("Toggle snapping on or off"), "inkscape:snap-global", _wr),
      _rcbsnbb(_("_Bounding box corners"), _("Only available in the selector tool: snap bounding box corners to guides, to grids, and to other bounding boxes (but not to nodes or paths)"),
                  "inkscape:snap-bbox", _wr),
      _rcbsnn(_("_Nodes"), _("Snap nodes (e.g. path nodes, special points in shapes, gradient handles, text base points, transformation origins, etc.) to guides, to grids, to paths and to other nodes"),
                "inkscape:snap-nodes", _wr),
      //Options for snapping to objects
      _rcbsnop(_("Snap to path_s"), _("Snap nodes to object paths"), "inkscape:object-paths", _wr),
      _rcbsnon(_("Snap to n_odes"), _("Snap nodes and guides to object nodes"), "inkscape:object-nodes", _wr),
      _rcbsnbbp(_("Snap to bounding bo_x edges"), _("Snap bounding box corners and guides to bounding box edges"), "inkscape:bbox-paths", _wr),
      _rcbsnbbn(_("Snap to bounding box co_rners"), _("Snap bounding box corners to other bounding box corners"), "inkscape:bbox-nodes", _wr),
      _rcbsnpb(_("Snap to page border"), _("Snap bounding box corners and nodes to the page border"), "inkscape:snap-page", _wr),
    //---------------------------------------------------------------
       //Applies to both nodes and guides, but not to bboxes, that's why its located here
      _rcbic( _("Rotation _center"), _("Consider the rotation center of an object when snapping"), "inkscape:snap-center", _wr),
      _rcbsm( _("_Smooth nodes"), _("Snap to smooth nodes too, instead of only snapping to cusp nodes"), "inkscape:snap-smooth-nodes", _wr),
      _rcbsigg(_("_Grid with guides"), _("Snap to grid-guide intersections"), "inkscape:snap-intersection-grid-guide", _wr),
      _rcbsils(_("_Paths"), _("Snap to intersections of paths ('snap to paths' must be enabled, see the previous tab)"),
                "inkscape:snap-intersection-paths", _wr),
    //---------------------------------------------------------------
      _grids_label_crea("", Gtk::ALIGN_LEFT),
      //TRANSLATORS: In Grid|_New translate only the word _New. It ref to grid
      _grids_button_new(Q_("Grid|_New"), _("Create new grid.")),
      _grids_button_remove(_("_Remove"), _("Remove selected grid.")),
      _grids_label_def("", Gtk::ALIGN_LEFT)
    //---------------------------------------------------------------
{
    _tt.enable();
    _getContents()->set_spacing (4);
    _getContents()->pack_start(_notebook, true, true);

    _notebook.append_page(_page_page,      _("Page"));
    _notebook.append_page(_page_guides,    _("Guides"));
    _notebook.append_page(_grids_vbox,     _("Grids"));
    _notebook.append_page(_page_snap,      _("Snap"));
    _notebook.append_page(_page_snap_dtls, _("Snap points"));
    _notebook.append_page(_page_cms, _("Color Management"));

    build_page();
    build_guides();
    build_gridspage();
    build_snap();
    build_snap_dtls();
#if ENABLE_LCMS
    build_cms();
#endif // ENABLE_LCMS

    _grids_button_new.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::onNewGrid));
    _grids_button_remove.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::onRemoveGrid));

    signalDocumentReplaced().connect(sigc::mem_fun(*this, &DocumentProperties::_handleDocumentReplaced));
    signalActivateDesktop().connect(sigc::mem_fun(*this, &DocumentProperties::_handleActivateDesktop));
    signalDeactiveDesktop().connect(sigc::mem_fun(*this, &DocumentProperties::_handleDeactivateDesktop));
}

void
DocumentProperties::init()
{
    update();

    Inkscape::XML::Node *repr = SP_OBJECT_REPR(sp_desktop_namedview(getDesktop()));
    repr->addListener (&_repr_events, this);
    Inkscape::XML::Node *root = SP_OBJECT_REPR(sp_desktop_document(getDesktop())->root);
    root->addListener (&_repr_events, this);

    show_all_children();
    _grids_button_remove.hide();
}

DocumentProperties::~DocumentProperties()
{
    Inkscape::XML::Node *repr = SP_OBJECT_REPR(sp_desktop_namedview(getDesktop()));
    repr->removeListenerByData (this);
    Inkscape::XML::Node *root = SP_OBJECT_REPR(sp_desktop_document(getDesktop())->root);
    root->removeListenerByData (this);
}

//========================================================================

/**
 * Helper function that attaches widgets in a 3xn table. The widgets come in an
 * array that has two entries per table row. The two entries code for four
 * possible cases: (0,0) means insert space in first column; (0, non-0) means
 * widget in columns 2-3; (non-0, 0) means label in columns 1-3; and
 * (non-0, non-0) means two widgets in columns 2 and 3.
**/
inline void
attach_all(Gtk::Table &table, Gtk::Widget *const arr[], unsigned const n, int start = 0)
{
    for (unsigned i = 0, r = start; i < n; i += 2)
    {
        if (arr[i] && arr[i+1])
        {
            table.attach(*arr[i],   1, 2, r, r+1,
                      Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0,0,0);
            table.attach(*arr[i+1], 2, 3, r, r+1,
                      Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0,0,0);
        }
        else
        {
            if (arr[i+1]) {
                Gtk::AttachOptions yoptions = (Gtk::AttachOptions)0;
                if (dynamic_cast<Inkscape::UI::Widget::PageSizer*>(arr[i+1])) {
                    // only the PageSizer in Document Properties|Page should be stretched vertically
                    yoptions = Gtk::FILL|Gtk::EXPAND;
                }
                table.attach(*arr[i+1], 1, 3, r, r+1,
                      Gtk::FILL|Gtk::EXPAND, yoptions, 0,0);
            }
            else if (arr[i])
            {
                Gtk::Label& label = reinterpret_cast<Gtk::Label&>(*arr[i]);
                label.set_alignment (0.0);
                table.attach (label, 0, 3, r, r+1,
                      Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0,0,0);
            }
            else
            {
                Gtk::HBox *space = manage (new Gtk::HBox);
                space->set_size_request (SPACE_SIZE_X, SPACE_SIZE_Y);
                table.attach (*space, 0, 1, r, r+1,
                      (Gtk::AttachOptions)0, (Gtk::AttachOptions)0,0,0);
            }
        }
        ++r;
    }
}

void
DocumentProperties::build_page()
{
    _page_page.show();

    Gtk::Label* label_gen = manage (new Gtk::Label);
    label_gen->set_markup (_("<b>General</b>"));
    Gtk::Label* label_bor = manage (new Gtk::Label);
    label_bor->set_markup (_("<b>Border</b>"));
    Gtk::Label *label_for = manage (new Gtk::Label);
    label_for->set_markup (_("<b>Format</b>"));
    _page_sizer.init();

    Gtk::Widget *const widget_array[] =
    {
        label_gen,         0,
        0,                 &_rum_deflt,
        _rcp_bg._label,    &_rcp_bg,
        0,                 0,
        label_for,         0,
        0,                 &_page_sizer,
        0,                 0,
        label_bor,         0,
        0,                 &_rcb_canb,
        0,                 &_rcb_bord,
        0,                 &_rcb_shad,
        _rcp_bord._label,  &_rcp_bord,
    };

    attach_all(_page_page.table(), widget_array, G_N_ELEMENTS(widget_array));
}

void
DocumentProperties::build_guides()
{
    _page_guides.show();

    Gtk::Label *label_gui = manage (new Gtk::Label);
    label_gui->set_markup (_("<b>Guides</b>"));

    Gtk::Widget *const widget_array[] =
    {
        label_gui,        0,
        0,                &_rcb_sgui,
        _rcp_gui._label,  &_rcp_gui,
        _rcp_hgui._label, &_rcp_hgui,
        0,                &_rcbsng,
    };

    attach_all(_page_guides.table(), widget_array, G_N_ELEMENTS(widget_array));
}

void
DocumentProperties::build_snap()
{
    _page_snap.show();

    _rsu_sno.init (_("Snap _distance"), _("Snap only when _closer than:"), _("Always snap"),
                  _("Snapping distance, in screen pixels, for snapping to objects"), _("Always snap to objects, regardless of their distance"),
                  _("If set, objects only snap to another object when it's within the range specified below"),
                  "objecttolerance", _wr);

    //Options for snapping to grids
    _rsu_sn.init (_("Snap d_istance"), _("Snap only when c_loser than:"), _("Always snap"),
                  _("Snapping distance, in screen pixels, for snapping to grid"), _("Always snap to grids, regardless of the distance"),
                  _("If set, objects only snap to a grid line when it's within the range specified below"),
                  "gridtolerance", _wr);

    //Options for snapping to guides
    _rsu_gusn.init (_("Snap dist_ance"), _("Snap only when close_r than:"), _("Always snap"),
                _("Snapping distance, in screen pixels, for snapping to guides"), _("Always snap to guides, regardless of the distance"),
                _("If set, objects only snap to a guide when it's within the range specified below"),
                "guidetolerance", _wr);

    //Other options to locate here: e.g. visual snapping indicators on/off

    std::list<Gtk::Widget*> slaves;
    slaves.push_back(&_rcbsnop);
    slaves.push_back(&_rcbsnon);
    _rcbsnn.setSlaveWidgets(slaves);

    slaves.clear();
    slaves.push_back(&_rcbsnbbp);
    slaves.push_back(&_rcbsnbbn);
    _rcbsnbb.setSlaveWidgets(slaves);

    slaves.clear();
    slaves.push_back(&_rcbsnn);
    slaves.push_back(&_rcbsnbb);
    _rcbs.setSlaveWidgets(slaves);

    Gtk::Label *label_g = manage (new Gtk::Label);
    label_g->set_markup (_("<b>Snapping</b>"));
    Gtk::Label *label_w = manage (new Gtk::Label);
    label_w->set_markup (_("<b>What snaps</b>"));
    Gtk::Label *label_o = manage (new Gtk::Label);
    label_o->set_markup (_("<b>Snap to objects</b>"));
    Gtk::Label *label_gr = manage (new Gtk::Label);
    label_gr->set_markup (_("<b>Snap to grids</b>"));
    Gtk::Label *label_gu = manage (new Gtk::Label);
    label_gu->set_markup (_("<b>Snap to guides</b>"));

    Gtk::Widget *const array[] =
    {
        label_g,            0,
        0,                  &_rcbs,
        0,                  0,
        label_w,            0,
        0,                  &_rcbsnn,
        0,                  &_rcbsnbb,
        0,                  0,
        label_o,            0,
        0,                  &_rcbsnop,
        0,                  &_rcbsnon,
        0,                  &_rcbsnbbp,
        0,                  &_rcbsnbbn,
        0,                  &_rcbsnpb,
        0,                  _rsu_sno._vbox,
        0,                  0,
        label_gr,           0,
        0,                  _rsu_sn._vbox,
        0,                  0,
        label_gu,           0,
        0,                  _rsu_gusn._vbox
    };

    attach_all(_page_snap.table(), array, G_N_ELEMENTS(array));
 }

void
DocumentProperties::build_snap_dtls()
{
    _page_snap_dtls.show();

    //Other options to locate here: e.g. visual snapping indicators on/off

    Gtk::Label *label_i= manage (new Gtk::Label);
    label_i->set_markup (_("<b>Snapping to intersections of</b>"));
    Gtk::Label *label_m = manage (new Gtk::Label);
    label_m->set_markup (_("<b>Special points to consider</b>"));

    Gtk::Widget *const array[] =
    {
        label_i,            0,
        0,                  &_rcbsigg,
        0,                  &_rcbsils,
        0,                  0,
        label_m,            0,
        0,                  &_rcbic,
        0,                  &_rcbsm
    };

    attach_all(_page_snap_dtls.table(), array, G_N_ELEMENTS(array));
}

// Very simple observer that just emits a signal if anything happens to a node
DocumentProperties::SignalObserver::SignalObserver()
    : _oldsel(0)
{}

// Add this observer to the SPObject and remove it from any previous object
void
DocumentProperties::SignalObserver::set(SPObject* o)
{
    if(_oldsel && _oldsel->repr)
        _oldsel->repr->removeObserver(*this);
    if(o && o->repr)
        o->repr->addObserver(*this);
    _oldsel = o;
}

void DocumentProperties::SignalObserver::notifyChildAdded(XML::Node&, XML::Node&, XML::Node*)
{ signal_changed()(); }

void DocumentProperties::SignalObserver::notifyChildRemoved(XML::Node&, XML::Node&, XML::Node*)
{ signal_changed()(); }

void DocumentProperties::SignalObserver::notifyChildOrderChanged(XML::Node&, XML::Node&, XML::Node*, XML::Node*)
{ signal_changed()(); }

void DocumentProperties::SignalObserver::notifyContentChanged(XML::Node&, Util::ptr_shared<char>, Util::ptr_shared<char>)
{}

void DocumentProperties::SignalObserver::notifyAttributeChanged(XML::Node&, GQuark, Util::ptr_shared<char>, Util::ptr_shared<char>)
{ signal_changed()(); }

sigc::signal<void>& DocumentProperties::SignalObserver::signal_changed()
{
    return _signal_changed;
}

#if ENABLE_LCMS
static void
lcms_profile_get_name (cmsHPROFILE   profile, const gchar **name)
{
  if (profile)
    {
      *name = cmsTakeProductDesc (profile);

      if (! *name)
        *name = cmsTakeProductName (profile);

      if (*name && ! g_utf8_validate (*name, -1, NULL))
        *name = _("(invalid UTF-8 string)");
    }
  else
    {
      *name = _("None");
    }
}

void
DocumentProperties::populate_available_profiles(){

    std::list<gchar *> sources;
    sources.push_back( profile_path("color/icc") );
    //sources.push_back( g_strdup(INKSCAPE_COLORPROFILESDIR) );

    int index = 1;

    // Use this loop to iterate through a list of possible document locations.
    while (!sources.empty()) {
        gchar *dirname = sources.front();

        if ( Inkscape::IO::file_test( dirname, G_FILE_TEST_EXISTS )
            && Inkscape::IO::file_test( dirname, G_FILE_TEST_IS_DIR )) {
            GError *err = 0;
            GDir *directory = g_dir_open(dirname, 0, &err);
            if (!directory) {
                gchar *safeDir = Inkscape::IO::sanitizeString(dirname);
                g_warning(_("Color profiles directory (%s) is unavailable."), safeDir);
                g_free(safeDir);
            } else {
                gchar *filename = 0;
                while ((filename = (gchar *)g_dir_read_name(directory)) != NULL) {
                    gchar* lower = g_ascii_strdown( filename, -1 );
                    gchar* full = g_build_filename(dirname, filename, NULL);
                    if ( !Inkscape::IO::file_test( full, G_FILE_TEST_IS_DIR ) ) {
                        cmsHPROFILE hProfile = cmsOpenProfileFromFile(full, "r");
                        if (hProfile != NULL){
                            const gchar* name;
                            lcms_profile_get_name(hProfile, &name);
                            Gtk::MenuItem* mi = new Gtk::MenuItem();
                            mi->set_data("filepath", g_strdup(full));
                            mi->set_data("name", g_strdup(name));
                            Gtk::HBox *hbox = new Gtk::HBox();
                            hbox->show();
                            Gtk::Label* lbl = manage(new Gtk::Label(name));
                            lbl->show();
                            hbox->pack_start(*lbl, true, true, 0);
                            mi->add(*hbox);
                            mi->show_all();
                            _menu.append(*mi);
        //                    g_free((void*)name);
                        }
                        cmsCloseProfile(hProfile);
                        index++;
                    }
                    g_free(full);
                    g_free(lower);
                }
                g_dir_close(directory);
            }
        }

        // toss the dirname
        g_free(dirname);
        sources.pop_front();
    }
    _menu.show_all();
}

void
DocumentProperties::onEmbedProfile()
{
//store this profile in the SVG document (create <color-profile> element in the XML)
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (!desktop){
        g_warning("No active desktop");
        return;
    }
    Inkscape::XML::Document *xml_doc = sp_document_repr_doc(desktop->doc());
    Inkscape::XML::Node *cprofRepr = xml_doc->createElement("svg:color-profile");
    cprofRepr->setAttribute("name", (gchar*) _menu.get_active()->get_data("name"));
    cprofRepr->setAttribute("xlink:href", (gchar*) _menu.get_active()->get_data("filepath"));

    /* Checks whether there is a defs element. Creates it when needed */
    Inkscape::XML::Node *defsRepr = sp_repr_lookup_name(xml_doc, "svg:defs");
    if (!defsRepr){
        defsRepr = xml_doc->createElement("svg:defs");
        xml_doc->root()->addChild(defsRepr, NULL);
    }

    g_assert(SP_ROOT(desktop->doc()->root)->defs);
    defsRepr->addChild(cprofRepr, NULL);

    Inkscape::GC::release(defsRepr);

    // inform the document, so we can undo
    sp_document_done(desktop->doc(), SP_VERB_EDIT_EMBED_COLOR_PROFILE, _("Embed Color Profile"));

    populate_embedded_profiles_box();
}

void
DocumentProperties::populate_embedded_profiles_box()
{
    _EmbeddedProfilesListStore->clear();
    const GSList *current = sp_document_get_resource_list( SP_ACTIVE_DOCUMENT, "iccprofile" );
    if (current) _emb_profiles_observer.set(SP_OBJECT(current->data)->parent);
    while ( current ) {
        SPObject* obj = SP_OBJECT(current->data);
        Inkscape::ColorProfile* prof = reinterpret_cast<Inkscape::ColorProfile*>(obj);
        Gtk::TreeModel::Row row = *(_EmbeddedProfilesListStore->append());
        row[_EmbeddedProfilesListColumns.nameColumn] = prof->name;
//        row[_EmbeddedProfilesListColumns.previewColumn] = "Color Preview";
        current = g_slist_next(current);
    }
}

void DocumentProperties::embedded_profiles_list_button_release(GdkEventButton* event)
{
    if((event->type == GDK_BUTTON_RELEASE) && (event->button == 3)) {
        _EmbProfContextMenu.popup(event->button, event->time);
    }
}

void DocumentProperties::create_popup_menu(Gtk::Widget& parent, sigc::slot<void> rem)
{
    Gtk::MenuItem* mi = Gtk::manage(new Gtk::ImageMenuItem(Gtk::Stock::REMOVE));
    _EmbProfContextMenu.append(*mi);
    mi->signal_activate().connect(rem);
    mi->show();
    _EmbProfContextMenu.accelerate(parent);
}

void DocumentProperties::remove_profile(){
    Glib::ustring name;
    if(_EmbeddedProfilesList.get_selection()) {
        Gtk::TreeModel::iterator i = _EmbeddedProfilesList.get_selection()->get_selected();

        if(i){
            name = (*i)[_EmbeddedProfilesListColumns.nameColumn];
        } else {
            return;
        }
    }

    const GSList *current = sp_document_get_resource_list( SP_ACTIVE_DOCUMENT, "iccprofile" );
    while ( current ) {
        SPObject* obj = SP_OBJECT(current->data);
        Inkscape::ColorProfile* prof = reinterpret_cast<Inkscape::ColorProfile*>(obj);
        if (!name.compare(prof->name)){
            sp_repr_unparent(obj->repr);
            sp_document_done(SP_ACTIVE_DOCUMENT, SP_VERB_EDIT_REMOVE_COLOR_PROFILE, _("Remove embedded color profile"));
        }
        current = g_slist_next(current);
    }

    populate_embedded_profiles_box();
}

void
DocumentProperties::build_cms()
{
    _page_cms.show();

    Gtk::Label *label_embed= manage (new Gtk::Label);
    label_embed->set_markup (_("<b>Embedded Color Profiles:</b>"));
    Gtk::Label *label_avail = manage (new Gtk::Label);
    label_avail->set_markup (_("<b>Available Color Profiles:</b>"));

    _embed_btn.set_label(_("Embed Profile"));

    Gtk::Widget *const array[] =
    {
        label_embed,          0,
        &_EmbeddedProfilesListScroller,      0,
        label_avail,        0,
        &_combo_avail,        &_embed_btn,
    };

    attach_all(_page_cms.table(), array, G_N_ELEMENTS(array));

    populate_available_profiles();

    _combo_avail.set_menu(_menu);
    _combo_avail.set_history(0);
    _combo_avail.show_all();

    //# Set up the Embedded Profiles combo box
    _EmbeddedProfilesListStore = Gtk::ListStore::create(_EmbeddedProfilesListColumns);
    _EmbeddedProfilesList.set_model(_EmbeddedProfilesListStore);
    _EmbeddedProfilesList.append_column(_("Profile Name"), _EmbeddedProfilesListColumns.nameColumn);
//    _EmbeddedProfilesList.append_column(_("Color Preview"), _EmbeddedProfilesListColumns.previewColumn);
    _EmbeddedProfilesList.set_headers_visible(false);
    _EmbeddedProfilesList.set_fixed_height_mode(true);

    populate_embedded_profiles_box();

    _EmbeddedProfilesListScroller.add(_EmbeddedProfilesList);
    _EmbeddedProfilesListScroller.set_shadow_type(Gtk::SHADOW_IN);
    _EmbeddedProfilesListScroller.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_ALWAYS);
    _EmbeddedProfilesListScroller.set_size_request(-1, 90);

    _embed_btn.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::onEmbedProfile));

    _EmbeddedProfilesList.signal_button_release_event().connect_notify(sigc::mem_fun(*this, &DocumentProperties::embedded_profiles_list_button_release));
    create_popup_menu(_EmbeddedProfilesList, sigc::mem_fun(*this, &DocumentProperties::remove_profile));

    const GSList *current = sp_document_get_resource_list( SP_ACTIVE_DOCUMENT, "defs" );
    if (current) _emb_profiles_observer.set(SP_OBJECT(current->data)->parent);
    _emb_profiles_observer.signal_changed().connect(sigc::mem_fun(*this, &DocumentProperties::populate_embedded_profiles_box));
}
#endif // ENABLE_LCMS

/**
* Called for _updating_ the dialog (e.g. when a new grid was manually added in XML)
*/
void
DocumentProperties::update_gridspage()
{
    SPDesktop *dt = getDesktop();
    SPNamedView *nv = sp_desktop_namedview(dt);

    //remove all tabs
    while (_grids_notebook.get_n_pages() != 0) {
        _grids_notebook.remove_page(-1); // this also deletes the page.
    }

    //add tabs
    bool grids_present = false;
    for (GSList const * l = nv->grids; l != NULL; l = l->next) {
        Inkscape::CanvasGrid * grid = (Inkscape::CanvasGrid*) l->data;
        if (!grid->repr->attribute("id")) continue; // update_gridspage is called again when "id" is added
        Glib::ustring name(grid->repr->attribute("id"));
        const char *icon = NULL;
        switch (grid->getGridType()) {
            case GRID_RECTANGULAR:
                icon = INKSCAPE_ICON_GRID_XY;
                break;
            case GRID_AXONOMETRIC:
                icon = INKSCAPE_ICON_GRID_AXONOM;
                break;
            default:
                break;
        }
        _grids_notebook.append_page(*grid->newWidget(), _createPageTabLabel(name, icon));
        grids_present = true;
    }
    _grids_notebook.show_all();

    if (grids_present)
        _grids_button_remove.set_sensitive(true);
    else
        _grids_button_remove.set_sensitive(false);
}

/**
 * Build grid page of dialog.
 */
void
DocumentProperties::build_gridspage()
{
    /// \todo FIXME: gray out snapping when grid is off.
    /// Dissenting view: you want snapping without grid.

    SPDesktop *dt = getDesktop();
    SPNamedView *nv = sp_desktop_namedview(dt);
    (void)nv;

    _grids_label_crea.set_markup(_("<b>Creation</b>"));
    _grids_label_def.set_markup(_("<b>Defined grids</b>"));
    _grids_hbox_crea.pack_start(_grids_combo_gridtype, true, true);
    _grids_hbox_crea.pack_start(_grids_button_new, true, true);

    for (gint t = 0; t <= GRID_MAXTYPENR; t++) {
        _grids_combo_gridtype.append_text( CanvasGrid::getName( (GridType) t ) );
    }
    _grids_combo_gridtype.set_active_text( CanvasGrid::getName(GRID_RECTANGULAR) );

    _grids_space.set_size_request (SPACE_SIZE_X, SPACE_SIZE_Y);

    _grids_vbox.set_spacing(4);
    _grids_vbox.pack_start(_grids_label_crea, false, false);
    _grids_vbox.pack_start(_grids_hbox_crea, false, false);
    _grids_vbox.pack_start(_grids_space, false, false);
    _grids_vbox.pack_start(_grids_label_def, false, false);
    _grids_vbox.pack_start(_grids_notebook, false, false);
    _grids_vbox.pack_start(_grids_button_remove, false, false);

    update_gridspage();
}



/**
 * Update dialog widgets from desktop. Also call updateWidget routines of the grids.
 */
void
DocumentProperties::update()
{
    if (_wr.isUpdating()) return;

    SPDesktop *dt = getDesktop();
    SPNamedView *nv = sp_desktop_namedview(dt);

    _wr.setUpdating (true);
    set_sensitive (true);

    //-----------------------------------------------------------page page
    _rcp_bg.setRgba32 (nv->pagecolor);
    _rcb_canb.setActive (nv->showborder);
    _rcb_bord.setActive (nv->borderlayer == SP_BORDER_LAYER_TOP);
    _rcp_bord.setRgba32 (nv->bordercolor);
    _rcb_shad.setActive (nv->showpageshadow);

    if (nv->doc_units)
        _rum_deflt.setUnit (nv->doc_units);

    double const doc_w_px = sp_document_width(sp_desktop_document(dt));
    double const doc_h_px = sp_document_height(sp_desktop_document(dt));
    _page_sizer.setDim (doc_w_px, doc_h_px);

    //-----------------------------------------------------------guide page

    _rcb_sgui.setActive (nv->showguides);
    _rcp_gui.setRgba32 (nv->guidecolor);
    _rcp_hgui.setRgba32 (nv->guidehicolor);

    //-----------------------------------------------------------snap page

    _rcbsnbb.setActive (nv->snap_manager.snapprefs.getSnapModeBBox());
    _rcbsnn.setActive (nv->snap_manager.snapprefs.getSnapModeNode());
    _rcbsng.setActive (nv->snap_manager.snapprefs.getSnapModeGuide());
    _rcbic.setActive (nv->snap_manager.snapprefs.getIncludeItemCenter());
    _rcbsm.setActive (nv->snap_manager.snapprefs.getSnapSmoothNodes());
    _rcbsigg.setActive (nv->snap_manager.snapprefs.getSnapIntersectionGG());
    _rcbsils.setActive (nv->snap_manager.snapprefs.getSnapIntersectionCS());
    _rcbsnop.setActive(nv->snap_manager.object.getSnapToItemPath());
    _rcbsnon.setActive(nv->snap_manager.object.getSnapToItemNode());
    _rcbsnbbp.setActive(nv->snap_manager.object.getSnapToBBoxPath());
    _rcbsnbbn.setActive(nv->snap_manager.object.getSnapToBBoxNode());
    _rcbsnpb.setActive(nv->snap_manager.object.getSnapToPageBorder());
    _rsu_sno.setValue (nv->objecttolerance);

    _rsu_sn.setValue (nv->gridtolerance);

    _rsu_gusn.setValue (nv->guidetolerance);

    _rcbs.setActive (nv->snap_manager.snapprefs.getSnapEnabledGlobally());

    //-----------------------------------------------------------grids page

    update_gridspage();

    //------------------------------------------------Color Management page

#if ENABLE_LCMS
    populate_embedded_profiles_box();
    populate_available_profiles();
#endif // ENABLE_LCMS

    _wr.setUpdating (false);
}

// TODO: copied from fill-and-stroke.cpp factor out into new ui/widget file?
Gtk::HBox&
DocumentProperties::_createPageTabLabel(const Glib::ustring& label, const char *label_image)
{
    Gtk::HBox *_tab_label_box = manage(new Gtk::HBox(false, 0));
    _tab_label_box->set_spacing(4);
    _tab_label_box->pack_start(*Glib::wrap(sp_icon_new(Inkscape::ICON_SIZE_DECORATION,
                                                       label_image)));

    Gtk::Label *_tab_label = manage(new Gtk::Label(label, true));
    _tab_label_box->pack_start(*_tab_label);
    _tab_label_box->show_all();

    return *_tab_label_box;
}

//--------------------------------------------------------------------

void
DocumentProperties::on_response (int id)
{
    if (id == Gtk::RESPONSE_DELETE_EVENT || id == Gtk::RESPONSE_CLOSE)
    {
        _rcp_bg.closeWindow();
        _rcp_bord.closeWindow();
        _rcp_gui.closeWindow();
        _rcp_hgui.closeWindow();
    }

    if (id == Gtk::RESPONSE_CLOSE)
        hide();
}

void
DocumentProperties::_handleDocumentReplaced(SPDesktop* desktop, SPDocument *document)
{
    Inkscape::XML::Node *repr = SP_OBJECT_REPR(sp_desktop_namedview(desktop));
    repr->addListener(&_repr_events, this);
    Inkscape::XML::Node *root = SP_OBJECT_REPR(document->root);
    root->addListener(&_repr_events, this);
    update();
}

void
DocumentProperties::_handleActivateDesktop(Inkscape::Application *, SPDesktop *desktop)
{
    Inkscape::XML::Node *repr = SP_OBJECT_REPR(sp_desktop_namedview(desktop));
    repr->addListener(&_repr_events, this);
    Inkscape::XML::Node *root = SP_OBJECT_REPR(sp_desktop_document(desktop)->root);
    root->addListener(&_repr_events, this);
    update();
}

void
DocumentProperties::_handleDeactivateDesktop(Inkscape::Application *, SPDesktop *desktop)
{
    Inkscape::XML::Node *repr = SP_OBJECT_REPR(sp_desktop_namedview(desktop));
    repr->removeListenerByData(this);
    Inkscape::XML::Node *root = SP_OBJECT_REPR(sp_desktop_document(desktop)->root);
    root->removeListenerByData(this);
}

static void
on_child_added(Inkscape::XML::Node */*repr*/, Inkscape::XML::Node */*child*/, Inkscape::XML::Node */*ref*/, void *data)
{
    if (DocumentProperties *dialog = static_cast<DocumentProperties *>(data))
        dialog->update_gridspage();
}

static void
on_child_removed(Inkscape::XML::Node */*repr*/, Inkscape::XML::Node */*child*/, Inkscape::XML::Node */*ref*/, void *data)
{
    if (DocumentProperties *dialog = static_cast<DocumentProperties *>(data))
        dialog->update_gridspage();
}



/**
 * Called when XML node attribute changed; updates dialog widgets.
 */
static void
on_repr_attr_changed (Inkscape::XML::Node *, gchar const *, gchar const *, gchar const *, bool, gpointer data)
{
    if (DocumentProperties *dialog = static_cast<DocumentProperties *>(data))
        dialog->update();
}


/*########################################################################
# BUTTON CLICK HANDLERS    (callbacks)
########################################################################*/

void
DocumentProperties::onNewGrid()
{
    SPDesktop *dt = getDesktop();
    Inkscape::XML::Node *repr = SP_OBJECT_REPR(sp_desktop_namedview(dt));
    SPDocument *doc = sp_desktop_document(dt);

    Glib::ustring typestring = _grids_combo_gridtype.get_active_text();
    CanvasGrid::writeNewGridToRepr(repr, doc, CanvasGrid::getGridTypeFromName(typestring.c_str()));

    // toggle grid showing to ON:
    dt->showGrids(true);
}


void
DocumentProperties::onRemoveGrid()
{
    gint pagenum = _grids_notebook.get_current_page();
    if (pagenum == -1) // no pages
      return;

    SPDesktop *dt = getDesktop();
    SPNamedView *nv = sp_desktop_namedview(dt);
    Inkscape::CanvasGrid * found_grid = NULL;
    int i = 0;
    for (GSList const * l = nv->grids; l != NULL; l = l->next, i++) {  // not a very nice fix, but works.
        Inkscape::CanvasGrid * grid = (Inkscape::CanvasGrid*) l->data;
        if (pagenum == i) {
            found_grid = grid;
            break; // break out of for-loop
        }
    }
    if (found_grid) {
        // delete the grid that corresponds with the selected tab
        // when the grid is deleted from SVG, the SPNamedview handler automatically deletes the object, so found_grid becomes an invalid pointer!
        found_grid->repr->parent()->removeChild(found_grid->repr);
        sp_document_done(sp_desktop_document(dt), SP_VERB_DIALOG_NAMEDVIEW, _("Remove grid"));
    }
}


} // namespace Dialog
} // namespace UI
} // namespace Inkscape

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=99 :
