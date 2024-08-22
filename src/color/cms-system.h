// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TODO: insert short description here
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2017 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#ifndef INKSCAPE_COLOR_CMS_SYSTEM_H
#define INKSCAPE_COLOR_CMS_SYSTEM_H

/** \file 
 * Access to ICC profiles provided by system.
 * Track which profile to use for proofing.(?)
 * Track which profile to use on which monitor.
 */

#include <vector>
#include <memory>
#include <cassert>

#include <glibmm/ustring.h>
#include <gdkmm/rgba.h>

#include <lcms2.h>  // cmsHTRANSFORM

#include "cms-color-types.h" // cmsColorSpaceSignature, cmsProfileClassSignature
#include "cms-util.h"

class SPDocument;
class ProfileInfo;

namespace Inkscape {

class ColorProfile;

class CMSTransform
{
public:
    explicit CMSTransform(cmsHTRANSFORM handle) : _handle(handle) { assert(_handle); }
    CMSTransform(CMSTransform const &) = delete;
    CMSTransform &operator=(CMSTransform const &) = delete;
    ~CMSTransform() { cmsDeleteTransform(_handle); }

    cmsHTRANSFORM getHandle() const { return _handle; }

    static std::shared_ptr<CMSTransform> create(cmsHTRANSFORM handle)
    {
        return handle ? std::make_shared<CMSTransform>(handle) : nullptr;
    }

private:
    cmsHTRANSFORM _handle;
};

class CMSSystem
{
public:
    /**
     * Access the singleton CMSSystem object.
     */
    static CMSSystem *get() {
        if (!_instance) {
            _instance = new CMSSystem();
        }
        return _instance;
    }

    static void unload() {
        if (_instance) {
            delete _instance;
            _instance = nullptr;
        }
    }

    static std::vector<std::pair<std::string, bool>> get_directory_paths();
    std::vector<ICCProfileInfo> const &get_system_profile_infos() const { return system_profile_infos; }
    std::vector<Glib::ustring> get_monitor_profile_names() const;
    std::vector<Glib::ustring> get_softproof_profile_names() const;
    std::string get_path_for_profile(Glib::ustring const &name) const;
    std::shared_ptr<CMSTransform const> const &get_cms_transform();
    static cmsHPROFILE get_document_profile(SPDocument *document, unsigned *intent, char const *name);

    static void do_transform(cmsHTRANSFORM transform, unsigned char *inBuf, unsigned char *outBuf, unsigned size);

private:
    CMSSystem();
    ~CMSSystem();

    void load_profiles(); // Should this be public (e.g., if a new ColorProfile is created).
    cmsHPROFILE get_monitor_profile(); // Get the user set monitor profile.
    cmsHPROFILE get_proof_profile(); // Get the user set proof profile.
    void clear_transform(); // Clears current_transform.

    static CMSSystem* _instance;

    // List of ICC profiles on system
    std::vector<ICCProfileInfo> system_profile_infos;

    // We track last transform settings. If there is a change, we delete create new transform.
    bool gamutWarn = false;
    Gdk::RGBA lastGamutColor = Gdk::RGBA("#808080");
    bool lastBPC = false;
    int lastIntent = INTENT_PERCEPTUAL;
    int lastProofIntent = INTENT_PERCEPTUAL;
    bool current_monitor_profile_changed = true; // Force at least one update.
    bool current_proof_profile_changed = true;

    // Shared immutably with all canvases.
    std::shared_ptr<CMSTransform const> current_transform;

    // So we can delete them later.
    cmsHPROFILE current_monitor_profile = nullptr;
    cmsHPROFILE current_proof_profile   = nullptr;
    cmsHPROFILE sRGB_profile            = nullptr;  // Genric sRGB profile, find it once on inititialization.
};

} // namespace Inkscape

#endif // INKSCAPE_COLOR_CMS_SYSTEM_H

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
