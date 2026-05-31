package com.libreshockwave.player.render.pipeline;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.player.cast.CastMember;
import com.libreshockwave.player.render.output.SimpleTextRenderer;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class SpriteBakerTextTest {

    @Test
    void dynamicTextUsesEffectiveSpriteBackColor() {
        CastMember.setTextRenderer(new SimpleTextRenderer());
        CastMember member = new CastMember(1, 1, MemberType.TEXT);
        member.setProp("text", Datum.of("Title"));
        member.setProp("bgColor", Datum.of(0xEFEFEF));

        RenderSprite sprite = new RenderSprite(
                1, 0, 0, 64, 16, 0, true,
                RenderSprite.SpriteType.TEXT,
                null, member,
                0xEEEEEE, 0x6794A7,
                true, false,
                0, 100,
                false, false,
                null, false);

        Bitmap baked = new SpriteBaker(new BitmapCache(), null, null)
                .bake(sprite)
                .getBakedBitmap();

        assertEquals(0xFF6794A7, baked.getPixel(0, 0));
    }

    @Test
    void shiftBitmapDownPreservesSizeAndClearsLeadingRows() {
        Bitmap source = new Bitmap(3, 4, 32, new int[] {
                0x00000000, 0x00000000, 0x00000000,
                0xFF000004, 0xFF000005, 0xFF000006,
                0xFF000007, 0xFF000008, 0xFF000009,
                0x00000000, 0x00000000, 0x00000000
        });
        source.setNativeAlpha(true);

        Bitmap shifted = SpriteBaker.shiftBitmapDown(source, 2, 0x00000000);

        assertEquals(3, shifted.getWidth());
        assertEquals(4, shifted.getHeight());
        assertEquals(0x00000000, shifted.getPixel(0, 0));
        assertEquals(0x00000000, shifted.getPixel(2, 1));
        assertEquals(0xFF000004, shifted.getPixel(0, 2));
        assertEquals(0xFF000009, shifted.getPixel(2, 3));
        assertTrue(shifted.isNativeAlpha());
    }

    @Test
    void shiftBitmapDownDoesNotClipBottomInkWhenThereIsNoRoom() {
        Bitmap source = new Bitmap(3, 3, 32, new int[] {
                0x00000000, 0x00000000, 0x00000000,
                0x00000000, 0xFF000001, 0x00000000,
                0xFF000002, 0xFF000003, 0xFF000004
        });

        Bitmap shifted = SpriteBaker.shiftBitmapDown(source, 2, 0x00000000);

        assertEquals(source.getPixel(0, 2), shifted.getPixel(0, 2));
        assertEquals(source.getPixel(1, 2), shifted.getPixel(1, 2));
        assertEquals(source.getPixel(2, 2), shifted.getPixel(2, 2));
    }

}
