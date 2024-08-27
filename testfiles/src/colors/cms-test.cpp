// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Unit tests for color objects.
 *
 * Copyright (C) 2023 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cairomm/context.h>
#include <cairomm/surface.h>
#include <gtest/gtest.h>

#include "colors/cms/profile.h"
#include "colors/cms/system.h"
#include "colors/cms/transform.h"
#include "colors/color.h"
#include "colors/manager.h"
#include "colors/utils.h"
#include "preferences.h"

static std::string icc_dir = INKSCAPE_TESTS_DIR "/data/colors";
static std::string grb_profile = INKSCAPE_TESTS_DIR "/data/colors/SwappedRedAndGreen.icc";
static std::string cmyk_profile = INKSCAPE_TESTS_DIR "/data/colors/default_cmyk.icc";
static std::string display_profile = INKSCAPE_TESTS_DIR "/data/colors/display.icc";
static std::string not_a_profile = INKSCAPE_TESTS_DIR "/data/colors/color-cms.svg";

using namespace Inkscape::Colors;

namespace {

// ================= CMS::System ================= //

class ColorCmsSystem : public ::testing::Test
{
protected:
    void SetUp() override
    {
        cms = &CMS::System::get();
        cms->clearDirectoryPaths();
        cms->addDirectoryPath(icc_dir, false);
        cms->refreshProfiles();

        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setString("/options/displayprofile/uri", display_profile);
        prefs->setBool("/options/displayprofile/enabled", true);
    }
    void TearDown() override {}
    Inkscape::Colors::CMS::System *cms = nullptr;
};

TEST_F(ColorCmsSystem, getDirectoryPaths)
{
    ASSERT_EQ(cms->getDirectoryPaths().size(), 1);
    ASSERT_EQ(cms->getDirectoryPaths()[0].first, icc_dir);
}

TEST_F(ColorCmsSystem, addDirectoryPath)
{
    cms->clearDirectoryPaths();
    cms->addDirectoryPath("nope", false);
    cms->addDirectoryPath("yep", true);
    ASSERT_EQ(cms->getDirectoryPaths().size(), 2);
    ASSERT_EQ(cms->getDirectoryPaths()[0].first, "nope");
    ASSERT_EQ(cms->getDirectoryPaths()[1].first, "yep");
}

TEST_F(ColorCmsSystem, clearDirectoryPaths)
{
    cms->clearDirectoryPaths();
    ASSERT_GE(cms->getDirectoryPaths().size(), 2);
}

TEST_F(ColorCmsSystem, getProfiles)
{
    auto profiles = cms->getProfiles();
    ASSERT_EQ(profiles.size(), 3);

    ASSERT_EQ(profiles[0]->getName(), "Artifex CMYK SWOP Profile");
    ASSERT_EQ(profiles[1]->getName(), "C.icc");
    ASSERT_EQ(profiles[2]->getName(), "Swapped Red and Green");
}

TEST_F(ColorCmsSystem, getProfileByName)
{
    auto profile = cms->getProfile("Swapped Red and Green");
    ASSERT_TRUE(profile);
    ASSERT_EQ(profile->getPath(), grb_profile);
}

TEST_F(ColorCmsSystem, getProfileByID)
{
    auto profile = cms->getProfile("f9eda5a42a222a28f0adb82a938eeb0e");
    ASSERT_TRUE(profile);
    ASSERT_EQ(profile->getName(), "Swapped Red and Green");
}

TEST_F(ColorCmsSystem, getProfileByPath)
{
    auto profile = cms->getProfile(grb_profile);
    ASSERT_TRUE(profile);
    ASSERT_EQ(profile->getId(), "f9eda5a42a222a28f0adb82a938eeb0e");
}

TEST_F(ColorCmsSystem, getDisplayProfiles)
{
    auto profiles = cms->getDisplayProfiles();
    ASSERT_EQ(profiles.size(), 1);
    ASSERT_EQ(profiles[0]->getName(), "C.icc");
}

TEST_F(ColorCmsSystem, getDisplayProfile)
{
    bool updated = false;
    auto profile = cms->getDisplayProfile(updated);
    ASSERT_TRUE(updated);
    ASSERT_TRUE(profile);
    ASSERT_EQ(profile->getName(), "C.icc");
}

TEST_F(ColorCmsSystem, getDisplayTransform)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    bool updated = false;
    auto profile = cms->getDisplayProfile(updated);
    ASSERT_TRUE(profile);

