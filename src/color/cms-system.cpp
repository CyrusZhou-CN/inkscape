// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A class to provide access to system/user ICC color profiles.
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"  // only include where actually required!
#endif

#include "cms-system.h"

#include <iomanip>

#include <glibmm.h>

#include "cms-util.h"
#include "document.h"
#include "preferences.h"

#include "io/resource.h"
#include "object/color-profile.h"

#ifdef _WIN32
#include <windows.h>
#include <icm.h>
#endif

using Inkscape::ColorProfile;

namespace Inkscape {

/**
 * Holds information about one ICC profile.
 */
class ProfileInfo
{
public:
    ProfileInfo(cmsHPROFILE prof, Glib::ustring &&path);

    Glib::ustring const &getName() const { return _name; }
    Glib::ustring const &getPath() const { return _path; }
    cmsColorSpaceSignature getSpace() const { return _profileSpace; }
    cmsProfileClassSignature getClass() const { return _profileClass; }

private:
    Glib::ustring _path;
    Glib::ustring _name;
    cmsColorSpaceSignature _profileSpace;
    cmsProfileClassSignature _profileClass;
};

ProfileInfo::ProfileInfo(cmsHPROFILE prof, Glib::ustring &&path)
    : _path(std::move(path))
    , _name(get_color_profile_name(prof))
    , _profileSpace(cmsGetColorSpace(prof))
    , _profileClass(cmsGetDeviceClass(prof))
{
}

CMSSystem::CMSSystem()
{
    // Read in profiles (move to refresh()?).
    load_profiles();

    // Create generic sRGB profile.
    sRGB_profile = cmsCreate_sRGBProfile();
}

CMSSystem::~CMSSystem()
{
    if (current_monitor_profile) {
        cmsCloseProfile(current_monitor_profile);
    }

    if (current_proof_profile) {
        cmsCloseProfile(current_proof_profile);
    }

    if (sRGB_profile) {
        cmsCloseProfile(sRGB_profile);
    }
}

/*
 * We track last transform created so we can delete it later.
 *
 * This is OK since we only have one transform for all montiors/canvases. If we choose to allow the
 * user to assign different profiles to different monitors or have CMS preferences that are not
 * global, we'll need to have either one transform per monitor or one transform per canvas.
 */

// Search for system ICC profile files and add them to list.
void CMSSystem::load_profiles()
{
    system_profile_infos.clear(); // Allows us to refresh list if necessary.

    // Get list of all possible file directories, with flag if they are "home" directories or not.
    auto directory_paths = get_directory_paths();

    // Look for icc files in specified directories.
    for (auto const &directory_path : directory_paths) {

        using Inkscape::IO::Resource::get_filenames;
        for (auto const &filename : get_filenames(directory_path.first, {".icc", ".icm"})) {

            // Check if files are ICC files and extract out basic information, add to list.
            if (!is_icc_file(filename)) {
                std::cerr << "CMSSystem::load_profiles: " << filename << " is not an ICC file!" << std::endl;
                continue;
            }

            cmsHPROFILE profile = cmsOpenProfileFromFile(filename.c_str(), "r");
            if (!profile) {
                std::cerr << "CMSSystem::load_profiles: failed to load " << filename << std::endl;
                continue;
            }

            ICCProfileInfo info(profile, filename, directory_path.second);
            cmsCloseProfile(profile);
            profile = nullptr;

            bool same_name = false;
            for (auto const &profile_info : system_profile_infos) {
                if (profile_info.get_name() == info.get_name() ) {
                    same_name = true;
                    std::cerr << "CMSSystem::load_profiles: ICC profile with duplicate name: " << profile_info.get_name() << ":" << std::endl;
                    std::cerr << "   " << profile_info.get_path() << std::endl;
                    std::cerr << "   " <<         info.get_path() << std::endl;
                    break;
                }
            }

            if (!same_name) {
                system_profile_infos.emplace_back(std::move(info));
            }
        }
    }
}

// Create list of all directories where ICC profiles are expected to be found.
std::vector<std::pair<std::string, bool>> CMSSystem::get_directory_paths()
{
    std::vector<std::pair<std::string, bool>> paths;

    // First try user's local directory.
    paths.emplace_back(Glib::build_filename(Glib::get_user_data_dir(), "color", "icc"), true);

    // See https://github.com/hughsie/colord/blob/fe10f76536bb27614ced04e0ff944dc6fb4625c0/lib/colord/cd-icc-store.c#L590

    // User store
    paths.emplace_back(Glib::build_filename(Glib::get_user_data_dir(), "icc"), true);

    paths.emplace_back(Glib::build_filename(Glib::get_home_dir(), ".color", "icc"), true);

    // System store
    paths.emplace_back("/var/lib/color/icc", false);
    paths.emplace_back("/var/lib/colord/icc", false);

    auto data_directories = Glib::get_system_data_dirs();
    for (auto const &data_directory : data_directories) {
        paths.emplace_back(Glib::build_filename(data_directory, "color", "icc"), false);
    }

#ifdef __APPLE__
    paths.emplace_back("/System/Library/ColorSync/Profiles", false);
    paths.emplace_back("/Library/ColorSync/Profiles", false);

    paths.emplace_back(Glib::build_filename(Glib::get_home_dir(), "Library", "ColorSync", "Profiles"), true);
#endif // __APPLE__

#ifdef _WIN32
    wchar_t pathBuf[MAX_PATH + 1];
    pathBuf[0] = 0;
    DWORD pathSize = sizeof(pathBuf);
    g_assert(sizeof(wchar_t) == sizeof(gunichar2));
    if (GetColorDirectoryW(NULL, pathBuf, &pathSize)) {
        auto utf8Path = g_utf16_to_utf8((gunichar2*)(&pathBuf[0]), -1, NULL, NULL, NULL);
        if (!g_utf8_validate(utf8Path, -1, NULL)) {
            g_warning( "GetColorDirectoryW() resulted in invalid UTF-8" );
        } else {
            paths.emplace_back(utf8Path, false);
        }
        g_free(utf8Path);
    }
#endif // _WIN32

    return paths;
}

// Get the user set monitor profile.
cmsHPROFILE CMSSystem::get_monitor_profile()
{
    static Glib::ustring current_monitor_uri;
    static bool use_user_monitor_profile_old = false;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool use_user_monitor_profile = prefs->getBool("/options/displayprofile/use_user_profile", false);

    if (use_user_monitor_profile_old != use_user_monitor_profile) {
        use_user_monitor_profile_old  = use_user_monitor_profile;
        current_monitor_profile_changed = true;
    }

    if (!use_user_monitor_profile) {
        if (current_monitor_profile) {
            cmsCloseProfile(current_monitor_profile);
            current_monitor_profile = nullptr;
            current_monitor_uri.clear();
        }
        return current_monitor_profile;
    }

    Glib::ustring new_uri = prefs->getString("/options/displayprofile/uri");

    if (!new_uri.empty()) {

        // User defined monitor profile.
        if (new_uri != current_monitor_uri) {

            // Monitor profile changed
            current_monitor_profile_changed = true;
            current_monitor_uri.clear();

            // Delete old profile
            if (current_monitor_profile) {
                cmsCloseProfile(current_monitor_profile);
            }

            // Open new profile
            current_monitor_profile = cmsOpenProfileFromFile(new_uri.data(), "r");
            if (current_monitor_profile) {

                // A display profile must be of the right type.
                cmsColorSpaceSignature space = cmsGetColorSpace(current_monitor_profile);
                cmsProfileClassSignature profClass = cmsGetDeviceClass(current_monitor_profile);

                if (profClass != cmsSigDisplayClass) {
                    std::cerr << "CMSSystem::get_monitor_profile: Not a display (monitor) profile: " << new_uri << std::endl;
                    cmsCloseProfile(current_monitor_profile);
                    current_monitor_profile = nullptr;
                } else if (space != cmsSigRgbData) {
                    std::cerr << "CMSSystem::get_monitor_profile: Not an RGB profile: " << new_uri << std::endl;
                    cmsCloseProfile(current_monitor_profile);
                    current_monitor_profile = nullptr;
                } else {
                    current_monitor_uri = new_uri;
                }
            }
        }
    } else if (current_monitor_profile) {
        cmsCloseProfile(current_monitor_profile);
        current_monitor_profile = nullptr;
        current_monitor_uri.clear();
        current_monitor_profile_changed = true;
    }

    return current_monitor_profile;
}

// Get the user set proof profile.
cmsHPROFILE CMSSystem::get_proof_profile()
{
    static Glib::ustring current_proof_uri;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring new_uri = prefs->getString("/options/softproof/uri");

    if (!new_uri.empty()) {

        // User defined proof profile.
        if (new_uri != current_proof_uri) {

            // Proof profile changed
            current_proof_profile_changed = true;
            current_proof_uri.clear();

            // Delete old profile
            if (current_proof_profile) {
                cmsCloseProfile(current_proof_profile);
            }

            // Open new profile
            current_proof_profile = cmsOpenProfileFromFile(new_uri.data(), "r");
            if (current_proof_profile) {

                // We don't check validity of proof profile!
                current_proof_uri = new_uri;
            }
        }
    } else if (current_proof_profile) {
        cmsCloseProfile(current_proof_profile);
        current_proof_profile = nullptr;
        current_proof_uri.clear();
        current_proof_profile_changed = true;
    }

    return current_proof_profile;
}

// Get a color profile handle corresponding to "name" from the document. Also, optionally, get intent.
cmsHPROFILE CMSSystem::get_document_profile(SPDocument *document, unsigned *intent, char const *name)
{
    cmsHPROFILE profile_handle = nullptr;

    // Search through <ColorProfile> elements for one with matching name.
    ColorProfile *color_profile = nullptr;
    auto color_profiles = document->getResourceList("iccprofile");
    for (auto *object : color_profiles) {
        if (auto color_profile_test = cast<ColorProfile>(object)) {
            if (color_profile_test->name && (strcmp(color_profile_test->name, name) == 0)) {
                color_profile = color_profile_test;
            }
        }
    }

    // If found, set profile_handle pointer.
    if (color_profile) {
        profile_handle = color_profile->getHandle();
    }

    // If requested, fill "RENDERING_INTENT" value.
    if (intent) {
        *intent = color_profile ? color_profile->rendering_intent : (guint)RENDERING_INTENT_UNKNOWN;
    }

    return profile_handle;
}

// Returns vector of names to list in Preferences dialog: display (monitor) profiles.
std::vector<Glib::ustring> CMSSystem::get_monitor_profile_names() const
{
    std::vector<Glib::ustring> result;

    for (auto const &profile_info : system_profile_infos) {
        if (profile_info.get_profileclass() == cmsSigDisplayClass &&
            profile_info.get_colorspace() == cmsSigRgbData)
        {
            result.emplace_back(profile_info.get_name());
        }
    }
    std::sort(result.begin(), result.end());

    return result;
}

// Returns vector of names to list in Preferences dialog: proofing profiles.
std::vector<Glib::ustring> CMSSystem::get_softproof_profile_names() const
{
    std::vector<Glib::ustring> result;

    for (auto const &profile_info : system_profile_infos) {
        if (profile_info.get_profileclass() == cmsSigOutputClass) {
            result.emplace_back(profile_info.get_name());
        }
    }
    std::sort(result.begin(), result.end());

    return result;
}

// Returns location of a profile.
std::string CMSSystem::get_path_for_profile(Glib::ustring const &name) const
{
    std::string result;

    for (auto const &profile_info : system_profile_infos) {
        if (name == profile_info.get_name()) {
            result = profile_info.get_path();
            break;
        }
    }

    return result;
}

// Static, doesn't rely on class. Simply calls lcms' cmsDoTransform.
// Called from Canvas and icc_color_to_sRGB in sgv-color.cpp.
void CMSSystem::do_transform(cmsHTRANSFORM transform, unsigned char *inBuf, unsigned char *outBuf, unsigned size)
{
    cmsDoTransform(transform, inBuf, outBuf, size);
}

// Called by Canvas to obtain transform.
// Currently there is one transform for all monitors.
// Transform immutably shared between CMSSystem and Canvas.
std::shared_ptr<CMSTransform const> const &CMSSystem::get_cms_transform()
{
    bool preferences_changed = false;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool warn              = prefs->getBool(      "/options/softproof/gamutwarn");
    int intent             = prefs->getIntLimited("/options/displayprofile/intent", 0, 0, 3);
    int proofIntent        = prefs->getIntLimited("/options/softproof/intent",      0, 0, 3);
    bool bpc               = prefs->getBool(      "/options/softproof/bpc");
    Glib::ustring colorStr = prefs->getString(    "/options/softproof/gamutcolor");
    Gdk::RGBA gamutColor(colorStr.empty() ? "#808080" : colorStr);

    if (gamutWarn       != warn        ||
        lastIntent      != intent      ||
        lastProofIntent != proofIntent ||
        lastBPC         != bpc         ||
        lastGamutColor  != gamutColor  )
    {
        preferences_changed = true;

        gamutWarn       = warn;
        lastIntent      = intent;
        lastProofIntent = proofIntent;
        lastBPC         = bpc;
        lastGamutColor  = gamutColor;
    }

    auto monitor_profile = get_monitor_profile();
    auto proof_profile   = get_proof_profile();

    bool need_to_update = preferences_changed || current_monitor_profile_changed || current_proof_profile_changed;

    if (need_to_update) {
        if (proof_profile) {
            cmsUInt32Number dwFlags = cmsFLAGS_SOFTPROOFING;

            if (gamutWarn) {
                dwFlags |= cmsFLAGS_GAMUTCHECK;

                auto gamutColor_r = gamutColor.get_red_u();
                auto gamutColor_g = gamutColor.get_green_u();
                auto gamutColor_b = gamutColor.get_blue_u();

                cmsUInt16Number newAlarmCodes[cmsMAXCHANNELS] = {0};
                newAlarmCodes[0] = gamutColor_r;
                newAlarmCodes[1] = gamutColor_g;
                newAlarmCodes[2] = gamutColor_b;
                newAlarmCodes[3] = ~0;
                cmsSetAlarmCodes(newAlarmCodes);
            }

            if (bpc) {
                dwFlags |= cmsFLAGS_BLACKPOINTCOMPENSATION;
            }

            current_transform = CMSTransform::create(
                cmsCreateProofingTransform(sRGB_profile, TYPE_BGRA_8, monitor_profile, TYPE_BGRA_8,
                                           proof_profile, intent, proofIntent, dwFlags));

        } else if (monitor_profile) {
            current_transform = CMSTransform::create(
                cmsCreateTransform(sRGB_profile, TYPE_BGRA_8, monitor_profile, TYPE_BGRA_8, intent, 0));
        }
    }

    return current_transform;
}

CMSSystem *CMSSystem::_instance = nullptr;

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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
