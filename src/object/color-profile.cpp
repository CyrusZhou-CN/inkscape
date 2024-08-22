// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TODO: insert short description here
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#ifdef HAVE_CONFIG_H
# include "config.h"  // only include where actually required!
#endif

#include "color-profile.h"

#include <cstring>
#include <utility>
#include <fcntl.h>
#include <unistd.h>

#include <gdkmm/rgba.h>
#include <glib/gstdio.h>
#include <glibmm/checksum.h>
#include <glibmm/convert.h>

#ifdef _WIN32
#include <windows.h>
#include <icm.h>
#endif

#include <lcms2.h>

#include "attributes.h"
#include "color.h"
#include "document.h"
#include "inkscape.h"
#include "preferences.h"
#include "uri.h"

#include "color/cms-system.h"
#include "color/color-profile-cms-fns.h"

#include <io/sys.h>
#include <io/resource.h>

#include "xml/repr.h"
#include "xml/href-attribute-helper.h"

using Inkscape::ColorProfile;
using Inkscape::ColorProfileImpl;

namespace Inkscape {

class ColorProfileImpl {
public:
    static cmsHPROFILE _sRGBProf;
    static cmsHPROFILE _NullProf;

    ColorProfileImpl();

    static cmsUInt32Number _getInputFormat( cmsColorSpaceSignature space );

    static cmsHPROFILE getNULLProfile();
    static cmsHPROFILE getSRGBProfile();

    void _clearProfile();

    cmsHPROFILE _profHandle;
    cmsProfileClassSignature _profileClass;
    cmsColorSpaceSignature _profileSpace;
    cmsHTRANSFORM _transf;
    cmsHTRANSFORM _revTransf;
    cmsHTRANSFORM _gamutTransf;
};

cmsColorSpaceSignature asICColorSpaceSig(ColorSpaceSig const & sig)
{
    return ColorSpaceSigWrapper(sig);
}

cmsProfileClassSignature asICColorProfileClassSig(ColorProfileClassSig const & sig)
{
    return ColorProfileClassSigWrapper(sig);
}

} // namespace Inkscape

ColorProfileImpl::ColorProfileImpl()
	:
    _profHandle(nullptr),
    _profileClass(cmsSigInputClass),
    _profileSpace(cmsSigRgbData),
    _transf(nullptr),
    _revTransf(nullptr),
    _gamutTransf(nullptr)
{
}


cmsHPROFILE ColorProfileImpl::_sRGBProf = nullptr;

cmsHPROFILE ColorProfileImpl::getSRGBProfile() {
    if ( !_sRGBProf ) {
        _sRGBProf = cmsCreate_sRGBProfile();
    }
    return ColorProfileImpl::_sRGBProf;
}

cmsHPROFILE ColorProfileImpl::_NullProf = nullptr;

cmsHPROFILE ColorProfileImpl::getNULLProfile() {
    if ( !_NullProf ) {
        _NullProf = cmsCreateNULLProfile();
    }
    return _NullProf;
}

ColorProfile::ColorProfile() : SPObject() {
    this->impl = new ColorProfileImpl();

    this->href = nullptr;
    this->local = nullptr;
    this->name = nullptr;
    this->intentStr = nullptr;
    this->rendering_intent = Inkscape::RENDERING_INTENT_UNKNOWN;
}

ColorProfile::~ColorProfile() = default;

bool ColorProfile::operator<(ColorProfile const &other) const {
    gchar *a_name_casefold = g_utf8_casefold(this->name, -1 );
    gchar *b_name_casefold = g_utf8_casefold(other.name, -1 );
    int result = g_strcmp0(a_name_casefold, b_name_casefold);
    g_free(a_name_casefold);
    g_free(b_name_casefold);
    return result < 0;
}

/**
 * Callback: free object
 */
