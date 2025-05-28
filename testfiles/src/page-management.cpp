// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test the multi-page functionality of Inkscape
 *
 * Authors:
 *   Martin Owens
 *
 * Copyright (C) 2025 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <gtest/gtest.h>

#include <src/document.h>
#include <src/inkscape.h>
#include <src/object/sp-page.h>
#include <src/object/sp-rect.h>
#include <src/page-manager.h>

using namespace Inkscape;
using namespace Inkscape::XML;
using namespace std::literals;

class MultiPageTest : public ::testing::Test {
public:
    static void SetUpTestCase() {
        Inkscape::Application::create(false);
    }

    void SetUp() override {
        constexpr auto docString = R"A(<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg width="100mm" height="100mm" viewBox="0 0 100 100" version="1.1" id="svg1" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd" xmlns="http://www.w3.org/2000/svg">
  <sodipodi:namedview id="nv1">
    <inkscape:page x="0" y="0" width="100" height="100" id="page1"/>
    <inkscape:page x="-100" y="200" width="10" height="190" id="page2"/>
  </sodipodi:namedview>
  <g inkscape:groupmode="layer" id="layer1" transform="translate(100, 100)">
    <rect id="rect1" x="-100" y="-100" width="50" height="50" fill="red"/>
    <rect id="rect2" x="-200" y="145" width="5" height="95" fill="green"/>
  </g>
</svg>)A"sv;
        doc = SPDocument::createNewDocFromMem(docString, false);

        ASSERT_TRUE(doc);
        ASSERT_TRUE(doc->getRoot());
    }

    std::unique_ptr<SPDocument> doc;
};

::testing::AssertionResult RectNear(std::string expr1,
                                    std::string expr2,
                                    Geom::Rect val1,
                                    Geom::Rect val2,
                                    double abs_error = 0.01) {

  double diff = 0;
  for (auto x = 0; x < 2; x++) {
      for (auto y = 0; y < 2; y++) {
          diff += fabs(val1[x][y] - val2[x][y]);
      }
  }

  if (diff <= abs_error)
      return ::testing::AssertionSuccess();

  return ::testing::AssertionFailure()
      << "The difference between " << expr1 << " and " << expr2
      << " is " << diff << ", which exceeds " << abs_error << ", where\n"
      << expr1 << " evaluates to " << val1 << ",\n"
      << expr2 << " evaluates to " << val2 << ".\n";
}

TEST_F(MultiPageTest, swapPages)
{
    doc->ensureUpToDate();

    auto &pm = doc->getPageManager();
    auto page1 = pm.getPage(0);
    auto page2 = pm.getPage(1);
    auto rect1 = cast<SPRect>(doc->getObjectById("rect1"));
    auto rect2 = cast<SPRect>(doc->getObjectById("rect2"));

    EXPECT_TRUE(RectNear(page1->getId(), "initial", page1->getRect(), {0, 0, 100, 100}));
    EXPECT_TRUE(RectNear(page2->getId(), "initial", page2->getRect(), {-100, 200, -90, 390}));
    EXPECT_TRUE(RectNear(rect1->getId(), "initial", *rect1->geometricBounds(), {-100, -100, -50, -50}));
    EXPECT_TRUE(RectNear(rect2->getId(), "initial", *rect2->geometricBounds(), {-200, 145, -195, 240}));
    EXPECT_TRUE(page1->itemOnPage(rect1));
    EXPECT_TRUE(page2->itemOnPage(rect2));
    EXPECT_TRUE(page1->isViewportPage());
    EXPECT_FALSE(page2->isViewportPage());

    page1->swapPage(page2, true);
    // This causes the viewport page to be resized if it's incorrectly positioned.
    doc->ensureUpToDate();

    EXPECT_TRUE(RectNear(page1->getId(), "swap1", page1->getRect(), {-100, 200, 0, 300}));
    EXPECT_TRUE(RectNear(page2->getId(), "swap1", page2->getRect(), {0, 0, 10, 190}));
    EXPECT_TRUE(RectNear(rect1->getId(), "swap1", *rect1->geometricBounds(), {-200, 100, -150, 150}));
    EXPECT_TRUE(RectNear(rect2->getId(), "swap1", *rect2->geometricBounds(), {-100, -55, -95, 40}));
    EXPECT_FALSE(page1->isViewportPage());
    EXPECT_TRUE(page2->isViewportPage());
    
    page1->swapPage(page2, true);
    doc->ensureUpToDate();

    EXPECT_TRUE(RectNear(page1->getId(), "swap2", page1->getRect(), {0, 0, 100, 100}));
    EXPECT_TRUE(RectNear(page2->getId(), "swap2", page2->getRect(), {-100, 200, -90, 390}));
    EXPECT_TRUE(RectNear(rect1->getId(), "swap2", *rect1->geometricBounds(), {-100, -100, -50, -50}));
    EXPECT_TRUE(RectNear(rect2->getId(), "swap2", *rect2->geometricBounds(), {-200, 145, -195, 240}));
    EXPECT_TRUE(page1->isViewportPage());
    EXPECT_FALSE(page2->isViewportPage());
}
