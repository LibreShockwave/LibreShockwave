package com.libreshockwave.player.cast;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.Drawing;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.player.render.output.SimpleTextRenderer;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

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
    void adjustTextHeightDoesNotShrinkBelowScriptedRect() {
        CastMember member = buildTextMember("Oops.. Cannot connect to Habbo Hotel");
        member.setProp("rect", new Datum.Rect(0, 0, 220, 120));

        assertEquals(120, member.getProp("height").toInt(),
                "boxType=adjust should expand, not shrink below the scripted rect");
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
        int count = 0;
        for (int y = 0; y < bitmap.getHeight(); y++) {
            for (int x = 0; x < bitmap.getWidth(); x++) {
                int pixel = bitmap.getPixel(x, y);
                if (((pixel >>> 24) & 0xFF) != 0 && (pixel & 0xFFFFFF) == 0xFFFFFF) {
                    count++;
                }
            }
        }
        return count;
    }
}