    ASSERT_TRUE(cms->getDisplayTransform());
    prefs->setBool("/options/displayprofile/enabled", false);
    ASSERT_FALSE(cms->getDisplayTransform());
    prefs->setBool("/options/displayprofile/enabled", true);
    ASSERT_TRUE(cms->getDisplayTransform());
    prefs->setString("/options/displayprofile/uri", "");
    ASSERT_FALSE(cms->getDisplayTransform());
}

TEST_F(ColorCmsSystem, getOutputProfiles)
{
    auto profiles = cms->getOutputProfiles();
    ASSERT_EQ(profiles.size(), 1);
    ASSERT_EQ(profiles[0]->getName(), "Artifex CMYK SWOP Profile");
}

TEST_F(ColorCmsSystem, refreshProfiles)
{
    ASSERT_EQ(cms->getDirectoryPaths().size(), 1);
    cms->clearDirectoryPaths();
    cms->refreshProfiles();
    ASSERT_GE(cms->getDirectoryPaths().size(), 5);
}

// ================= CMS::Profile ================= //

TEST(ColorCmsProfile, create)
{
    auto rgb_profile = cmsCreate_sRGBProfile();
    auto profile = CMS::Profile::create(rgb_profile, "", false);
    ASSERT_TRUE(profile);
    ASSERT_EQ(profile->getId(), "");
    ASSERT_EQ(profile->getName(), "sRGB built-in");
    ASSERT_EQ(profile->getPath(), "");
    ASSERT_FALSE(profile->inHome());
    ASSERT_EQ(profile->getHandle(), rgb_profile);
}

TEST(ColorCmsProfile, create_from_uri)
{
    auto profile = CMS::Profile::create_from_uri(grb_profile);

    ASSERT_EQ(profile->getId(), "f9eda5a42a222a28f0adb82a938eeb0e");
    ASSERT_EQ(profile->getName(), "Swapped Red and Green");
    ASSERT_EQ(profile->getName(true), "Swapped-Red-and-Green");
    ASSERT_EQ(profile->getPath(), grb_profile);
    ASSERT_EQ(profile->getColorSpace(), cmsSigRgbData);
    ASSERT_EQ(profile->getProfileClass(), cmsSigDisplayClass);

    ASSERT_FALSE(profile->inHome());
    ASSERT_FALSE(profile->isForDisplay());
}

TEST(ColorCmsProfile, create_from_data)
{
    // Prepare some memory first
    cmsUInt32Number len = 0;
    auto rgb_profile = cmsCreate_sRGBProfile();
    ASSERT_TRUE(cmsSaveProfileToMem(rgb_profile, nullptr, &len));
    auto buf = std::vector<unsigned char>(len);
    cmsSaveProfileToMem(rgb_profile, &buf.front(), &len);
    std::string data(buf.begin(), buf.end());

    auto profile = CMS::Profile::create_from_data(data);
    ASSERT_TRUE(profile);
}

TEST(ColorCmsProfile, create_srgb)
{
    auto profile = CMS::Profile::create_srgb();
    ASSERT_TRUE(profile);
}

TEST(ColorCmsProfile, equalTo)
{
    auto profile1 = CMS::Profile::create_from_uri(grb_profile);
    auto profile2 = CMS::Profile::create_from_uri(grb_profile);
    auto profile3 = CMS::Profile::create_from_uri(cmyk_profile);
    ASSERT_EQ(*profile1, *profile2);
    ASSERT_NE(*profile1, *profile3);
}

TEST(ColorCmsProfile, isIccFile)
{
    ASSERT_TRUE(CMS::Profile::isIccFile(grb_profile));
    ASSERT_FALSE(CMS::Profile::isIccFile(not_a_profile));
    ASSERT_FALSE(CMS::Profile::isIccFile(icc_dir + "not_existing.icc"));
}

TEST(ColorCmsProfile, cmsDumpBase64)
{
    auto profile = CMS::Profile::create_from_uri(grb_profile);
    // First 100 bytes taken from the base64 of the icc profile file on the command line
    ASSERT_EQ(profile->dumpBase64().substr(0, 100),
              "AAA9aGxjbXMEMAAAbW50clJHQiBYWVogB+YAAgAWAA0AGQAuYWNzcEFQUEwAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAPbWAAEA");
}

// ================= CMS::Transform ================= //

