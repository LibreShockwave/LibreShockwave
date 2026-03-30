package com.libreshockwave.player.render.pipeline;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.id.ChunkId;
import com.libreshockwave.id.InkMode;
import com.libreshockwave.player.cast.CastMember;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.opcode.dispatch.ImageMethodDispatcher;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;

class BitmapCacheTest {

    private static final byte[] EXIT_SHAPE_SPECIFIC_DATA = new byte[] {
            0x00, 0x01,
            0x00, 0x00,
            0x00, 0x00,
            0x00, 0x39,
            0x00, 0x39,
            0x00, 0x01,
            (byte) 0xF9,
            0x00,
            0x00,
            0x01,
            0x05
    };

    @Test
    void indexedMatteRemapSkipsDefaultBlackToWhiteRamp() {
        Bitmap raw = new Bitmap(3, 1, 8, new int[] {
                0xFFFFFFFF,
                0xFF7B5005,
                0xFF000000
        });
        raw.setPaletteIndices(new byte[] {0, (byte) 128, (byte) 255});

        BitmapCache.IndexedMatteColorRemap remap = BitmapCache.resolveIndexedMatteColorRemap(
                raw, InkMode.MATTE.code(), 0x000000, 0xFFFFFF, true, true, null);

        assertNull(remap);
    }

    @Test
    void indexedMatteRemapUsesDirectForeColorAndResolvedBackColor() {
        Bitmap raw = new Bitmap(3, 1, 8, new int[] {
                0xFFFFFFFF,
                0xFF7B5005,
                0xFF000000
        });
        raw.setPaletteIndices(new byte[] {0, (byte) 128, (byte) 255});
        Palette palette = new Palette(new int[] {
                0xFFFFFFFF,
                0xFF33CC66
        }, "test-remap");

        BitmapCache.IndexedMatteColorRemap remap = BitmapCache.resolveIndexedMatteColorRemap(
                raw, InkMode.MATTE.code(), 0x000000, 1, true, true, palette);

        assertNotNull(remap);
        assertEquals(0x000000, remap.foreColor());
        assertEquals(0x33CC66, remap.backColor());
    }

    @Test
    void quadCopiedPalettedWrapperKeepsIndicesAndDynamicMatteRemap() {
        Palette sourcePalette = new Palette(new int[] {0xFFFFFF, 0xFF808080, 0xFF000000}, "furni-ramp");
        Bitmap src = new Bitmap(2, 3, 8, new int[] {
                0xFFFFFFFF, 0xFF000000,
                0xFF808080, 0xFF000000,
                0xFFFFFFFF, 0xFF808080
        });
        src.setImagePalette(sourcePalette);
        src.setPaletteRefCastMember(4, 12);
        src.setPaletteIndices(new byte[] {
                0, (byte) 255,
                (byte) 128, (byte) 255,
                0, (byte) 128
        });

        Bitmap dest = new Bitmap(3, 2, 32);
        Datum.List quad = new Datum.List(new ArrayList<>(List.of(
                new Datum.Point(3, 0),
                new Datum.Point(3, 2),
                new Datum.Point(0, 2),
                new Datum.Point(0, 0)
        )));

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), quad, new Datum.Rect(0, 0, 2, 3)));

        assertSame(sourcePalette, dest.getImagePalette());
        assertEquals(4, dest.getPaletteRefCastLib());
        assertEquals(12, dest.getPaletteRefMemberNum());
        assertArrayEquals(new byte[] {
                0, (byte) 128, 0,
                (byte) 128, (byte) 255, (byte) 255
        }, dest.getPaletteIndices());

        CastMember member = new CastMember(1, 10005, MemberType.BITMAP);
        member.setBitmapDirectly(dest);

        RenderSprite sprite = new RenderSprite(
                1, 0, 0, 3, 2, 0, true,
                RenderSprite.SpriteType.BITMAP,
                null, member,
                0x000000, 0x33CC66, true, true,
                8, 100, false, false, null, false
        );

        SpriteBaker baker = new SpriteBaker(new BitmapCache(), null, null);
        RenderSprite baked = baker.bake(sprite);

        assertNotNull(baked.getBakedBitmap());
        assertEquals(0x00000000, baked.getBakedBitmap().getPixel(0, 0));
        assertEquals(0xFF196633, baked.getBakedBitmap().getPixel(1, 0));
        assertEquals(0x00000000, baked.getBakedBitmap().getPixel(2, 0));
        assertEquals(0xFF196633, baked.getBakedBitmap().getPixel(0, 1));
        assertEquals(0xFF000000, baked.getBakedBitmap().getPixel(1, 1));
        assertEquals(0xFF000000, baked.getBakedBitmap().getPixel(2, 1));
    }

    @Test
    void authoredOutlineShapeWithThicknessOneBakesTransparent() {
        CastMemberChunk exitShape = new CastMemberChunk(
                null,
                new ChunkId(1),
                MemberType.SHAPE,
                0,
                EXIT_SHAPE_SPECIFIC_DATA.length,
                new byte[0],
                EXIT_SHAPE_SPECIFIC_DATA,
                "exitshape",
                0,
                0,
                0
        );

        RenderSprite sprite = new RenderSprite(
                1, 0, 0, 57, 57, 0, true,
                RenderSprite.SpriteType.SHAPE,
                exitShape, null,
                0x888888, 0xFFFFFF, true, true,
                0, 100, false, false, null, false
        );

        SpriteBaker baker = new SpriteBaker(new BitmapCache(), null, null);
        RenderSprite baked = baker.bake(sprite);

        assertNotNull(baked.getBakedBitmap());
        assertEquals(0x00000000, baked.getBakedBitmap().getPixel(0, 0));
        assertEquals(0x00000000, baked.getBakedBitmap().getPixel(28, 28));
        assertEquals(0x00000000, baked.getBakedBitmap().getPixel(56, 56));
    }

    @Test
    void syntheticShapeWithoutMemberStillBakesSolidFill() {
        RenderSprite sprite = new RenderSprite(
                1, 0, 0, 4, 3, 0, true,
                RenderSprite.SpriteType.SHAPE,
                null, null,
                0x336699, 0xFFFFFF, true, true,
                0, 100, false, false, null, false
        );

        SpriteBaker baker = new SpriteBaker(new BitmapCache(), null, null);
        RenderSprite baked = baker.bake(sprite);

        assertNotNull(baked.getBakedBitmap());
        assertEquals(0xFF336699, baked.getBakedBitmap().getPixel(0, 0));
        assertEquals(0xFF336699, baked.getBakedBitmap().getPixel(2, 1));
        assertEquals(0xFF336699, baked.getBakedBitmap().getPixel(3, 2));
    }
}