void ColorProfile::release() {
    // Unregister ourselves
    if ( this->document ) {
        this->document->removeResource("iccprofile", this);
    }

    if ( this->href ) {
        g_free( this->href );
        this->href = nullptr;
    }

    if ( this->local ) {
        g_free( this->local );
        this->local = nullptr;
    }

    if ( this->name ) {
        g_free( this->name );
        this->name = nullptr;
    }

    if ( this->intentStr ) {
        g_free( this->intentStr );
        this->intentStr = nullptr;
    }

    this->impl->_clearProfile();

    delete this->impl;
    this->impl = nullptr;

    SPObject::release();
}

void ColorProfileImpl::_clearProfile()
{
    _profileSpace = cmsSigRgbData;

    if ( _transf ) {
        cmsDeleteTransform( _transf );
        _transf = nullptr;
    }
    if ( _revTransf ) {
        cmsDeleteTransform( _revTransf );
        _revTransf = nullptr;
    }
    if ( _gamutTransf ) {
        cmsDeleteTransform( _gamutTransf );
        _gamutTransf = nullptr;
    }
    if ( _profHandle ) {
        cmsCloseProfile( _profHandle );
        _profHandle = nullptr;
    }
}

/**
 * Callback: set attributes from associated repr.
 */
void ColorProfile::build(SPDocument *document, Inkscape::XML::Node *repr) {
    g_assert(this->href == nullptr);
    g_assert(this->local == nullptr);
    g_assert(this->name == nullptr);
    g_assert(this->intentStr == nullptr);

    SPObject::build(document, repr);

    this->readAttr(SPAttr::XLINK_HREF);
    this->readAttr(SPAttr::ID);
    this->readAttr(SPAttr::LOCAL);
    this->readAttr(SPAttr::NAME);
    this->readAttr(SPAttr::RENDERING_INTENT);

    // Register
    if ( document ) {
        document->addResource( "iccprofile", this );
    }
}


/**
 * Callback: set attribute.
 */
void ColorProfile::set(SPAttr key, gchar const *value) {
    switch (key) {
        case SPAttr::XLINK_HREF:
            if ( this->href ) {
                g_free( this->href );
                this->href = nullptr;
            }
            if ( value ) {
                this->href = g_strdup( value );
                if ( *this->href ) {

                    // TODO open filename and URIs properly
                    //FILE* fp = fopen_utf8name( filename, "r" );
                    //LCMSAPI cmsHPROFILE   LCMSEXPORT cmsOpenProfileFromMem(LPVOID MemPtr, cmsUInt32Number dwSize);

                    // Try to open relative
                    SPDocument *doc = this->document;
                    if (!doc) {
                        doc = SP_ACTIVE_DOCUMENT;
                        g_warning("this has no document.  using active");
                    }
                    //# 1.  Get complete filename of document
                    gchar const *docbase = doc->getDocumentFilename();

                    Inkscape::URI docUri("");
                    if (docbase) { // The file has already been saved
                        docUri = Inkscape::URI::from_native_filename(docbase);
                    }

                    this->impl->_clearProfile();

                    try {
                        auto hrefUri = Inkscape::URI(this->href, docUri);
                        auto contents = hrefUri.getContents();
                        this->impl->_profHandle = cmsOpenProfileFromMem(contents.data(), contents.size());
                    } catch (...) {
                        g_warning("Failed to open CMS profile URI '%.100s'", this->href);
                    }

                    if ( this->impl->_profHandle ) {
                        this->impl->_profileSpace = cmsGetColorSpace( this->impl->_profHandle );
                        this->impl->_profileClass = cmsGetDeviceClass( this->impl->_profHandle );
                    }
                }
            }
            this->requestModified(SP_OBJECT_MODIFIED_FLAG);
            break;

        case SPAttr::LOCAL:
            if ( this->local ) {
                g_free( this->local );
                this->local = nullptr;
            }
            this->local = g_strdup( value );
            this->requestModified(SP_OBJECT_MODIFIED_FLAG);
            break;

        case SPAttr::NAME:
            if ( this->name ) {
                g_free( this->name );
                this->name = nullptr;
            }
            this->name = g_strdup( value );
            this->requestModified(SP_OBJECT_MODIFIED_FLAG);
            break;

        case SPAttr::RENDERING_INTENT:
            if ( this->intentStr ) {
                g_free( this->intentStr );
                this->intentStr = nullptr;
            }
            this->intentStr = g_strdup( value );

            if ( value ) {
                if ( strcmp( value, "auto" ) == 0 ) {
                    this->rendering_intent = RENDERING_INTENT_AUTO;
                } else if ( strcmp( value, "perceptual" ) == 0 ) {
                    this->rendering_intent = RENDERING_INTENT_PERCEPTUAL;
                } else if ( strcmp( value, "relative-colorimetric" ) == 0 ) {
                    this->rendering_intent = RENDERING_INTENT_RELATIVE_COLORIMETRIC;
                } else if ( strcmp( value, "saturation" ) == 0 ) {
                    this->rendering_intent = RENDERING_INTENT_SATURATION;
                } else if ( strcmp( value, "absolute-colorimetric" ) == 0 ) {
                    this->rendering_intent = RENDERING_INTENT_ABSOLUTE_COLORIMETRIC;
                } else {
                    this->rendering_intent = RENDERING_INTENT_UNKNOWN;
                }
            } else {
                this->rendering_intent = RENDERING_INTENT_UNKNOWN;
            }

            this->requestModified(SP_OBJECT_MODIFIED_FLAG);
            break;

        default:
        	SPObject::set(key, value);
            break;
    }
}

