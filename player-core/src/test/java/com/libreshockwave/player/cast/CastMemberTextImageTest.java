package com.libreshockwave.player.cast;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.Drawing;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.player.render.output.SimpleTextRenderer;
import com.libreshockwave.vm.opcode.dispatch.ImageMethodDispatcher;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class CastMemberTextImageTest {

    @Test
    void textMemberImageUsesMemberBackgroundColor() {
        CastMember member = buildTextMember("Habbo Club");

        Bitmap image = member.renderTextToImage();

        assertEquals(0xFF000000, image.getPixel(0, 0));
        assertEquals(0xFF000000, image.getPixel(image.getWidth() - 1, image.getHeight() - 1));
    }

    @Test
    void explicitTextRenderKeepsBackgroundSeparateFromMemberImageCache() {
        CastMember member = buildTextMember("Habbo Club");

        Bitmap memberImage = member.renderTextToImage();
        Bitmap opaqueSpriteImage = member.renderTextToImage(
                memberImage.getWidth(),
                memberImage.getHeight(),
                0xFFFFFFFF);

        assertEquals(0xFF000000, memberImage.getPixel(0, 0));
        assertEquals(0xFFFFFFFF, opaqueSpriteImage.getPixel(0, 0));
    }

    @Test
    void matteCompositePreservesWhiteTextFromBlackBackedMemberImage() {
        CastMember member = buildTextMember("Habbo Club\rqg");

        Bitmap textImage = member.renderTextToImage();
        Bitmap dest = new Bitmap(textImage.getWidth(), textImage.getHeight(), 32);
        dest.fill(0xFF000000);

        Drawing.copyPixels(dest, textImage, 0, 0, 0, 0,
                textImage.getWidth(), textImage.getHeight(),
                Palette.InkMode.MATTE, 255);

        assertEquals(countWhitePixels(textImage), countWhitePixels(dest),
                "MATTE copy should preserve all rendered white text pixels");
    }

    @Test
    void compactVFontWhiteTextRendersInsideTenPixelHighBottomBarFields() {
        CastMember.setTextRenderer(new SimpleTextRenderer());
        CastMember member = new CastMember(1, 1, MemberType.TEXT);
        member.setProp("font", Datum.of("V"));
        member.setProp("fontsize", Datum.of(9));
        member.setProp("fixedlinespace", Datum.of(11));
        member.setProp("rect", new Datum.Rect(0, 0, 90, 10));
        member.setProp("color", new Datum.Color(255, 255, 255));
        member.setProp("bgcolor", new Datum.Color(0, 0, 0));
        member.setProp("text", Datum.of("Habbo Club"));

        Bitmap textImage = member.renderTextToImage();

        assertTrue(countWhitePixels(textImage) > 0,
                "Bottom bar fields are only 10px high, so V/9 text must not be clipped away");
    }

    @Test
    void iconButtonTextMeasurementKeepsFullGlyphHeight() {
        CastMember.setTextRenderer(new SimpleTextRenderer());
        CastMember member = new CastMember(1, 1, MemberType.TEXT);
        member.setProp("wordwrap", Datum.of(0));
        member.setProp("font", Datum.of("vb"));
        member.setProp("fontstyle", new Datum.List(java.util.List.of(Datum.symbol("plain"))));
        member.setProp("fontsize", Datum.of(9));
        member.setProp("color", new Datum.Color(0, 0, 0));
        member.setProp("bgcolor", new Datum.Color(255, 255, 255));
        member.setProp("boxtype", Datum.symbol("adjust"));
        member.setProp("fixedlinespace", Datum.of(11));
        member.setProp("text", Datum.of("Pick up"));

        member.setProp("rect", new Datum.Rect(0, 0, 85, 30));
        Bitmap measurement = new Bitmap(85, 30, 32);
        measurement.fill(0xFFFFFFFF);
        Drawing.copyPixels(measurement, member.renderTextToImage(), 0, 0, 0, 0,
                85, 30, Palette.InkMode.COPY, 255);
        int textWidth = measurement.getRegion(0, 0, 85, 30).trimWhiteSpace()[2];

        member.setProp("rect", new Datum.Rect(0, 0, textWidth, member.getProp("height").toInt()));
        Bitmap textImage = member.renderTextToImage();

        assertTrue(textImage.getHeight() > 1,
                "Icon button labels must render taller than a one-pixel baseline");
        assertTrue(countPixels(textImage, 0xFF000000) > 20,
                "Expected visible black glyph pixels for the Pick up label");
    }

    @Test
    void iconButtonTextCopyIntoPalettedImageKeepsGlyphPixels() {
        CastMember.setTextRenderer(new SimpleTextRenderer());
        CastMember member = new CastMember(1, 1, MemberType.TEXT);
        member.setProp("wordwrap", Datum.of(0));
        member.setProp("font", Datum.of("vb"));
        member.setProp("fontstyle", new Datum.List(java.util.List.of(Datum.symbol("plain"))));
        member.setProp("fontsize", Datum.of(9));
        member.setProp("color", new Datum.Color(0, 0, 0));
        member.setProp("bgcolor", new Datum.Color(255, 255, 255));
        member.setProp("boxtype", Datum.symbol("adjust"));
        member.setProp("fixedlinespace", Datum.of(11));
        member.setProp("text", Datum.of("Pick up"));

        member.setProp("rect", new Datum.Rect(0, 0, 85, 30));
        Bitmap measurement = new Bitmap(85, 30, 32);
        measurement.fill(0xFFFFFFFF);
        Drawing.copyPixels(measurement, member.renderTextToImage(), 0, 0, 0, 0,
                85, 30, Palette.InkMode.COPY, 255);
        int textWidth = measurement.getRegion(0, 0, 85, 30).trimWhiteSpace()[2];
        member.setProp("rect", new Datum.Rect(0, 0, textWidth, member.getProp("height").toInt()));
        Bitmap textImage = ((Datum.ImageRef) member.getProp("image")).bitmap();

        Bitmap button = new Bitmap(69, 17, 8);
        button.setImagePalette(Palette.SYSTEM_MAC_PALETTE);
        button.fill(0xFFEFEFEF);
        ImageMethodDispatcher.dispatch(new Datum.ImageRef(button), "copyPixels",
                List.of(new Datum.ImageRef(textImage),
                        new Datum.Rect(24, 3, 24 + textImage.getWidth(), 3 + textImage.getHeight()),
                        new Datum.Rect(0, 0, textImage.getWidth(), textImage.getHeight())));

        assertTrue(countPixels(button, 0xFF000000) > 20,
                "Default icon-button text copy into an 8-bit button image must preserve black glyphs");
        assertTrue(countNonWhitePixels(button) > countPixels(textImage, 0xFF000000),
                "Paletted button should retain both existing shell pixels and copied text pixels");
    }

    @Test
    void adjustTextHeightDoesNotShrinkBelowScriptedRect() {
        CastMember member = buildTextMember("Oops.. Cannot connect to Habbo Hotel");
        member.setProp("rect", new Datum.Rect(0, 0, 220, 120));

        assertEquals(120, member.getProp("height").toInt(),
                "boxType=adjust should expand, not shrink below the scripted rect");
    }

    @Test
    void runtimeWriterMeasurementRectReportsRenderedHeight() {
        CastMember member = buildTextMember("These are hotel's public rooms. What are you waiting for? Go and meet other Habbos!");
        member.setName("writer_public_info");
        member.setProp("wordwrap", Datum.of(1));
        member.setProp("rect", new Datum.Rect(0, 0, 480, 480));

        Bitmap rendered = member.renderTextToImage();

        assertEquals(rendered.getHeight(), member.getProp("height").toInt(),
                "Temporary writer measurement rects should not make fake-alpha text images 480px tall");
    }

    @Test
    void rightAlignedCourierLinkStyleRendersVisibleTealUnderline() {
        CastMember.setTextRenderer(new SimpleTextRenderer());
        CastMember member = new CastMember(1, 1, MemberType.TEXT);
        member.setProp("font", Datum.of("Courier"));
        member.setProp("fontsize", Datum.of(9));
        member.setProp("fontstyle", Datum.of("underline"));
        member.setProp("alignment", Datum.symbol("right"));
        member.setProp("fixedlinespace", Datum.of(9));
        member.setProp("rect", new Datum.Rect(0, 0, 165, 14));
        member.setProp("color", new Datum.Color(0x7B, 0x94, 0x98));
        member.setProp("bgcolor", new Datum.Color(0xFF, 0xFF, 0xFF));
        member.setProp("text", Datum.of("Hide Full Rooms"));

        Bitmap textImage = member.renderTextToImage();
        int teal = 0xFF7B9498;

        int firstTealColumn = findFirstColorColumn(textImage, teal);
        int lastTealRow = findLastColorRow(textImage, teal);

        assertTrue(countPixels(textImage, teal) > 0,
                "expected rendered teal link pixels for Hide Full Rooms");
        assertTrue(firstTealColumn > 0,
                "expected right-aligned link ink to leave some leading whitespace");
        assertTrue(findLastColorColumn(textImage, teal) >= textImage.getWidth() - 6,
                "expected right-aligned link ink to reach the right edge of the field");
        assertTrue(lastTealRow >= 0 && countColorPixelsOnRow(textImage, lastTealRow, teal) >= 20,
                "expected a visible underline row in the teal link image");
    }

    @Test
    void textMemberImageUsesMemberTextAndBackgroundColors() {
        CastMember.setTextRenderer(new SimpleTextRenderer());
        CastMember member = new CastMember(1, 1, MemberType.TEXT);
        member.setProp("font", Datum.of("Courier"));
        member.setProp("fontsize", Datum.of(9));
        member.setProp("fontstyle", Datum.of("underline"));
        member.setProp("alignment", Datum.symbol("right"));
        member.setProp("fixedlinespace", Datum.of(9));
        member.setProp("rect", new Datum.Rect(0, 0, 165, 14));
        member.setProp("color", new Datum.Color(0x7B, 0x94, 0x98));
        member.setProp("bgcolor", new Datum.Color(0x00, 0x00, 0x00));
        member.setProp("text", Datum.of("Hide Full Rooms"));

        Datum imageDatum = member.getProp("image");
        Bitmap image = ((Datum.ImageRef) imageDatum).bitmap();

        assertTrue(countPixels(image, 0xFF7B9498) > 0,
                "text member .image should preserve the member display text color");
        assertTrue(countPixels(image, 0xFF000000) > 0,
                "text member .image should preserve the member background color");
        assertEquals(0, countPixels(image, 0xFFFFFFFF),
                "text member .image should not force a white mask background");
    }

    @Test
    void writerStyleMaskedTintedTextPipelineProducesVisibleTealLinkPixels() {
        CastMember.setTextRenderer(new SimpleTextRenderer());
        CastMember member = new CastMember(1, 1, MemberType.TEXT);
        member.setProp("font", Datum.of("Courier"));
        member.setProp("fontsize", Datum.of(9));
        member.setProp("fontstyle", Datum.of("underline"));
        member.setProp("alignment", Datum.symbol("right"));
        member.setProp("fixedlinespace", Datum.of(9));
        member.setProp("rect", new Datum.Rect(0, 0, 165, 14));
        member.setProp("color", new Datum.Color(0x7B, 0x94, 0x98));
        member.setProp("text", Datum.of("Hide Full Rooms"));

        Bitmap textMaskSource = ((Datum.ImageRef) member.getProp("image")).bitmap();
        Bitmap mask = new Bitmap(textMaskSource.getWidth(), textMaskSource.getHeight(), 8);
        mask.fill(0xFFFFFFFF);
        Drawing.copyPixels(mask, textMaskSource, 0, 0, 0, 0,
                textMaskSource.getWidth(), textMaskSource.getHeight(),
                Palette.InkMode.MATTE, 255);

        Bitmap tinted = new Bitmap(textMaskSource.getWidth(), textMaskSource.getHeight(), 32);
        tinted.fill(0xFF7B9498);
        Bitmap out = new Bitmap(textMaskSource.getWidth(), textMaskSource.getHeight(), 32);
        out.fill(0xFFFFFFFF);

        Datum.PropList props = new Datum.PropList();
        props.add("maskImage", new Datum.ImageRef(mask), true);
        ImageMethodDispatcher.dispatch(new Datum.ImageRef(out), "copyPixels",
                java.util.List.of(
                        new Datum.ImageRef(tinted),
                        new Datum.Rect(0, 0, out.getWidth(), out.getHeight()),
                        new Datum.Rect(0, 0, tinted.getWidth(), tinted.getHeight()),
                        props));

        assertTrue(countPixels(out, 0xFF7B9498) > 0,
                "writer-style tinted mask copy should leave visible teal link pixels");
    }

    @Test
    void fillWithSymbolicColorPropListAppliesRequestedTeal() {
        Bitmap target = new Bitmap(4, 3, 32);

        Datum.PropList props = new Datum.PropList();
        props.add("color", new Datum.Color(0x7B, 0x94, 0x98), true);
        props.add("shape", Datum.symbol("rect"), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(target), "fill",
                java.util.List.of(new Datum.Rect(0, 0, 4, 3), props));

        assertEquals(12, countPixels(target, 0xFF7B9498),
                "fill(rect, [#color: teal]) should tint the entire target image");
    }

    @Test
    void navigatorHideLinkPipelineSurvivesMaskingAndNegativeOffset() {
        CastMember.setTextRenderer(new SimpleTextRenderer());
        CastMember member = new CastMember(1, 1, MemberType.TEXT);
        member.setProp("font", Datum.of("Courier"));
        member.setProp("fontsize", Datum.of(9));
        member.setProp("fontstyle", Datum.of("underline"));
        member.setProp("alignment", Datum.symbol("right"));
        member.setProp("fixedlinespace", Datum.of(9));
        member.setProp("rect", new Datum.Rect(0, 0, 165, 14));
        member.setProp("color", new Datum.Color(0x7B, 0x94, 0x98));
        member.setProp("text", Datum.of("Hide Full Rooms"));

        Bitmap textMaskSource = ((Datum.ImageRef) member.getProp("image")).bitmap();
        Bitmap mask = new Bitmap(textMaskSource.getWidth(), textMaskSource.getHeight(), 8);
        mask.fill(0xFFFFFFFF);
        Drawing.copyPixels(mask, textMaskSource, 0, 0, 0, 0,
                textMaskSource.getWidth(), textMaskSource.getHeight(),
                Palette.InkMode.MATTE, 255);

        Bitmap tinted = new Bitmap(textMaskSource.getWidth(), textMaskSource.getHeight(), 32);
        Datum.PropList fillProps = new Datum.PropList();
        fillProps.add("color", new Datum.Color(0x7B, 0x94, 0x98), true);
        fillProps.add("shape", Datum.symbol("rect"), true);
        ImageMethodDispatcher.dispatch(new Datum.ImageRef(tinted), "fill",
                java.util.List.of(new Datum.Rect(0, 0, tinted.getWidth(), tinted.getHeight()), fillProps));

        Bitmap wrapperImage = new Bitmap(165, 14, 32);
        wrapperImage.fill(0xFFFFFFFF);
        Datum.PropList maskProps = new Datum.PropList();
        maskProps.add("maskImage", new Datum.ImageRef(mask), true);
        ImageMethodDispatcher.dispatch(new Datum.ImageRef(wrapperImage), "copyPixels",
                java.util.List.of(
                        new Datum.ImageRef(tinted),
                        new Datum.Rect(0, 0, wrapperImage.getWidth(), wrapperImage.getHeight()),
                        new Datum.Rect(0, 0, wrapperImage.getWidth(), wrapperImage.getHeight()),
                        maskProps));

        assertTrue(countPixels(wrapperImage, 0xFF7B9498) > 0,
                "maskImage copy should create teal link pixels inside the wider wrapper image");

        Bitmap stageBuffer = new Bitmap(165, 14, 32);
        stageBuffer.fill(0xFFD4DDE1);
        Datum.PropList wrapperProps = new Datum.PropList();
        wrapperProps.add("ink", Datum.of(36), true);
        ImageMethodDispatcher.dispatch(new Datum.ImageRef(stageBuffer), "copyPixels",
                java.util.List.of(
                        new Datum.ImageRef(wrapperImage),
                        new Datum.Rect(0, 0, stageBuffer.getWidth(), stageBuffer.getHeight()),
                        new Datum.Rect(-67, 0, 98, 14),
                        wrapperProps));

        assertTrue(countPixels(stageBuffer, 0xFF7B9498) > 0,
                "negative source offsets with ink 36 should keep the teal link visible");
        assertTrue(findLastColorColumn(stageBuffer, 0xFF7B9498) >= stageBuffer.getWidth() - 6,
                "the hide link should remain right-aligned after the wrapper offset is applied");
    }

    @Test
    void adjustTextImageCanGrowWiderThanRectForUnwrappedCourierLinkText() {
        CastMember.setTextRenderer(new SimpleTextRenderer());
        CastMember member = new CastMember(1, 1, MemberType.TEXT);
        member.setProp("font", Datum.of("Courier"));
        member.setProp("fontsize", Datum.of(9));
        member.setProp("fontstyle", Datum.of("underline"));
        member.setProp("alignment", Datum.symbol("right"));
        member.setProp("fixedlinespace", Datum.of(9));
        member.setProp("rect", new Datum.Rect(0, 0, 60, 14));
        member.setProp("color", new Datum.Color(0x7B, 0x94, 0x98));
        member.setProp("bgcolor", new Datum.Color(0xFF, 0xFF, 0xFF));
        member.setProp("text", Datum.of("Hide Full Rooms"));

        Bitmap textImage = member.renderTextToImage();

        assertTrue(textImage.getWidth() > 60,
                "adjust-to-fit link text images should grow wide enough for the actual glyph metrics");
    }

    private static CastMember buildTextMember(String text) {
        CastMember.setTextRenderer(new SimpleTextRenderer());
        CastMember member = new CastMember(1, 1, MemberType.TEXT);
        member.setProp("font", Datum.of("Verdana"));
        member.setProp("fontsize", Datum.of(9));
        member.setProp("fixedlinespace", Datum.of(9));
        member.setProp("rect", new Datum.Rect(0, 0, 120, 18));
        member.setProp("color", new Datum.Color(255, 255, 255));
        member.setProp("bgcolor", new Datum.Color(0, 0, 0));
        member.setProp("text", Datum.of(text));
        return member;
    }

    private static int countWhitePixels(Bitmap bitmap) {
        return countPixels(bitmap, 0xFFFFFFFF);
    }

    private static int countPixels(Bitmap bitmap, int argb) {
        int count = 0;
        for (int y = 0; y < bitmap.getHeight(); y++) {
            for (int x = 0; x < bitmap.getWidth(); x++) {
                int pixel = bitmap.getPixel(x, y);
                if (((pixel >>> 24) & 0xFF) != 0 && pixel == argb) {
                    count++;
                }
            }
        }
        return count;
    }

    private static int countNonWhitePixels(Bitmap bitmap) {
        int count = 0;
        for (int y = 0; y < bitmap.getHeight(); y++) {
            for (int x = 0; x < bitmap.getWidth(); x++) {
                int pixel = bitmap.getPixel(x, y);
                if (((pixel >>> 24) & 0xFF) != 0 && (pixel & 0xFFFFFF) != 0xFFFFFF) {
                    count++;
                }
            }
        }
        return count;
    }

    private static int findFirstColorColumn(Bitmap bitmap, int argb) {
        for (int x = 0; x < bitmap.getWidth(); x++) {
            for (int y = 0; y < bitmap.getHeight(); y++) {
                if (bitmap.getPixel(x, y) == argb) {
                    return x;
                }
            }
        }
        return -1;
    }

    private static int findLastColorColumn(Bitmap bitmap, int argb) {
        for (int x = bitmap.getWidth() - 1; x >= 0; x--) {
            for (int y = 0; y < bitmap.getHeight(); y++) {
                if (bitmap.getPixel(x, y) == argb) {
                    return x;
                }
            }
        }
        return -1;
    }

    private static int findLastColorRow(Bitmap bitmap, int argb) {
        for (int y = bitmap.getHeight() - 1; y >= 0; y--) {
            if (countColorPixelsOnRow(bitmap, y, argb) > 0) {
                return y;
            }
        }
        return -1;
    }

    private static int countColorPixelsOnRow(Bitmap bitmap, int y, int argb) {
        int count = 0;
        for (int x = 0; x < bitmap.getWidth(); x++) {
            if (bitmap.getPixel(x, y) == argb) {
                count++;
            }
        }
        return count;
    }
}