TEST(ColorCmsTransform, applyTransformColor)
{
    auto srgb = CMS::Profile::create_srgb();
    auto profile = CMS::Profile::create_from_uri(grb_profile);
    auto tr = CMS::Transform::create_for_cms(srgb, profile, RenderingIntent::RELATIVE_COLORIMETRIC);

    std::vector<double> output = {0.1, 0.2, 0.3, 1.0};
    tr->do_transform(output);
    ASSERT_NEAR(output[0], 0.2, 0.01);
    ASSERT_NEAR(output[1], 0.1, 0.01);
    ASSERT_NEAR(output[2], 0.3, 0.01);
    ASSERT_EQ(output[3], 1.0);
}

TEST(ColorCmsTransform, gamutCheckColor)
{
    auto srgb = CMS::Profile::create_srgb();
    auto profile = CMS::Profile::create_from_uri(cmyk_profile);
    ASSERT_TRUE(srgb);
    ASSERT_TRUE(profile);

    auto tr1 = CMS::Transform::create_for_cms_checker(srgb, profile);
    ASSERT_TRUE(tr1);

    // An RGB color which is within the cmyk color profile gamut
    EXPECT_FALSE(tr1->check_gamut({0.83, 0.19, 0.49}));

    // An RGB color (magenta) which is outside the cmyk color profile
    EXPECT_TRUE(tr1->check_gamut({1.0, 0.0, 1.0}));
}

::testing::AssertionResult CairoPixelIs(Cairo::RefPtr<Cairo::ImageSurface> &cs, unsigned expected_color)
{
    auto data = cs->get_data();
    // BGRA8 cairo surfaces, (aka ARGB32)
    unsigned found_color = SP_RGBA32_U_COMPOSE(data[2], data[1], data[0], data[3]);

    if (found_color != expected_color) {
        return ::testing::AssertionFailure() << "\n"
                                             << Inkscape::Colors::rgba_to_hex(found_color) << "\n != \n"
                                             << Inkscape::Colors::rgba_to_hex(expected_color);
    }
    return ::testing::AssertionSuccess();
}

TEST(ColorCmsTransform, applyTransformCairo)
{
    auto srgb = CMS::Profile::create_srgb();
    auto profile = CMS::Profile::create_from_uri(grb_profile);
    auto tr = CMS::Transform::create_for_cairo(srgb, profile);

    auto cs = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, 1, 1);
    auto cr = Cairo::Context::create(cs);

    cr->set_source_rgb(0.5, 0, 0);
    cr->paint();
    ASSERT_TRUE(CairoPixelIs(cs, 0x800000ff));
    tr->do_transform(cs, cs);
    ASSERT_TRUE(CairoPixelIs(cs, 0x008000ff));
}

TEST(ColorCmsTransform, applyWithProofing)
{
    auto srgb = CMS::Profile::create_srgb();
    auto profile = CMS::Profile::create_from_uri(cmyk_profile);
    auto proofed = CMS::Transform::create_for_cairo(srgb, srgb, profile, RenderingIntent::AUTO, false);

    auto cs = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, 1, 1);
    auto cr = Cairo::Context::create(cs);

    cr->set_source_rgb(1.0, 0, 1.0);
    cr->paint();

    ASSERT_TRUE(CairoPixelIs(cs, 0xff00ffff));
    proofed->do_transform(cs, cs);
    // Value should be
    ASSERT_TRUE(CairoPixelIs(cs, 0xba509dff));

    cr->set_source_rgb(0.83, 0.19, 0.49);
    cr->paint();

    ASSERT_TRUE(CairoPixelIs(cs, 0xd4307dff));
    proofed->do_transform(cs, cs);
    ASSERT_TRUE(CairoPixelIs(cs, 0xd42279ff));
}

TEST(ColorCmsTransform, applyWithGamutWarn)
{
    auto srgb = CMS::Profile::create_srgb();
    auto profile = CMS::Profile::create_from_uri(cmyk_profile);
    auto warned = CMS::Transform::create_for_cairo(srgb, srgb, profile, RenderingIntent::AUTO, true);
    warned->set_gamut_warn({0.0, 1.0, 0.0});

    auto cs = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, 1, 1);
    auto cr = Cairo::Context::create(cs);

    cr->set_source_rgb(1.0, 0, 1.0);
    cr->paint();

    ASSERT_TRUE(CairoPixelIs(cs, 0xff00ffff));
    warned->do_transform(cs, cs);
    ASSERT_TRUE(CairoPixelIs(cs, 0x00ff00ff));

    cr->set_source_rgb(0.83, 0.19, 0.49);
    cr->paint();

    ASSERT_TRUE(CairoPixelIs(cs, 0xd4307dff));
    warned->do_transform(cs, cs);
    ASSERT_TRUE(CairoPixelIs(cs, 0xd42279ff));
}

} // namespace

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