/**
 * Callback: write attributes to associated repr.
 */
Inkscape::XML::Node* ColorProfile::write(Inkscape::XML::Document *xml_doc, Inkscape::XML::Node *repr, guint flags) {
    if ((flags & SP_OBJECT_WRITE_BUILD) && !repr) {
        repr = xml_doc->createElement("svg:color-profile");
    }

    if ( (flags & SP_OBJECT_WRITE_ALL) || this->href ) {
        Inkscape::setHrefAttribute(*repr, this->href );
    }

    if ( (flags & SP_OBJECT_WRITE_ALL) || this->local ) {
        repr->setAttribute( "local", this->local );
    }

    if ( (flags & SP_OBJECT_WRITE_ALL) || this->name ) {
        repr->setAttribute( "name", this->name );
    }

    if ( (flags & SP_OBJECT_WRITE_ALL) || this->intentStr ) {
        repr->setAttribute( "rendering-intent", this->intentStr );
    }

    SPObject::write(xml_doc, repr, flags);

    return repr;
}


struct MapMap {
    cmsColorSpaceSignature space;
    cmsUInt32Number inForm;
};

cmsUInt32Number ColorProfileImpl::_getInputFormat( cmsColorSpaceSignature space )
{
    MapMap possible[] = {
        {cmsSigXYZData,   TYPE_XYZ_16},
        {cmsSigLabData,   TYPE_Lab_16},
        //cmsSigLuvData
        {cmsSigYCbCrData, TYPE_YCbCr_16},
        {cmsSigYxyData,   TYPE_Yxy_16},
        {cmsSigRgbData,   TYPE_RGB_16},
        {cmsSigGrayData,  TYPE_GRAY_16},
        {cmsSigHsvData,   TYPE_HSV_16},
        {cmsSigHlsData,   TYPE_HLS_16},
        {cmsSigCmykData,  TYPE_CMYK_16},
        {cmsSigCmyData,   TYPE_CMY_16},
    };

    int index = 0;
    for ( guint i = 0; i < G_N_ELEMENTS(possible); i++ ) {
        if ( possible[i].space == space ) {
            index = i;
            break;
        }
    }

    return possible[index].inForm;
}

static int getLcmsIntent( guint svgIntent )
{
    int intent = INTENT_PERCEPTUAL;
    switch ( svgIntent ) {
        case Inkscape::RENDERING_INTENT_RELATIVE_COLORIMETRIC:
            intent = INTENT_RELATIVE_COLORIMETRIC;
            break;
        case Inkscape::RENDERING_INTENT_SATURATION:
            intent = INTENT_SATURATION;
            break;
        case Inkscape::RENDERING_INTENT_ABSOLUTE_COLORIMETRIC:
            intent = INTENT_ABSOLUTE_COLORIMETRIC;
            break;
        case Inkscape::RENDERING_INTENT_PERCEPTUAL:
        case Inkscape::RENDERING_INTENT_UNKNOWN:
        case Inkscape::RENDERING_INTENT_AUTO:
        default:
            intent = INTENT_PERCEPTUAL;
    }
    return intent;
}

