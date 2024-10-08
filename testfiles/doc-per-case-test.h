// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test fixture with SPDocument per entire test case.
 *
 * Author:
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 2015 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "gtest/gtest.h"

#include "document.h"

/**
 * Simple fixture that creates a single SPDocument to be shared between all tests
 * in this test case.
 */
class DocPerCaseTest : public ::testing::Test
{
protected:
    static void SetUpTestCase();
    static void TearDownTestCase();

    static std::unique_ptr<SPDocument> _doc;
};

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
