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
    void textMemberImageUsesTransparentBackgroundEvenWhenBgColorIsDark() {
        CastMember member = buildTextMember("Habbo Club");

        Bitmap image = member.renderTextToImage();

        assertEquals(0, (image.getPixel(0, 0) >>> 24) & 0xFF);
        assertEquals(0, (image.getPixel(image.getWidth() - 1, image.getHeight() - 1) >>> 24) & 0xFF);
        assertTrue(image.hasNativeMatteAlpha(), "text member image should expose alpha-backed transparency");
    }

    @Test
    void explicitTextRenderKeepsOpaqueBackgroundSeparateFromMemberImageCache() {
        CastMember member = buildTextMember("Habbo Club");

        Bitmap transparentMemberImage = member.renderTextToImage();
        Bitmap opaqueSpriteImage = member.renderTextToImage(
                transparentMemberImage.getWidth(),
                transparentMemberImage.getHeight(),
                0xFF000000);

        assertEquals(0, (transparentMemberImage.getPixel(0, 0) >>> 24) & 0xFF);
        assertEquals(0xFF000000, opaqueSpriteImage.getPixel(0, 0));
    }

    @Test
    void matteCompositePreservesWhiteTextFromTransparentMemberImage() {
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