Inkscape::ColorSpaceSig ColorProfile::getColorSpace() const {
    return ColorSpaceSigWrapper(impl->_profileSpace);
}

Inkscape::ColorProfileClassSig ColorProfile::getProfileClass() const {
    return ColorProfileClassSigWrapper(impl->_profileClass);
}

cmsHTRANSFORM ColorProfile::getTransfToSRGB8()
{
    if ( !impl->_transf && impl->_profHandle ) {
        int intent = getLcmsIntent(rendering_intent);
        impl->_transf = cmsCreateTransform( impl->_profHandle, ColorProfileImpl::_getInputFormat(impl->_profileSpace), ColorProfileImpl::getSRGBProfile(), TYPE_RGBA_8, intent, 0 );
    }
    return impl->_transf;
}

cmsHTRANSFORM ColorProfile::getTransfFromSRGB8()
{
    if ( !impl->_revTransf && impl->_profHandle ) {
        int intent = getLcmsIntent(rendering_intent);
        impl->_revTransf = cmsCreateTransform( ColorProfileImpl::getSRGBProfile(), TYPE_RGBA_8, impl->_profHandle, ColorProfileImpl::_getInputFormat(impl->_profileSpace), intent, 0 );
    }
    return impl->_revTransf;
}

cmsHTRANSFORM ColorProfile::getTransfGamutCheck()
{
    if ( !impl->_gamutTransf ) {
        impl->_gamutTransf = cmsCreateProofingTransform(ColorProfileImpl::getSRGBProfile(),
                                                        TYPE_BGRA_8,
                                                        ColorProfileImpl::getNULLProfile(),
                                                        TYPE_GRAY_8,
                                                        impl->_profHandle,
                                                        INTENT_RELATIVE_COLORIMETRIC,
                                                        INTENT_RELATIVE_COLORIMETRIC,
                                                        (cmsFLAGS_GAMUTCHECK | cmsFLAGS_SOFTPROOFING));
    }
    return impl->_gamutTransf;
}

// Check if a particular color is out of gamut.
bool ColorProfile::GamutCheck(SPColor color)
{
    guint32 val = color.toRGBA32(0);

    cmsUInt16Number oldAlarmCodes[cmsMAXCHANNELS] = {0};
    cmsGetAlarmCodes(oldAlarmCodes);
    cmsUInt16Number newAlarmCodes[cmsMAXCHANNELS] = {0};
    newAlarmCodes[0] = ~0;
    cmsSetAlarmCodes(newAlarmCodes);

    cmsUInt8Number outofgamut = 0;
    guchar check_color[4] = {
        static_cast<guchar>(SP_RGBA32_R_U(val)),
        static_cast<guchar>(SP_RGBA32_G_U(val)),
        static_cast<guchar>(SP_RGBA32_B_U(val)),
        255};

    cmsHTRANSFORM gamutCheck = ColorProfile::getTransfGamutCheck();
    if (gamutCheck) {
        cmsDoTransform(gamutCheck, &check_color, &outofgamut, 1);
    }

    cmsSetAlarmCodes(oldAlarmCodes);

    return (outofgamut != 0);
}

gint ColorProfile::getChannelCount() const
{
    return cmsChannelsOf(asICColorSpaceSig(getColorSpace()));
}

bool ColorProfile::isPrintColorSpace()
{
    ColorSpaceSigWrapper colorspace = getColorSpace();
    return (colorspace == cmsSigCmykData) || (colorspace == cmsSigCmyData);
}

cmsHPROFILE ColorProfile::getHandle()
{
    return impl->_profHandle;
}

void errorHandlerCB(cmsContext /*contextID*/, cmsUInt32Number errorCode, char const *errorText)
{
    g_message("lcms: Error %d", errorCode);
    g_message("                 %p", errorText);
    //g_message("lcms: Error %d; %s", errorCode, errorText);
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
