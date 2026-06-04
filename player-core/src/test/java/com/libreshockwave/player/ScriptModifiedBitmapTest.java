package com.libreshockwave.player;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.Drawing;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.id.ChunkId;
import com.libreshockwave.player.cast.CastMember;
import com.libreshockwave.player.render.pipeline.BitmapCache;
import com.libreshockwave.player.render.pipeline.InkProcessor;
import com.libreshockwave.player.render.pipeline.RenderSprite;
import com.libreshockwave.player.render.pipeline.SpriteBaker;
import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.opcode.dispatch.ImageMethodDispatcher;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Tests that Lingo image modifications (fill, copyPixels) are visible
 * to the renderer via SpriteBaker.
 */
public class ScriptModifiedBitmapTest {

    @Test
    void textMemberExposesDirectorLineChunks() {
        CastMember member = new CastMember(1, 10001, MemberType.TEXT);
        member.setDynamicText("[#id:\"window_70s_wide\"]\r[#id:\"window_romantic_wide\"]");

        assertEquals(2, member.getProp("lineCount").toInt());

        Datum.List lines = assertInstanceOf(Datum.List.class, member.getProp("line"));
        assertEquals("[#id:\"window_70s_wide\"]", lines.items().get(0).toStr());
        assertEquals("[#id:\"window_romantic_wide\"]", lines.items().get(1).toStr());
        assertEquals("[#id:\"window_70s_wide\"]",
                member.callMethod("getProp", List.of(Datum.symbol("line"), Datum.of(1))).toStr());
        assertEquals("[#id:\"window_romantic_wide\"]",
                member.callMethod("getProp", List.of(Datum.symbol("line"), Datum.of(2))).toStr());
    }

    @Test
    void bitmapMemberWithUnavailableMediaStillExposesImageDimensions() {
        byte[] bitmapInfo = new byte[] {
                (byte) 0x80, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F,
                0x00, 0x11, 0x01, 0x00, 0x00, 0x00, (byte) 0xFF, 0x2A,
                (byte) 0xFF, 0x5D, (byte) 0xFF, (byte) 0xCB, 0x00, 0x0A,
                0x00, 0x20, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0x9B,
        };
        CastMemberChunk chunk = new CastMemberChunk(
                null,
                new ChunkId(1926),
                MemberType.BITMAP,
                0,
                bitmapInfo.length,
                new byte[0],
                bitmapInfo,
                "katalogi_ikoni.pets",
                0,
                10,
                -53);
        CastMember member = new CastMember(57, 480, chunk, null);

        assertEquals(17, member.getProp("width").toInt());
        assertEquals(15, member.getProp("height").toInt());
        Datum image = member.getProp("image");
        assertInstanceOf(Datum.ImageRef.class, image);
        assertEquals(17, ImageMethodDispatcher.getProperty((Datum.ImageRef) image, "width").toInt());
        assertEquals(15, ImageMethodDispatcher.getProperty((Datum.ImageRef) image, "height").toInt());
    }

    @Test
    void scriptModifiedBitmapIsUsedByBaker() {
        // 1. Create a dynamic CastMember with a red bitmap
        CastMember member = new CastMember(1, 42, MemberType.BITMAP);
        Bitmap original = new Bitmap(10, 10, 32);
        int red = 0xFFFF0000;
        original.fill(red);
        // Set the bitmap directly (simulates initial load, not Lingo assignment)
        member.setBitmapDirectly(original);

        // Verify the member's bitmap is red and NOT yet modified
        Bitmap memberBmp = member.getBitmap();
        assertNotNull(memberBmp);
        assertEquals(red, memberBmp.getPixel(5, 5));
        assertFalse(memberBmp.isScriptModified(), "Bitmap should not be marked modified yet");

        // 2. Get the member's image (as Lingo would: member("foo").image)
        Datum imageRef = member.getProp("image");
        assertInstanceOf(Datum.ImageRef.class, imageRef);
        Bitmap imgBitmap = ((Datum.ImageRef) imageRef).bitmap();

        // 3. Modify via ImageMethodDispatcher.fill (as Lingo would: pImg.fill(rect, blue))
        int blue = 0xFF0000FF;
        Datum.Rect fullRect = new Datum.Rect(0, 0, 10, 10);
        Datum.Color blueColor = new Datum.Color(0, 0, 255);
        ImageMethodDispatcher.dispatch(
                (Datum.ImageRef) imageRef, "fill", List.of(fullRect, blueColor));

        // 4. Verify the bitmap is now blue AND marked as script-modified
        assertEquals(blue, imgBitmap.getPixel(5, 5), "Bitmap pixels should be blue after fill");
        assertTrue(imgBitmap.isScriptModified(), "Bitmap should be marked script-modified after fill");

        // The CastMember's bitmap should be the SAME object
        assertSame(imgBitmap, member.getBitmap(),
                "member.getBitmap() must return the same Bitmap instance that was modified");

        // 5. Build a RenderSprite with this member as dynamicMember
        //    (simulates what StageRenderer does for sprites with runtime CastMembers)
        RenderSprite sprite = new RenderSprite(
                1,  // channel
                0, 0, 10, 10,  // x, y, w, h
                0,  // locZ
                true,  // visible
                RenderSprite.SpriteType.BITMAP,
                null,  // no file CastMemberChunk
                member,  // dynamicMember = our runtime CastMember
                0, 0,  // foreColor, backColor
                false, false,  // hasForeColor, hasBackColor
                0,  // ink = COPY
                255,  // blend
                false, false,  // flipH, flipV
                null,  // no baked bitmap yet
                false  // hasBehaviors
        );

        // 6. Bake through SpriteBaker
        BitmapCache cache = new BitmapCache();  // sync mode
        SpriteBaker baker = new SpriteBaker(cache, null, null);
        RenderSprite baked = baker.bake(sprite);

        // 7. Verify the baked bitmap is the modified (blue) one
        Bitmap bakedBmp = baked.getBakedBitmap();
        assertNotNull(bakedBmp, "Baked bitmap should not be null");
        assertEquals(blue, bakedBmp.getPixel(5, 5),
                "Baked bitmap should reflect the Lingo-modified pixels (blue), not original (red)");
    }

    @Test
    void scriptModifiedBitmapTakesPriorityOverCache() {
        // This tests the scenario where a file-loaded member also has a runtime CastMember
        // whose bitmap was modified by Lingo. The modified bitmap should take priority.
        CastMember member = new CastMember(1, 42, MemberType.BITMAP);
        Bitmap bmp = new Bitmap(10, 10, 32);
        bmp.fill(0xFFFF0000); // red
        member.setBitmapDirectly(bmp);

        // Modify via Lingo
        Datum imageRef = member.getProp("image");
        Datum.Rect fullRect = new Datum.Rect(0, 0, 10, 10);
        Datum.Color green = new Datum.Color(0, 255, 0);
        ImageMethodDispatcher.dispatch(
                (Datum.ImageRef) imageRef, "fill", List.of(fullRect, green));

        assertTrue(member.getBitmap().isScriptModified());

        // Build a RenderSprite that has BOTH a file CastMemberChunk AND a dynamicMember
        // In real usage, the CastMemberChunk would make BitmapCache return a cached bitmap.
        // But since we don't have a real file here, getCastMember() is null,
        // so we test that dynamicMember path works.
        RenderSprite sprite = new RenderSprite(
                1, 0, 0, 10, 10, 0, true,
                RenderSprite.SpriteType.BITMAP,
                null, member,
                0, 0, false, false,
                0, 255, false, false, null, false
        );

        BitmapCache cache = new BitmapCache();
        SpriteBaker baker = new SpriteBaker(cache, null, null);
        RenderSprite baked = baker.bake(sprite);

        Bitmap bakedBmp = baked.getBakedBitmap();
        assertNotNull(bakedBmp);
        assertEquals(0xFF00FF00, bakedBmp.getPixel(5, 5),
                "Baked bitmap should be green (the Lingo-modified version)");
    }

    @Test
    void unmodifiedBitmapNotFlaggedAsScriptModified() {
        Bitmap bmp = new Bitmap(10, 10, 32);
        bmp.fill(0xFFFF0000);
        assertFalse(bmp.isScriptModified(), "Regular Bitmap.fill should NOT set scriptModified");

        // Only ImageMethodDispatcher calls should set the flag
        Datum.ImageRef ref = new Datum.ImageRef(bmp);
        ImageMethodDispatcher.dispatch(ref, "fill",
                List.of(new Datum.Rect(0, 0, 10, 10), new Datum.Color(0, 0, 255)));
        assertTrue(bmp.isScriptModified(), "ImageMethodDispatcher.fill SHOULD set scriptModified");
    }

    @Test
    void copyPixelsSetsScriptModified() {
        Bitmap dest = new Bitmap(10, 10, 32);
        dest.fill(0xFFFFFFFF); // white
        Bitmap src = new Bitmap(5, 5, 32);
        src.fill(0xFFFF0000); // red

        Datum.ImageRef destRef = new Datum.ImageRef(dest);
        Datum.ImageRef srcRef = new Datum.ImageRef(src);
        Datum.Rect destRect = new Datum.Rect(0, 0, 5, 5);
        Datum.Rect srcRect = new Datum.Rect(0, 0, 5, 5);

        assertFalse(dest.isScriptModified());

        ImageMethodDispatcher.dispatch(destRef, "copyPixels",
                List.of(srcRef, destRect, srcRect));

        assertTrue(dest.isScriptModified(), "copyPixels should set scriptModified on dest");
        assertEquals(0xFFFF0000, dest.getPixel(2, 2), "Pixels should be copied from source");
    }

    @Test
    void fillAcceptsDirectorColorPropList() {
        Bitmap bmp = new Bitmap(3, 3, 32);
        bmp.fill(0xFF000000);

        Datum.PropList props = new Datum.PropList();
        props.add("color", Datum.of("\"#FFFFFF\""), true);
        props.add("shape", Datum.symbol("rect"), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(bmp), "fill",
                List.of(new Datum.Rect(0, 0, 3, 3), props));

        assertEquals(0xFFFFFFFF, bmp.getPixel(1, 1),
                "Director fill(rect, [#color: ...]) should use the prop-list color");
    }

    @Test
    void fillWithVoidLeavesExistingPixelsUntouched() {
        Bitmap bmp = new Bitmap(3, 3, 32);
        bmp.fill(0xFF000066);
        AtomicInteger callbackCount = new AtomicInteger();

        ImageMethodDispatcher.setImageMutationCallback(callbackCount::incrementAndGet);
        try {
            ImageMethodDispatcher.dispatch(new Datum.ImageRef(bmp), "fill",
                    List.of(new Datum.Rect(0, 0, 3, 3), Datum.VOID));
        } finally {
            ImageMethodDispatcher.setImageMutationCallback(null);
        }

        assertEquals(0xFF000066, bmp.getPixel(1, 1),
                "Director fill(rect, VOID) should not synthesize a white fill color");
        assertFalse(bmp.isScriptModified(), "fill(rect, VOID) should not mark the image as script-modified");
        assertEquals(0, callbackCount.get(), "fill(rect, VOID) should not fire image mutation callbacks");
    }

    @Test
    void copyPixelsQuadRotatesClockwiseLikeDirectorDropdown() {
        Bitmap src = new Bitmap(2, 3, 32);
        src.setPixel(0, 0, 0xFFFF0000);
        src.setPixel(1, 0, 0xFF00FF00);
        src.setPixel(0, 1, 0xFF0000FF);
        src.setPixel(1, 1, 0xFFFFFF00);
        src.setPixel(0, 2, 0xFFFF00FF);
        src.setPixel(1, 2, 0xFF00FFFF);

        Bitmap dest = new Bitmap(3, 2, 32);
        Datum.ImageRef destRef = new Datum.ImageRef(dest);
        Datum.ImageRef srcRef = new Datum.ImageRef(src);

        Datum.List quad = new Datum.List(new ArrayList<>(List.of(
                new Datum.Point(3, 0),
                new Datum.Point(3, 2),
                new Datum.Point(0, 2),
                new Datum.Point(0, 0)
        )));

        ImageMethodDispatcher.dispatch(destRef, "copyPixels",
                List.of(srcRef, quad, new Datum.Rect(0, 0, 2, 3)));

        assertEquals(0xFFFF00FF, dest.getPixel(0, 0));
        assertEquals(0xFF0000FF, dest.getPixel(1, 0));
        assertEquals(0xFFFF0000, dest.getPixel(2, 0));
        assertEquals(0xFF00FFFF, dest.getPixel(0, 1));
        assertEquals(0xFFFFFF00, dest.getPixel(1, 1));
        assertEquals(0xFF00FF00, dest.getPixel(2, 1));
    }

    @Test
    void copyPixelsQuadRotatesCounterClockwiseLikeDirectorDropdown() {
        Bitmap src = new Bitmap(2, 3, 32);
        src.setPixel(0, 0, 0xFFFF0000);
        src.setPixel(1, 0, 0xFF00FF00);
        src.setPixel(0, 1, 0xFF0000FF);
        src.setPixel(1, 1, 0xFFFFFF00);
        src.setPixel(0, 2, 0xFFFF00FF);
        src.setPixel(1, 2, 0xFF00FFFF);

        Bitmap dest = new Bitmap(3, 2, 32);
        Datum.ImageRef destRef = new Datum.ImageRef(dest);
        Datum.ImageRef srcRef = new Datum.ImageRef(src);

        Datum.List quad = new Datum.List(new ArrayList<>(List.of(
                new Datum.Point(0, 2),
                new Datum.Point(0, 0),
                new Datum.Point(3, 0),
                new Datum.Point(3, 2)
        )));

        ImageMethodDispatcher.dispatch(destRef, "copyPixels",
                List.of(srcRef, quad, new Datum.Rect(0, 0, 2, 3)));

        assertEquals(0xFF00FF00, dest.getPixel(0, 0));
        assertEquals(0xFFFFFF00, dest.getPixel(1, 0));
        assertEquals(0xFF00FFFF, dest.getPixel(2, 0));
        assertEquals(0xFFFF0000, dest.getPixel(0, 1));
        assertEquals(0xFF0000FF, dest.getPixel(1, 1));
        assertEquals(0xFFFF00FF, dest.getPixel(2, 1));
    }

    @Test
    void backgroundTransparentCopyPixelsDefaultsToWhiteKeyForPalettedSource() {
        Bitmap dest = new Bitmap(2, 1, 32);
        dest.fill(0xFFFFFFFF);

        Bitmap src = new Bitmap(2, 1, 8);
        src.setImagePalette(new Palette(new int[] {0xFF00FF, 0xC89C32}, "test-purse"));
        src.setPixel(0, 0, 0xFFFF00FF);
        src.setPixel(1, 0, 0xFFC89C32);

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(36), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 2, 1),
                        new Datum.Rect(0, 0, 2, 1), props));

        assertEquals(0xFFFF00FF, dest.getPixel(0, 0),
                "Without #bgColor, ink 36 should still default to white even for paletted sources");
        assertEquals(0xFFC89C32, dest.getPixel(1, 0));
    }

    @Test
    void backgroundTransparentCopyPixelsUsesExplicitBgColorKey() {
        Bitmap dest = new Bitmap(2, 1, 32);
        dest.fill(0xFFFFFFFF);

        Bitmap src = new Bitmap(2, 1, 8);
        src.setImagePalette(new Palette(new int[] {0xFF00FF, 0xC89C32}, "test-purse"));
        src.setPixel(0, 0, 0xFFFF00FF);
        src.setPixel(1, 0, 0xFFC89C32);

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(36), true);
        props.add("bgColor", new Datum.Color(255, 0, 255), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 2, 1),
                        new Datum.Rect(0, 0, 2, 1), props));

        assertEquals(0xFFFFFFFF, dest.getPixel(0, 0),
                "With explicit #bgColor, ink 36 should key that exact color");
        assertEquals(0xFFC89C32, dest.getPixel(1, 0));
    }

    @Test
    void backgroundTransparentCopyPixelsKeepsDirectorWhiteDefaultForGrayscaleSource() {
        Bitmap dest = new Bitmap(3, 3, 32);
        dest.fill(0xFFAF8349);

        Bitmap src = new Bitmap(3, 3, 32);
        src.fill(0xFF6E6E6E);
        src.setPixel(1, 1, 0xFF000000);

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(36), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 3, 3),
                        new Datum.Rect(0, 0, 3, 3), props));

        assertEquals(0xFF6E6E6E, dest.getPixel(0, 0),
                "Without explicit #bgColor, Director ink 36 should still key only white");
        assertEquals(0xFF000000, dest.getPixel(1, 1),
                "Foreground text pixels must still copy through");
        assertEquals(0xFF6E6E6E, dest.getPixel(2, 2));
    }

    @Test
    void backgroundTransparentCopyPixelsDoesNotInferColoredBorderAsKey() {
        Bitmap dest = new Bitmap(3, 3, 32);
        dest.fill(0xFFFFFFFF);

        Bitmap src = new Bitmap(3, 3, 32);
        src.fill(0xFF00AA44);
        src.setPixel(1, 1, 0xFFCC2200);

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(36), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 3, 3),
                        new Datum.Rect(0, 0, 3, 3), props));

        assertEquals(0xFF00AA44, dest.getPixel(0, 0),
                "Non-grayscale art should keep the historical white default key");
        assertEquals(0xFFCC2200, dest.getPixel(1, 1));
    }

    @Test
    void backgroundTransparentCopyPixelsPreservesTransparentTextBackground() {
        Bitmap dest = new Bitmap(10, 10, 32);
        dest.fill(0xFFC0C0C0);

        Bitmap src = new Bitmap(3, 3, 32);
        src.fill(0x00FFFFFF);
        src.setPixel(1, 1, 0xFF000000);

        Datum.ImageRef destRef = new Datum.ImageRef(dest);
        Datum.ImageRef srcRef = new Datum.ImageRef(src);
        Datum.Rect rect = new Datum.Rect(2, 2, 5, 5);

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(36), true);
        props.add("bgColor", new Datum.Color(221, 221, 221), true);

        ImageMethodDispatcher.dispatch(destRef, "copyPixels",
                List.of(srcRef, rect, new Datum.Rect(0, 0, 3, 3), props));

        assertEquals(0xFFC0C0C0, dest.getPixel(2, 2),
                "Transparent source pixels should leave the destination unchanged");
        assertEquals(0xFF000000, dest.getPixel(3, 3),
                "Black text pixels should still copy through");
    }

    @Test
    void backgroundTransparentCopyPixelsUsesNativeAlphaInsteadOfKeyingTextPixels() {
        Bitmap dest = new Bitmap(3, 3, 32);
        dest.fill(0xFFC0C0C0);

        Bitmap src = new Bitmap(3, 3, 32);
        src.fill(0x00FFFFFF);
        src.setPixel(1, 1, 0xFF000000);
        src.setNativeAlpha(true);

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(36), true);
        props.add("bgColor", new Datum.Color(0, 0, 0), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 3, 3),
                        new Datum.Rect(0, 0, 3, 3), props));

        assertEquals(0xFFC0C0C0, dest.getPixel(0, 0),
                "Native-alpha transparent pixels should leave the destination unchanged");
        assertEquals(0xFF000000, dest.getPixel(1, 1),
                "Native-alpha text pixels should not be erased by a matching #bgColor key");
    }

    @Test
    void backgroundTransparentCopyPixelsKeysOpaqueWhiteMatteBorderOnNativeAlphaSource() {
        Bitmap dest = new Bitmap(7, 5, 32);
        dest.fill(0xFF99CC33);

        Bitmap src = new Bitmap(7, 5, 32);
        src.fill(0x00FFFFFF);
        for (int x = 0; x < 7; x++) {
            src.setPixel(x, 0, 0xFFFFFFFF);
        }
        src.setPixel(1, 1, 0xFFFFFFFF);
        src.setPixel(2, 2, 0xFF000000);
        src.setPixel(3, 3, 0xFFF0F0F0);
        src.setNativeAlpha(true);

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(36), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 7, 5),
                        new Datum.Rect(0, 0, 7, 5), props));

        assertEquals(0xFF99CC33, dest.getPixel(0, 0),
                "Opaque white matte borders should key out under ink 36");
        assertEquals(0xFF99CC33, dest.getPixel(1, 1),
                "Opaque white matte interiors should key out under ink 36");
        assertEquals(0xFF000000, dest.getPixel(2, 2),
                "Black arrow pixels should still copy");
        assertEquals(0xFF99CC33, dest.getPixel(3, 3),
                "Near-white matte antialias pixels should key out with the white border");
    }

    @Test
    void backgroundTransparentCopyPixelsInvertsWhiteNativeAlphaMaskWithExplicitBackgroundColor() {
        Bitmap dest = new Bitmap(3, 1, 32);
        dest.fill(0xFFC0C0C0);

        Bitmap src = new Bitmap(3, 1, 32, new int[] {
                0xFFFFFFFF, 0x00FFFFFF, 0xFFFFFFFF
        });
        src.setNativeAlpha(true);

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(36), true);
        props.add("bgColor", new Datum.Color(221, 221, 221), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 3, 1),
                        new Datum.Rect(0, 0, 3, 1), props));

        assertEquals(0xFFC0C0C0, dest.getPixel(0, 0),
                "Opaque white mask backing should remain transparent over the destination");
        assertEquals(0xFF000000, dest.getPixel(1, 0),
                "Transparent mask holes should become black ink");
        assertEquals(0xFFC0C0C0, dest.getPixel(2, 0));
    }

    @Test
    void backgroundTransparentCopyPixelsInvertsWhiteNativeAlphaMaskWithDefaultBackgroundColor() {
        Bitmap dest = new Bitmap(3, 1, 32);
        dest.fill(0xFFC0C0C0);

        Bitmap src = new Bitmap(3, 1, 32, new int[] {
                0xFFFFFFFF, 0x00FFFFFF, 0xFFFFFFFF
        });
        src.setNativeAlpha(true);

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(36), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 3, 1),
                        new Datum.Rect(0, 0, 3, 1), props));

        assertEquals(0xFFC0C0C0, dest.getPixel(0, 0),
                "Opaque white mask backing should key out with the default white background color");
        assertEquals(0xFF7B9498, dest.getPixel(1, 0),
                "Transparent mask holes should use Director's default UI text-mask ink without explicit #bgColor");
        assertEquals(0xFFC0C0C0, dest.getPixel(2, 0));
    }

    @Test
    void backgroundTransparentInverseAlphaMaskDoesNotPaintOutsideSourceBounds() {
        Bitmap dest = new Bitmap(5, 1, 32);
        dest.fill(0xFFC0C0C0);

        Bitmap src = new Bitmap(3, 1, 32, new int[] {
                0xFFFFFFFF, 0x00FFFFFF, 0xFFFFFFFF
        });
        src.setNativeAlpha(true);

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(36), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 5, 1),
                        new Datum.Rect(0, 0, 5, 1), props));

        assertEquals(0xFFC0C0C0, dest.getPixel(0, 0));
        assertEquals(0xFF7B9498, dest.getPixel(1, 0));
        assertEquals(0xFFC0C0C0, dest.getPixel(2, 0));
        assertEquals(0xFFC0C0C0, dest.getPixel(3, 0),
                "Pixels outside the source bitmap must not be converted to black ink");
        assertEquals(0xFFC0C0C0, dest.getPixel(4, 0));
    }

    @Test
    void backgroundTransparentCopyPixelsPreservesColoredNativeAlphaText() {
        Bitmap dest = new Bitmap(3, 3, 32);
        dest.fill(0xFFC0C0C0);

        Bitmap src = new Bitmap(3, 3, 32);
        src.fill(0x00FFFFFF);
        src.setPixel(1, 1, 0xFF000066);
        src.setNativeAlpha(true);

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(36), true);
        props.add("bgColor", new Datum.Color(221, 221, 221), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 3, 3),
                        new Datum.Rect(0, 0, 3, 3), props));

        assertEquals(0xFFC0C0C0, dest.getPixel(0, 0),
                "Native-alpha transparent pixels should leave the destination unchanged");
        assertEquals(0xFF000066, dest.getPixel(1, 1),
                "Colored text pixels should stay colored when copied with ink 36");
    }

    @Test
    void imageUseAlphaPropertyEnablesNativeAlphaCopyPixels() {
        Bitmap dest = new Bitmap(3, 3, 32);
        dest.fill(0xFFC0C0C0);

        Bitmap src = new Bitmap(3, 3, 32);
        src.fill(0x00FFFFFF);
        src.setPixel(1, 1, 0xFF000000);
        Datum.ImageRef srcRef = new Datum.ImageRef(src);

        ImageMethodDispatcher.setProperty(srcRef, "useAlpha", Datum.TRUE);

        assertTrue(src.isNativeAlpha());
        assertEquals(1, ImageMethodDispatcher.getProperty(srcRef, "useAlpha").toInt());

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(36), true);
        props.add("bgColor", new Datum.Color(0, 0, 0), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(srcRef, new Datum.Rect(0, 0, 3, 3),
                        new Datum.Rect(0, 0, 3, 3), props));

        assertEquals(0xFFC0C0C0, dest.getPixel(0, 0),
                "useAlpha transparent pixels should leave the destination unchanged");
        assertEquals(0xFF000000, dest.getPixel(1, 1),
                "useAlpha text pixels should not be erased by a matching #bgColor key");
    }

    @Test
    void imageSetAlphaAppliesMattePolarityForFakeAlphaText() {
        Bitmap dest = new Bitmap(3, 1, 32);
        dest.fill(0xFF336699);
        dest.setNativeAlpha(true);

        Bitmap matte = new Bitmap(3, 1, 8, new int[] {
                0x00FFFFFF,
                0xFF7F7F7F,
                0xFF000000
        });

        Datum result = ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "setAlpha",
                List.of(new Datum.ImageRef(matte)));

        assertEquals(1, result.toInt());
        assertEquals(0x00336699, dest.getPixel(0, 0),
                "Transparent matte background should clear the destination alpha");
        assertEquals(0x80336699, dest.getPixel(1, 0),
                "Gray antialias matte pixels should become partial alpha");
        assertEquals(0xFF336699, dest.getPixel(2, 0),
                "Black matte text pixels should become fully opaque");
    }

    @Test
    void imageSetAlphaUsesGrayscaleLevelForOpaqueAlphaImages() {
        Bitmap dest = new Bitmap(3, 1, 32);
        dest.fill(0xFF336699);
        dest.setNativeAlpha(true);

        Bitmap alpha = new Bitmap(3, 1, 8, new int[] {
                0xFF000000,
                0xFF7F7F7F,
                0xFFFFFFFF
        });

        Datum result = ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "setAlpha",
                List.of(new Datum.ImageRef(alpha)));

        assertEquals(1, result.toInt());
        assertEquals(0x00336699, dest.getPixel(0, 0));
        assertEquals(0x7F336699, dest.getPixel(1, 0));
        assertEquals(0xFF336699, dest.getPixel(2, 0));
    }

    @Test
    void imageSetAlphaInvertsOpaqueWhiteEdgeTextMattes() {
        Bitmap dest = new Bitmap(5, 3, 32);
        dest.fill(0xFF336699);
        dest.setNativeAlpha(true);

        Bitmap matte = new Bitmap(5, 3, 8);
        matte.fill(0xFFFFFFFF);
        matte.setPixel(2, 1, 0xFF000000);
        matte.setPixel(1, 1, 0xFF7F7F7F);

        Datum result = ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "setAlpha",
                List.of(new Datum.ImageRef(matte)));

        assertEquals(1, result.toInt());
        assertEquals(0x00336699, dest.getPixel(0, 0),
                "White matte edges should become transparent background");
        assertEquals(0x80336699, dest.getPixel(1, 1),
                "Gray antialias matte pixels should become partial alpha");
        assertEquals(0xFF336699, dest.getPixel(2, 1),
                "Dark text matte pixels should become fully opaque");
    }

    @Test
    void imageSetAlphaInvertsSingleLineOpaqueTextMattes() {
        Bitmap dest = new Bitmap(5, 1, 32);
        dest.fill(0xFF336699);
        dest.setNativeAlpha(true);

        Bitmap matte = new Bitmap(5, 1, 8, new int[] {
                0xFFFFFFFF,
                0xFF000000,
                0xFF7F7F7F,
                0xFF000000,
                0xFFFFFFFF
        });

        Datum result = ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "setAlpha",
                List.of(new Datum.ImageRef(matte)));

        assertEquals(1, result.toInt());
        assertEquals(0x00336699, dest.getPixel(0, 0),
                "White matte corners should become transparent background");
        assertEquals(0xFF336699, dest.getPixel(1, 0),
                "Black one-line text pixels should become fully opaque");
        assertEquals(0x80336699, dest.getPixel(2, 0),
                "Gray one-line antialias pixels should become partial alpha");
        assertEquals(0x00336699, dest.getPixel(4, 0),
                "White matte corners should stay transparent after the glyph run");
    }

    @Test
    void imageSetAlphaAppliesFlatAlphaLevel() {
        Bitmap dest = new Bitmap(1, 1, 32);
        dest.fill(0xFF336699);

        Datum result = ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "setAlpha",
                List.of(Datum.of(96)));

        assertEquals(1, result.toInt());
        assertEquals(0x60336699, dest.getPixel(0, 0));
        assertTrue(dest.isNativeAlpha(),
                "setAlpha creates an authored alpha channel even on script-built 32-bit images");
    }

    @Test
    void matteInkTrustsAlphaCreatedBySetAlphaOnScriptBuiltImages() {
        Bitmap dest = new Bitmap(3, 1, 32);
        dest.fill(0xFFBEBEBE);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "setAlpha",
                List.of(Datum.of(255)));

        Bitmap processed = InkProcessor.applyInk(dest, 8, 0xFFFFFF, dest.isNativeAlpha(), null);

        assertEquals(0xFFBEBEBE, processed.getPixel(0, 0),
                "MATTE ink should not flood-fill away opaque pixels after setAlpha supplied alpha");
        assertEquals(0xFFBEBEBE, processed.getPixel(1, 0));
    }

    @Test
    void backgroundTransparentQuadCopyUsesNativeAlphaInsteadOfWhiteKey() {
        Bitmap dest = new Bitmap(3, 3, 32);
        dest.fill(0xFFC0C0C0);

        Bitmap src = new Bitmap(3, 3, 32);
        src.fill(0x00FFFFFF);
        src.setPixel(1, 1, 0xFFFFFFFF);
        src.setNativeAlpha(true);

        Datum.List quad = new Datum.List(new ArrayList<>(List.of(
                new Datum.Point(0, 0),
                new Datum.Point(3, 0),
                new Datum.Point(3, 3),
                new Datum.Point(0, 3)
        )));

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(36), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), quad, new Datum.Rect(0, 0, 3, 3), props));

        assertEquals(0xFFC0C0C0, dest.getPixel(0, 0),
                "Native-alpha transparent pixels should leave the destination unchanged through quad copies");
        assertEquals(0xFFFFFFFF, dest.getPixel(1, 1),
                "Native-alpha white text pixels should not be erased by ink 36's default white key");
    }

    @Test
    void copyPixelsCombinesMaskImageWithNativeAlpha() {
        Bitmap dest = new Bitmap(1, 1, 32);
        dest.fill(0xFFFFFFFF);

        Bitmap src = new Bitmap(1, 1, 32, new int[] { 0x80000000 });
        src.setNativeAlpha(true);

        Bitmap mask = new Bitmap(1, 1, 32, new int[] { 0xFFFFFFFF });

        Datum.PropList props = new Datum.PropList();
        props.add("maskImage", new Datum.ImageRef(mask), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), props));

        assertEquals(0xFFFFFFFF, dest.getPixel(0, 0),
                "Explicit #maskImage should still constrain native-alpha sources");
    }

    @Test
    void copyPixelsHonorsMaskImageWhenNativeAlphaSourceIsOpaque() {
        Bitmap dest = new Bitmap(1, 1, 32);
        dest.fill(0xFFFFFFFF);

        Bitmap src = new Bitmap(1, 1, 32, new int[] { 0xFF000000 });
        src.setNativeAlpha(true);

        Bitmap mask = new Bitmap(1, 1, 32, new int[] { 0xFFFFFFFF });

        Datum.PropList props = new Datum.PropList();
        props.add("maskImage", new Datum.ImageRef(mask), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), props));

        assertEquals(0xFFFFFFFF, dest.getPixel(0, 0),
                "Opaque 32-bit sources should still honor an explicit #maskImage");
    }

    @Test
    void createMaskExtractsDirectorImageMatte() {
        Bitmap wallMask = new Bitmap(3, 1, 8);
        wallMask.setPixel(0, 0, 0xFFFFFFFF);
        wallMask.setPixel(1, 0, 0xFF000000);
        wallMask.setPixel(2, 0, 0xFFFFFFFF);

        Datum result = ImageMethodDispatcher.dispatch(new Datum.ImageRef(wallMask), "createMask", List.of());

        Bitmap mask = assertInstanceOf(Datum.ImageRef.class, result).bitmap();
        assertFalse(Drawing.maskAllowsPixel(mask, 0, 0));
        assertTrue(Drawing.maskAllowsPixel(mask, 1, 0));
        assertFalse(Drawing.maskAllowsPixel(mask, 2, 0));
    }

    @Test
    void darkenCopyPixelsUsesBgColorAsTintInsteadOfMinAgainstDestination() {
        Bitmap dest = new Bitmap(1, 1, 32, new int[] { 0xFF202020 });
        Bitmap src = new Bitmap(1, 1, 32, new int[] { 0xFFC0C0C0 });

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(41), true);
        props.add("bgColor", new Datum.Color(160, 112, 32), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), props));

        assertEquals(0xFF785418, dest.getPixel(0, 0),
                "DARKEN should tint the source with #bgColor, not preserve prior darker destination pixels");
    }

    @Test
    void darkenCopyPixelsTintsIndexedSourceByShadeNotPaletteHue() {
        Bitmap dest = new Bitmap(1, 1, 32, new int[] { 0xFFFFFFFF });
        Bitmap src = new Bitmap(1, 1, 8, new int[] { 0xFF990000 });
        src.setPaletteIndices(new byte[] { 13 });

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(41), true);
        props.add("bgColor", new Datum.Color(0xFF, 0x9B, 0xBD), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), props));

        assertEquals(0xFF985C70, dest.getPixel(0, 0),
                "Indexed DARKEN previews should tint the source shade instead of preserving custom-palette red hue");
    }

    @Test
    void darkenCopyPixelsTintsGrayscaleSourceByDirectorShadeMath() {
        Bitmap dest = new Bitmap(1, 1, 32, new int[] { 0xFFFFFFFF });
        Bitmap src = new Bitmap(1, 1, 32, new int[] { 0xFFC6C6C6 });

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(41), true);
        props.add("bgColor", new Datum.Color(0xEE, 0x7E, 0xA4), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), props));

        assertEquals(0xFFB8617E, dest.getPixel(0, 0),
                "DARKEN previews should multiply grayscale shade by #bgColor using Director's 8-bit fixed-point math");
    }

    @Test
    void darkenCopyPixelsPreservesSubtleSourceColorBias() {
        Bitmap dest = new Bitmap(1, 1, 32, new int[] { 0xFFFFFFFF });
        Bitmap src = new Bitmap(1, 1, 32, new int[] { 0xFFC9C9CB });

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(41), true);
        props.add("bgColor", new Datum.Color(0x46, 0x8D, 0xB9), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), props));

        assertEquals(0xFF366E92, dest.getPixel(0, 0),
                "DARKEN copyPixels should preserve non-neutral source channels in room wrapper art");
    }

    @Test
    void darkenCopyPixelsUsesInversePaletteIndexForIndexedGrayscaleSource() {
        Bitmap dest = new Bitmap(1, 1, 32, new int[] { 0xFFFFFFFF });
        Bitmap src = new Bitmap(1, 1, 8, new int[] { 0xFFCCCCCC });
        src.setImagePalette(Palette.GRAYSCALE_PALETTE);
        src.setPaletteIndices(new byte[] { 57 });

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(41), true);
        props.add("bgColor", new Datum.Color(0xEE, 0x7E, 0xA4), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), props));

        assertEquals(0xFFB8617E, dest.getPixel(0, 0),
                "Indexed grayscale DARKEN should use Director's inverse palette index shade");
    }

    @Test
    void maskedDarkenCopyPixelsUsesInversePaletteIndexForIndexedSource() {
        Bitmap dest = new Bitmap(1, 1, 32, new int[] { 0xFFFFFFFF });
        Bitmap src = new Bitmap(1, 1, 8, new int[] { 0xFFCC6666 });
        src.setImagePalette(Palette.SYSTEM_MAC_PALETTE);
        src.setPaletteIndices(new byte[] { 57 });
        Bitmap mask = new Bitmap(1, 1, 8, new int[] { 0xFF000000 });

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(41), true);
        props.add("bgColor", new Datum.Color(0xEE, 0x7E, 0xA4), true);
        props.add("maskImage", new Datum.ImageRef(mask), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), props));

        assertEquals(0xFFB8617E, dest.getPixel(0, 0),
                "Masked indexed DARKEN preview layers should use the source index as the shade mask");
    }

    @Test
    void maskedDarkenCopyPixelsPreservesFullTintChannelForIndexedShadeSource() {
        Bitmap dest = new Bitmap(1, 1, 32, new int[] { 0xFFFFFFFF });
        Bitmap src = new Bitmap(1, 1, 8, new int[] { 0xFFCC6666 });
        src.setImagePalette(Palette.SYSTEM_MAC_PALETTE);
        src.setPaletteIndices(new byte[] { 107 });
        Bitmap mask = new Bitmap(1, 1, 8, new int[] { 0xFF000000 });

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(41), true);
        props.add("bgColor", new Datum.Color(0xFF, 0x9B, 0xBD), true);
        props.add("maskImage", new Datum.ImageRef(mask), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), props));

        assertEquals(0xFF94596D, dest.getPixel(0, 0),
                "Index-shade DARKEN should preserve a 255 tint channel while using fixed-point math for partial channels");
    }

    @Test
    void maskedDarkenCopyPixelsUsesPaletteColorShadeForCustomIndexedSource() {
        Bitmap dest = new Bitmap(1, 1, 32, new int[] { 0xFFFFFFFF });
        Bitmap src = new Bitmap(1, 1, 8, new int[] { 0xFFBDBABC });
        src.setImagePalette(new Palette(new int[] { 0xFFBDBABC }, "Custom Palette"));
        src.setPaletteIndices(new byte[] { 34 });
        Bitmap mask = new Bitmap(1, 1, 8, new int[] { 0xFF000000 });

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(41), true);
        props.add("bgColor", new Datum.Color(0xFF, 0xDD, 0xEF), true);
        props.add("maskImage", new Datum.ImageRef(mask), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), props));

        assertEquals(0xFFBDA0AF, dest.getPixel(0, 0),
                "Custom-palette indexed DARKEN should multiply each palette pixel channel, not collapse to one shade");
    }

    @Test
    void repeatedDarkenCopyPixelsDoesNotKeepGhostOfPreviousPattern() {
        Bitmap persistentDest = new Bitmap(1, 1, 32, new int[] { 0xFFFFFFFF });
        Bitmap freshDest = new Bitmap(1, 1, 32, new int[] { 0xFFFFFFFF });
        Bitmap firstPattern = new Bitmap(1, 1, 32, new int[] { 0xFF303030 });
        Bitmap secondPattern = new Bitmap(1, 1, 32, new int[] { 0xFFE0E0E0 });

        Datum.PropList firstProps = new Datum.PropList();
        firstProps.add("ink", Datum.of(41), true);
        firstProps.add("bgColor", new Datum.Color(40, 80, 180), true);

        Datum.PropList secondProps = new Datum.PropList();
        secondProps.add("ink", Datum.of(41), true);
        secondProps.add("bgColor", new Datum.Color(200, 140, 60), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(persistentDest), "copyPixels",
                List.of(new Datum.ImageRef(firstPattern), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), firstProps));
        ImageMethodDispatcher.dispatch(new Datum.ImageRef(persistentDest), "copyPixels",
                List.of(new Datum.ImageRef(secondPattern), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), secondProps));

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(freshDest), "copyPixels",
                List.of(new Datum.ImageRef(secondPattern), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), secondProps));

        assertEquals(freshDest.getPixel(0, 0), persistentDest.getPixel(0, 0),
                "Changing a DARKEN-tinted preview should replace the prior pattern instead of leaving a ghost");
    }

    @Test
    void createMatteOnOpaqueFigurePartStillCutsWhiteBoundingBoxForDarken() {
        Bitmap dest = new Bitmap(3, 3, 32);
        dest.fill(0xFFFFFFFF);

        Bitmap src = new Bitmap(3, 3, 32, new int[] {
                0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
                0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF,
                0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
        });
        Bitmap mask = Drawing.createMatte(src);

        Datum.PropList props = new Datum.PropList();
        props.add("maskImage", new Datum.ImageRef(mask), true);
        props.add("ink", Datum.of(41), true);
        props.add("bgColor", new Datum.Color(240, 200, 120), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 3, 3),
                        new Datum.Rect(0, 0, 3, 3), props));

        assertEquals(0xFFFFFFFF, dest.getPixel(0, 0),
                "Opaque non-alpha figure assets still rely on createMatte() to skip white border pixels");
        assertEquals(0xFF000000, dest.getPixel(1, 1));
    }

    @Test
    void copyPixelsBlendOnCopyInkUsesBlendPercentage() {
        Bitmap dest = new Bitmap(1, 1, 32, new int[] { 0xFFFFFFFF });
        Bitmap src = new Bitmap(1, 1, 32, new int[] { 0xFF000000 });

        Datum.PropList props = new Datum.PropList();
        props.add("blend", Datum.of(50), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), props));

        assertEquals(0xFF7F7F7F, dest.getPixel(0, 0),
                "Default copyPixels should honor #blend as source opacity over the destination");
    }

    @Test
    void copyPixelsBlendCombinesSourceAlphaWithBlend() {
        Bitmap dest = new Bitmap(1, 1, 32, new int[] { 0xFFFFFFFF });
        Bitmap src = new Bitmap(1, 1, 32, new int[] { 0x80000000 });
        src.setNativeAlpha(true);

        Datum.PropList props = new Datum.PropList();
        props.add("blend", Datum.of(50), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), props));

        assertEquals(0xFFBFBFBF, dest.getPixel(0, 0),
                "copyPixels blend should scale, not replace, the source alpha");
    }

    @Test
    void copyPixelsBackgroundTransparentBlendRoundsPercentageToNearestAlpha() {
        Bitmap dest = new Bitmap(1, 1, 32, new int[] { 0xFFEEEEEE });
        Bitmap src = new Bitmap(1, 1, 32, new int[] { 0xFF000000 });

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(36), true);
        props.add("blend", Datum.of(30), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), props));

        assertEquals(0xFFA6A6A6, dest.getPixel(0, 0),
                "#blend:30 should map to alpha 77, matching Director's nearest-alpha rounding");
    }

    @Test
    void compatibilityTextMaskCopiesWhiteGlyphsOntoBlackImage() {
        Bitmap textMemberImage = new Bitmap(3, 3, 32, new int[] {
                0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
                0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF,
                0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
        });
        Bitmap mask = new Bitmap(3, 3, 8);
        mask.fill(0xFFFFFFFF);

        Datum.PropList matteProps = new Datum.PropList();
        matteProps.add("ink", Datum.of(8), true);
        ImageMethodDispatcher.dispatch(new Datum.ImageRef(mask), "copyPixels",
                List.of(new Datum.ImageRef(textMemberImage), new Datum.Rect(0, 0, 3, 3),
                        new Datum.Rect(0, 0, 3, 3), matteProps));

        Bitmap solidWhite = new Bitmap(3, 3, 32);
        solidWhite.fill(0xFFFFFFFF);
        Bitmap dest = new Bitmap(3, 3, 32);
        dest.fill(0xFF000000);

        Datum.PropList maskProps = new Datum.PropList();
        maskProps.add("maskImage", new Datum.ImageRef(mask), true);
        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(solidWhite), new Datum.Rect(0, 0, 3, 3),
                        new Datum.Rect(0, 0, 3, 3), maskProps));

        assertEquals(0xFF000000, dest.getPixel(0, 0));
        assertEquals(0xFFFFFFFF, dest.getPixel(1, 1));
        assertEquals(0xFF000000, dest.getPixel(2, 2));
    }

    @Test
    void compatibilityTextMaskPreservesExactRgbOnPaletteBearingDeepImage() {
        Bitmap textMemberImage = new Bitmap(3, 3, 32, new int[] {
                0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
                0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF,
                0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
        });
        Bitmap mask = new Bitmap(3, 3, 8);
        mask.fill(0xFFFFFFFF);

        Datum.PropList matteProps = new Datum.PropList();
        matteProps.add("ink", Datum.of(8), true);
        ImageMethodDispatcher.dispatch(new Datum.ImageRef(mask), "copyPixels",
                List.of(new Datum.ImageRef(textMemberImage), new Datum.Rect(0, 0, 3, 3),
                        new Datum.Rect(0, 0, 3, 3), matteProps));

        Bitmap solidTextColor = new Bitmap(3, 3, 32);
        solidTextColor.fill(0xFFEEEEEE);
        Bitmap dest = new Bitmap(3, 3, 32);
        dest.setImagePalette(Palette.SYSTEM_MAC_PALETTE);
        dest.setPaletteRefSystemName("systemMac");
        dest.fill(0xFFFFFFFF);

        Datum.PropList maskProps = new Datum.PropList();
        maskProps.add("maskImage", new Datum.ImageRef(mask), true);
        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(solidTextColor), new Datum.Rect(0, 0, 3, 3),
                        new Datum.Rect(0, 0, 3, 3), maskProps));

        assertEquals(0xFFFFFFFF, dest.getPixel(0, 0));
        assertEquals(0xFFEEEEEE, dest.getPixel(1, 1));
        assertEquals(0xFFFFFFFF, dest.getPixel(2, 2));
    }

    @Test
    void memberPaletteRefAssignmentOnThirtyTwoBitImageDoesNotRecolorExistingRgbPixels() {
        CastMember member = new CastMember(1, 42, MemberType.BITMAP);
        Bitmap bitmap = new Bitmap(1, 1, 32);
        bitmap.setImagePalette(new Palette(new int[] { 0xEEEEEE }, "one-off text palette"));
        bitmap.fill(0xFFEEEEEE);
        member.setBitmapDirectly(bitmap);

        assertTrue(member.setProp("paletteRef", Datum.symbol("systemMac")));

        assertEquals(0xFFEEEEEE, member.getBitmap().getPixel(0, 0));
        assertSame(Palette.SYSTEM_MAC_PALETTE, member.getBitmap().getImagePalette());
    }

    @Test
    void copyPixelsAcceptsSymbolicAddPinInk() {
        Bitmap dest = new Bitmap(2, 1, 32, new int[] {
                0xFF204060,
                0xFF204060
        });
        Bitmap src = new Bitmap(2, 1, 32, new int[] {
                0xFF000000,
                0xFF40D0FF
        });

        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.symbol("addPin"), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 2, 1),
                        new Datum.Rect(0, 0, 2, 1), props));

        assertEquals(0xFF204060, dest.getPixel(0, 0),
                "Symbolic #addPin must keep black source pixels neutral");
        assertEquals(0xFF60FFFF, dest.getPixel(1, 0),
                "Symbolic #addPin should brighten the destination like numeric ink 33");
    }

    @Test
    void coloredScriptModifiedBitmapSkipsBackgroundTransparentReprocessing() {
        CastMember member = new CastMember(1, 42, MemberType.BITMAP);
        Bitmap bmp = new Bitmap(2, 1, 32, new int[] {
                0xFF6794A7,
                0xFFEEEEEE
        });
        bmp.markScriptModified();
        member.setBitmapDirectly(bmp);

        RenderSprite sprite = new RenderSprite(
                41, 0, 0, 2, 1, 0, true,
                RenderSprite.SpriteType.BITMAP,
                null, member,
                0, 0x00FFFFFF, false, true,
                36, 255, false, false, null, false
        );

        SpriteBaker baker = new SpriteBaker(new BitmapCache(), null, null);
        RenderSprite baked = baker.bake(sprite);

        assertNotNull(baked.getBakedBitmap());
        assertEquals(0xFF6794A7, baked.getBakedBitmap().getPixel(0, 0));
        assertEquals(0xFFEEEEEE, baked.getBakedBitmap().getPixel(1, 0));
    }

    @Test
    void scriptModifiedBitmapStillProcessesBackgroundTransparentWhenKeyColorTouchesBorder() {
        CastMember member = new CastMember(1, 43, MemberType.BITMAP);
        Bitmap bmp = new Bitmap(3, 1, 32, new int[] {
                0xFFFFFFFF,
                0xFF6794A7,
                0xFFFFFFFF
        });
        bmp.markScriptModified();
        member.setBitmapDirectly(bmp);

        RenderSprite sprite = new RenderSprite(
                42, 0, 0, 3, 1, 0, true,
                RenderSprite.SpriteType.BITMAP,
                null, member,
                0, 0x00FFFFFF, false, true,
                36, 255, false, false, null, false
        );

        SpriteBaker baker = new SpriteBaker(new BitmapCache(), null, null);
        RenderSprite baked = baker.bake(sprite);

        assertNotNull(baked.getBakedBitmap());
        assertEquals(0x00000000, baked.getBakedBitmap().getPixel(0, 0));
        assertEquals(0x00000000, baked.getBakedBitmap().getPixel(2, 0));
        assertEquals(0xFF6794A7, baked.getBakedBitmap().getPixel(1, 0),
                "Only the key color should be removed when BACKGROUND_TRANSPARENT processing triggers");
    }

    @Test
    void scriptBuiltBitmapWithTransparentPixelsStillProcessesBackgroundTransparent() {
        CastMember member = new CastMember(1, 44, MemberType.BITMAP);
        Bitmap bmp = new Bitmap(3, 1, 32, new int[] {
                0xFFFFFFFF,
                0x00000000,
                0xFF111111
        });
        bmp.markScriptModified();
        member.setBitmapDirectly(bmp);

        RenderSprite sprite = new RenderSprite(
                43, 0, 0, 3, 1, 0, true,
                RenderSprite.SpriteType.BITMAP,
                null, member,
                0, 0x00FFFFFF, false, true,
                36, 255, false, false, null, false
        );

        SpriteBaker baker = new SpriteBaker(new BitmapCache(), null, null);
        RenderSprite baked = baker.bake(sprite);

        assertNotNull(baked.getBakedBitmap());
        assertEquals(0x00000000, baked.getBakedBitmap().getPixel(0, 0),
                "White key color should still be removed even when the script bitmap already contains transparency");
        assertEquals(0x00000000, baked.getBakedBitmap().getPixel(1, 0),
                "Existing transparent pixels must be preserved");
        assertEquals(0xFF111111, baked.getBakedBitmap().getPixel(2, 0),
                "Non-key pixels must survive BACKGROUND_TRANSPARENT processing");
    }

    @Test
    void scriptModifiedDarkenTreatsOpaqueWhiteCanvasAsNeutral() {
        CastMember member = new CastMember(1, 45, MemberType.BITMAP);
        member.setName("dynamic_tint_canvas");
        Bitmap bmp = new Bitmap(3, 1, 32, new int[] {
                0xFFFFFFFF,
                0xFF808080,
                0xFFFFFFFF
        });
        bmp.markScriptModified();
        member.setBitmapDirectly(bmp);

        RenderSprite sprite = new RenderSprite(
                44, 0, 0, 3, 1, 0, true,
                RenderSprite.SpriteType.BITMAP,
                null, member,
                0, 0xA07020, false, true,
                41, 100, false, false, null, false
        );

        SpriteBaker baker = new SpriteBaker(new BitmapCache(), null, null);
        RenderSprite baked = baker.bake(sprite);

        assertNotNull(baked.getBakedBitmap());
        assertEquals(0x00000000, baked.getBakedBitmap().getPixel(0, 0),
                "Untouched opaque white on a script canvas should stay non-contributing under DARKEN");
        assertEquals(0xFF503810, baked.getBakedBitmap().getPixel(1, 0),
                "Actual drawn content should still be tinted by the sprite bgColor");
        assertEquals(0x00000000, baked.getBakedBitmap().getPixel(2, 0));
    }

    @Test
    void scriptModifiedIndexedDarkenUsesPaletteIndicesForSpriteColorRamp() {
        CastMember member = new CastMember(1, 46, MemberType.BITMAP);
        member.setName("dynamic_indexed_photo");
        Bitmap bmp = new Bitmap(3, 1, 8, new int[] {
                0xFF383838,
                0xFF282828,
                0xFF808080
        });
        bmp.setImagePalette(Palette.GRAYSCALE_PALETTE);
        bmp.setPaletteIndices(new byte[] {(byte) 199, (byte) 215, (byte) 127});
        bmp.markScriptModified();
        member.setBitmapDirectly(bmp);

        RenderSprite sprite = new RenderSprite(
                45, 0, 0, 3, 1, 0, true,
                RenderSprite.SpriteType.BITMAP,
                null, member,
                0x681F10, 0xFFCC66, true, true,
                41, 100, false, false, null, false
        );

        SpriteBaker baker = new SpriteBaker(new BitmapCache(), null, null);
        RenderSprite baked = baker.bake(sprite);

        assertNotNull(baked.getBakedBitmap());
        assertEquals(0xFFA04B26, baked.getBakedBitmap().getPixel(0, 0));
        assertEquals(0xFF903F20, baked.getBakedBitmap().getPixel(1, 0));
        assertEquals(0xFFE88543, baked.getBakedBitmap().getPixel(2, 0));
    }

    @Test
    void copyPixelsOntoIndexedDestinationRefreshesOnlyAffectedPaletteIndices() {
        Bitmap dest = new Bitmap(3, 1, 8, new int[] {
                0xFFFFFFFF,
                0xFF808080,
                0xFF000000
        });
        dest.setImagePalette(Palette.GRAYSCALE_PALETTE);
        dest.setPaletteIndices(new byte[] {0, (byte) 127, (byte) 255});
        Bitmap src = new Bitmap(1, 1, 32, new int[] {0xFF404040});

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(1, 0, 2, 1),
                        new Datum.Rect(0, 0, 1, 1)));

        assertArrayEquals(new byte[] {0, (byte) 191, (byte) 255}, dest.getPaletteIndices());
    }

    /**
     * Tests the cloud pattern: pImg = member.image is obtained early,
     * then member.image = image(w,h,8) replaces the member's bitmap,
     * then pImg.fill() and pImg.copyPixels() must operate on the NEW bitmap.
     * In Director, member.image returns a live reference that stays in sync.
     */
    @Test
    void liveImageRefStaysInSyncAfterMemberImageReplacement() {
        // 1. Create a member with a red bitmap
        CastMember member = new CastMember(1, 42, MemberType.BITMAP);
        Bitmap original = new Bitmap(10, 10, 32);
        original.fill(0xFFFF0000); // red
        member.setBitmapDirectly(original);

        // 2. Get pImg = member.image (live reference)
        Datum imageRef = member.getProp("image");
        assertInstanceOf(Datum.ImageRef.class, imageRef);
        Datum.ImageRef pImg = (Datum.ImageRef) imageRef;

        // Verify it currently resolves to the red bitmap
        assertEquals(0xFFFF0000, pImg.bitmap().getPixel(5, 5));

        // 3. Replace member's image (like initCloud: pCloudMember.image = image(w, 60, 8))
        Bitmap replacement = new Bitmap(20, 60, 32);
        replacement.fill(0xFF00FF00); // green
        member.setProp("image", new Datum.ImageRef(replacement));

        // 4. pImg should now resolve to the NEW bitmap (the green one), not the old red one
        Bitmap resolvedBmp = pImg.bitmap();
        assertNotSame(original, resolvedBmp, "pImg should NOT still point to the old bitmap");
        assertEquals(0xFF00FF00, resolvedBmp.getPixel(5, 5),
                "pImg should resolve to the member's new (green) bitmap");

        // 5. Modify via pImg (like the cloud turn handler)
        int blue = 0xFF0000FF;
        ImageMethodDispatcher.dispatch(pImg, "fill",
                List.of(new Datum.Rect(0, 0, 20, 60), new Datum.Color(0, 0, 255)));

        // 6. The member's bitmap should be the same one that was filled
        assertEquals(blue, member.getBitmap().getPixel(5, 5),
                "member.getBitmap() should reflect the fill done through pImg");
        assertTrue(member.getBitmap().isScriptModified(),
                "member's bitmap should be marked script-modified");
    }

    @Test
    void duplicatePreservesPaletteRefMetadata() {
        Bitmap src = new Bitmap(4, 4, 8);
        src.setImagePalette(Palette.SYSTEM_MAC_PALETTE);
        src.setPaletteRefCastMember(2, 77);

        Datum.ImageRef duplicate = (Datum.ImageRef) ImageMethodDispatcher.dispatch(
                new Datum.ImageRef(src), "duplicate", List.of());

        assertSame(Palette.SYSTEM_MAC_PALETTE, duplicate.bitmap().getImagePalette());
        Datum paletteRef = ImageMethodDispatcher.getProperty(duplicate, "paletteRef");
        assertInstanceOf(Datum.CastMemberRef.class, paletteRef);
        assertEquals(2, ((Datum.CastMemberRef) paletteRef).castLibNum());
        assertEquals(77, ((Datum.CastMemberRef) paletteRef).memberNum());
    }

    @Test
    void cropPreservesPaletteRefMetadata() {
        Bitmap src = new Bitmap(4, 4, 8);
        src.setImagePalette(Palette.SYSTEM_WIN_PALETTE);
        src.setPaletteRefSystemName("systemWin");

        Datum.ImageRef cropped = (Datum.ImageRef) ImageMethodDispatcher.dispatch(
                new Datum.ImageRef(src), "crop", List.of(new Datum.Rect(1, 1, 3, 3)));

        assertSame(Palette.SYSTEM_WIN_PALETTE, cropped.bitmap().getImagePalette());
        Datum paletteRef = ImageMethodDispatcher.getProperty(cropped, "paletteRef");
        assertInstanceOf(Datum.Symbol.class, paletteRef);
        assertEquals("systemWin", ((Datum.Symbol) paletteRef).name());
    }

    @Test
    void remapImagePaletteRecolorsExisting8BitPixels() {
        Palette oldPalette = new Palette(new int[]{0xFFFFFF, 0x111111, 0x224466}, "old");
        Palette newPalette = new Palette(new int[]{0xFFFFFF, 0xAA5500, 0x66CC22}, "new");
        Bitmap bmp = new Bitmap(2, 1, 8);
        bmp.setImagePalette(oldPalette);
        bmp.setPixel(0, 0, 0xFF111111);
        bmp.setPixel(1, 0, 0xFF224466);

        int changed = bmp.remapImagePalette(newPalette);

        assertEquals(2, changed);
        assertSame(newPalette, bmp.getImagePalette());
        assertEquals(0xFFAA5500, bmp.getPixel(0, 0));
        assertEquals(0xFF66CC22, bmp.getPixel(1, 0));
    }

    @Test
    void remapImagePaletteUsesPreservedIndicesWhenSourceRgbCollides() {
        Palette oldPalette = new Palette(new int[]{0xFFFFFF, 0x222222, 0x222222}, "old-colliding");
        Palette newPalette = new Palette(new int[]{0xFFFFFF, 0xAA5500, 0x33AA77}, "new-diverged");

        Bitmap bmp = new Bitmap(2, 1, 8);
        bmp.setImagePalette(oldPalette);
        bmp.setPixel(0, 0, 0xFF222222);
        bmp.setPixel(1, 0, 0xFF222222);
        bmp.setPaletteIndices(new byte[]{1, 2});

        Bitmap duplicate = bmp.copy();
        int changed = duplicate.remapImagePalette(newPalette);

        assertEquals(2, changed);
        assertSame(newPalette, duplicate.getImagePalette());
        assertEquals(0xFFAA5500, duplicate.getPixel(0, 0),
                "Index 1 should map to the first divergent target shade");
        assertEquals(0xFF33AA77, duplicate.getPixel(1, 0),
                "Index 2 must not collapse onto index 1 just because the old RGB matched");
    }

    @Test
    void remapImagePaletteUsesPreservedIndicesWithoutOldPaletteMetadata() {
        Palette newPalette = new Palette(new int[]{0xD4DDE1, 0x335577}, "nav-ui");
        Bitmap bmp = new Bitmap(2, 1, 8);
        bmp.setPixel(0, 0, 0xFFDBDBDB);
        bmp.setPixel(1, 0, 0xFF999999);
        bmp.setPaletteIndices(new byte[]{0, 1});

        int changed = bmp.remapImagePalette(newPalette);

        assertEquals(2, changed);
        assertSame(newPalette, bmp.getImagePalette());
        assertEquals(0xFFD4DDE1, bmp.getPixel(0, 0));
        assertEquals(0xFF335577, bmp.getPixel(1, 0));
    }

    @Test
    void fillOnPalettedImageResolvesSmallIntegersThroughImagePalette() {
        Palette navPalette = new Palette(new int[]{0xFFFFFF, 0xD4DDE1}, "nav-ui");
        Bitmap bmp = new Bitmap(2, 2, 8);
        bmp.setImagePalette(navPalette);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(bmp), "fill",
                List.of(new Datum.Rect(0, 0, 2, 2), Datum.of(1)));

        assertEquals(0xFFD4DDE1, bmp.getPixel(0, 0));
        assertEquals(0xFFD4DDE1, bmp.getPixel(1, 1));
        assertArrayEquals(new byte[]{1, 1, 1, 1}, bmp.getPaletteIndices(),
                "paletted fills should preserve the palette index used by later remaps");
    }

    @Test
    void fillOnPaletteRefThirtyTwoBitImageResolvesSmallIntegersThroughImagePalette() {
        Palette uiPalette = new Palette(new int[]{0xFFFFFF, 0xBEBEBE}, "ui");
        Bitmap bmp = new Bitmap(2, 2, 32);
        bmp.setImagePalette(uiPalette);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(bmp), "fill",
                List.of(new Datum.Rect(0, 0, 2, 2), Datum.of(1)));

        assertEquals(0xFFBEBEBE, bmp.getPixel(0, 0));
        assertEquals(0xFFBEBEBE, bmp.getPixel(1, 1));
        assertNull(bmp.getPaletteIndices(),
                "32-bit paletteRef images resolve colors through the palette but remain RGB images");
    }

    @Test
    void paletteRefAssignmentOnThirtyTwoBitImageDoesNotRecolorExistingRgbPixels() {
        Palette sourcePalette = new Palette(new int[]{0xFFFFFF, 0xFFCC99}, "source");
        Bitmap bmp = new Bitmap(1, 1, 32);
        bmp.setImagePalette(sourcePalette);
        bmp.setPixel(0, 0, 0xFFFFCC99);
        bmp.setPaletteIndices(new byte[]{1});

        ImageMethodDispatcher.setProperty(new Datum.ImageRef(bmp), "paletteRef", Datum.symbol("systemMac"));

        assertSame(Palette.SYSTEM_MAC_PALETTE, bmp.getImagePalette());
        assertEquals(0xFFFFCC99, bmp.getPixel(0, 0),
                "RGB/deep images keep authored pixels when paletteRef metadata changes");
    }

    @Test
    void drawOnPaletteRefThirtyTwoBitImageResolvesSmallIntegersThroughImagePalette() {
        Palette uiPalette = new Palette(new int[]{0xFFFFFF, 0xA6A6A6}, "ui");
        Bitmap bmp = new Bitmap(3, 3, 32);
        bmp.setImagePalette(uiPalette);
        bmp.fill(0xFFFFFFFF);

        Datum.PropList props = new Datum.PropList();
        props.put("color", true, Datum.of(1));
        props.put("shapeType", true, new Datum.Symbol("rect"));

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(bmp), "draw",
                List.of(new Datum.Rect(0, 0, 3, 3), props));

        assertEquals(0xFFA6A6A6, bmp.getPixel(0, 0));
        assertEquals(0xFFA6A6A6, bmp.getPixel(1, 0));
        assertEquals(0xFFFFFFFF, bmp.getPixel(1, 1));
        assertNull(bmp.getPaletteIndices(),
                "32-bit paletteRef draw operations resolve colors through the palette but remain RGB images");
    }

    @Test
    void rgbFillOnPaletteRefThirtyTwoBitImageDoesNotQuantizeToNearestPaletteColor() {
        Palette uiPalette = new Palette(new int[]{0xFFFFFF, 0x669999}, "ui");
        Bitmap bmp = new Bitmap(2, 2, 32);
        bmp.setImagePalette(uiPalette);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(bmp), "fill",
                List.of(new Datum.Rect(0, 0, 2, 2), new Datum.Color(0x67, 0x94, 0xA7)));

        assertEquals(0xFF6794A7, bmp.getPixel(0, 0));
        assertEquals(0xFF6794A7, bmp.getPixel(1, 1));
        assertNull(bmp.getPaletteIndices(),
                "RGB fills into 32-bit paletteRef images must keep authored RGB values");
    }

    @Test
    void palettedFillIndicesSurviveLaterIndexedCopyPixels() {
        Palette navPalette = new Palette(new int[]{0xFFFFFF, 0xD4DDE1, 0xC0C0C0}, "nav-ui");
        Bitmap dest = new Bitmap(4, 1, 8);
        dest.setImagePalette(navPalette);
        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "fill",
                List.of(new Datum.Rect(0, 0, 4, 1), new Datum.PaletteIndexColor(1)));

        Bitmap src = new Bitmap(1, 1, 8);
        src.setImagePalette(navPalette);
        src.fillRectPaletteIndex(0, 0, 1, 1, 2, 0xFFC0C0C0);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(3, 0, 4, 1),
                        new Datum.Rect(0, 0, 1, 1)));

        assertArrayEquals(new byte[]{1, 1, 1, 2}, dest.getPaletteIndices(),
                "copyPixels should update the copied region without clearing previous paletted fills");
        assertEquals(0xFFD4DDE1, dest.getPixel(0, 0));
        assertEquals(0xFFC0C0C0, dest.getPixel(3, 0));
    }

    @Test
    void palettedBackgroundTransparentCopyDoesNotPreserveKeyedOutWhiteIndex() {
        Palette navPalette = new Palette(new int[]{0xFFFFFF, 0xD4DDE1, 0x000000}, "nav-ui");
        Bitmap dest = new Bitmap(2, 1, 8);
        dest.setImagePalette(navPalette);
        dest.fillRectPaletteIndex(0, 0, 2, 1, 1, 0xFFD4DDE1);

        Bitmap src = new Bitmap(2, 1, 8);
        src.setImagePalette(navPalette);
        src.fillRectPaletteIndex(0, 0, 1, 1, 0, 0xFFFFFFFF);
        src.fillRectPaletteIndex(1, 0, 1, 1, 2, 0xFF000000);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 2, 1),
                        new Datum.Rect(0, 0, 2, 1),
                        inkProps(36)));

        assertArrayEquals(new byte[]{1, 2}, dest.getPaletteIndices(),
                "white keyed out by Background Transparent must not overwrite destination palette metadata");
        assertEquals(0xFFD4DDE1, dest.getPixel(0, 0));
        assertEquals(0xFF000000, dest.getPixel(1, 0));
    }

    @Test
    void rgbCopyIntoPalettedImageKeepsMatteWhiteSlotAvailable() {
        Palette windowPalette = new Palette(new int[]{0xFFFFFF, 0xEFEFEF, 0x000000}, "window-ui");
        Bitmap dest = new Bitmap(3, 1, 8);
        dest.setImagePalette(windowPalette);
        dest.fillRectPaletteIndex(0, 0, 3, 1, 1, 0xFFEFEFEF);

        Bitmap src = new Bitmap(3, 1, 32, new int[]{
                0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF
        });

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 3, 1),
                        new Datum.Rect(0, 0, 3, 1)));

        assertArrayEquals(new byte[]{1, 2, 1}, dest.getPaletteIndices(),
                "RGB white copied into a paletted UI image should not consume the matte-white slot");
        assertEquals(0xFFEFEFEF, dest.getPixel(0, 0));
        assertEquals(0xFF000000, dest.getPixel(1, 0));
        assertEquals(0xFFEFEFEF, dest.getPixel(2, 0));
    }

    @Test
    void rgbTextCopyIntoPalettedControlPreservesExistingWhiteButtonFill() {
        Palette windowPalette = new Palette(new int[]{0xFFFFFF, 0xEFEFEF, 0x555555, 0x000000}, "window-ui");
        Bitmap dest = new Bitmap(5, 1, 8);
        dest.setImagePalette(windowPalette);
        dest.fillRectPaletteIndex(0, 0, 5, 1, 1, 0xFFEFEFEF);
        dest.fillRectPaletteIndex(4, 0, 1, 1, 2, 0xFF555555);

        Bitmap src = new Bitmap(5, 1, 32, new int[]{
                0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
        });

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 5, 1),
                        new Datum.Rect(0, 0, 5, 1)));

        assertArrayEquals(new byte[]{1, 3, 1, 1, 2}, dest.getPaletteIndices(),
                "Opaque white in a copied text image should preserve the prebuilt indexed button body");
        assertEquals(0xFFEFEFEF, dest.getPixel(0, 0));
        assertEquals(0xFF000000, dest.getPixel(1, 0));
        assertEquals(0xFFEFEFEF, dest.getPixel(2, 0));
        assertEquals(0xFFEFEFEF, dest.getPixel(3, 0));
        assertEquals(0xFF555555, dest.getPixel(4, 0));
    }

    @Test
    void whiteBackedGrayscaleCopyIntoRgbWrapperTreatsWhiteAsTransparent() {
        Bitmap dest = new Bitmap(4, 3, 32);
        dest.fill(0xFF88ADBD);

        Bitmap src = new Bitmap(4, 3, 32, new int[]{
                0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
                0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFFFFFFFF,
                0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
        });

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 4, 3),
                        new Datum.Rect(0, 0, 4, 3)));

        assertEquals(0xFF88ADBD, dest.getPixel(0, 0),
                "white-backed grayscale wrapper copies should keep the destination background visible");
        assertEquals(0xFF000000, dest.getPixel(1, 1));
        assertEquals(0xFF000000, dest.getPixel(2, 1));
        assertEquals(0xFF88ADBD, dest.getPixel(3, 2));
    }

    @Test
    void alreadyColoredTextCopyWithColorParamPreservesExactRgb() {
        Bitmap dest = new Bitmap(3, 1, 32, new int[]{
                0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
        });
        Bitmap src = new Bitmap(3, 1, 32, new int[]{
                0xFFFFFFFF, 0xFFEEEEEE, 0xFFFFFFFF
        });

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 3, 1),
                        new Datum.Rect(0, 0, 3, 1),
                        colorInkProps(0xEEEEEE, 36)));

        assertEquals(0xFFFFFFFF, dest.getPixel(0, 0));
        assertEquals(0xFFEEEEEE, dest.getPixel(1, 0),
                "Already-colored white-backed text must not be reinterpreted as a low-alpha text mask");
        assertEquals(0xFFFFFFFF, dest.getPixel(2, 0));
    }

    @Test
    void scaledPalettedCopyPixelsScalePaletteIndicesWithPixels() {
        Palette navPalette = new Palette(new int[]{0xFFFFFF, 0xDEDEDE, 0x000000}, "nav-ui");
        Bitmap src = new Bitmap(3, 1, 8);
        src.setImagePalette(navPalette);
        src.fillRectPaletteIndex(0, 0, 1, 1, 2, 0xFF000000);
        src.fillRectPaletteIndex(1, 0, 2, 1, 1, 0xFFDEDEDE);

        Bitmap dest = new Bitmap(3, 6, 8);
        dest.setImagePalette(navPalette);
        dest.fill(0xFFFFFFFF);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 3, 6),
                        new Datum.Rect(0, 0, 3, 1),
                        inkProps(36)));

        byte[] indices = dest.getPaletteIndices();
        assertNotNull(indices);
        assertEquals(2, indices[0] & 0xFF);
        assertEquals(1, indices[1] & 0xFF);
        assertEquals(1, indices[(5 * 3) + 2] & 0xFF);

        Bitmap buffer = new Bitmap(3, 6, 8);
        buffer.setImagePalette(navPalette);
        buffer.fill(0xFFFFFFFF);
        ImageMethodDispatcher.dispatch(new Datum.ImageRef(buffer), "copyPixels",
                List.of(new Datum.ImageRef(dest), new Datum.Rect(0, 0, 3, 6),
                        new Datum.Rect(0, 0, 3, 6)));

        assertEquals(0xFFDEDEDE, buffer.getPixel(1, 3),
                "later paletted copies should not rematerialize scaled track pixels as palette index 0");
    }

    private static Datum.PropList inkProps(int ink) {
        Datum.PropList props = new Datum.PropList();
        props.add("ink", Datum.of(ink), true);
        return props;
    }

    private static Datum.PropList colorInkProps(int colorRgb, int ink) {
        Datum.PropList props = inkProps(ink);
        props.add("color", new Datum.Color(
                (colorRgb >> 16) & 0xFF,
                (colorRgb >> 8) & 0xFF,
                colorRgb & 0xFF), true);
        return props;
    }

    @Test
    void bitmapMemberPalettePropertyAcceptsPaletteMemberName() {
        Palette defaultPalette = new Palette(new int[]{0xDBDBDB, 0x000000}, "default");
        Palette navPalette = new Palette(new int[]{0xD4DDE1, 0x000000}, "nav_ui_palette");
        Bitmap bmp = new Bitmap(1, 1, 8);
        bmp.setImagePalette(defaultPalette);
        bmp.setPixel(0, 0, 0xFFDBDBDB);
        bmp.setPaletteIndices(new byte[]{0});
        CastMember member = new CastMember(1, 42, MemberType.BITMAP);
        member.setBitmapDirectly(bmp);

        CastLibProvider.setProvider(new PaletteNameProvider(navPalette));
        try {
            assertTrue(member.setProp("palette", Datum.of("nav_ui_palette")));

            assertEquals(0xFFD4DDE1, member.getBitmap().getPixel(0, 0));
            Datum paletteRef = member.getProp("paletteRef");
            assertInstanceOf(Datum.CastMemberRef.class, paletteRef);
            assertEquals(3, ((Datum.CastMemberRef) paletteRef).castLibNum());
            assertEquals(44, ((Datum.CastMemberRef) paletteRef).memberNum());
        } finally {
            CastLibProvider.clearProvider();
        }
    }

    private record PaletteNameProvider(Palette palette) implements CastLibProvider {
        @Override
        public int getCastLibByNumber(int castLibNumber) { return castLibNumber; }

        @Override
        public int getCastLibByName(String name) { return -1; }

        @Override
        public Datum getCastLibProp(int castLibNumber, String propName) { return Datum.VOID; }

        @Override
        public boolean setCastLibProp(int castLibNumber, String propName, Datum value) { return false; }

        @Override
        public Datum getMember(int castLibNumber, int memberNumber) { return Datum.VOID; }

        @Override
        public Datum getMemberByName(int castLibNumber, String memberName) {
            return "nav_ui_palette".equals(memberName) ? Datum.CastMemberRef.of(3, 44) : Datum.VOID;
        }

        @Override
        public int getCastLibCount() { return 1; }

        @Override
        public Datum getMemberProp(int castLibNumber, int memberNumber, String propName) { return Datum.VOID; }

        @Override
        public boolean setMemberProp(int castLibNumber, int memberNumber, String propName, Datum value) { return false; }

        @Override
        public Palette getMemberPalette(int castLibNumber, int memberNumber) {
            return castLibNumber == 3 && memberNumber == 44 ? palette : null;
        }
    }

    @Test
    void copyPixelsCarriesPaletteMetadataIntoBlankDynamicWrapperImages() {
        Palette sourcePalette = new Palette(new int[]{0xFFFFFF, 0x6C5230}, "room-floor");
        Bitmap src = new Bitmap(1, 1, 8);
        src.setImagePalette(sourcePalette);
        src.setPaletteRefCastMember(2, 77);
        src.setPixel(0, 0, 0xFF6C5230);

        Bitmap dest = new Bitmap(1, 1, 32);
        dest.fill(0xFFFFFFFF);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1)));

        assertSame(sourcePalette, dest.getImagePalette(),
                "Blank wrapper images should inherit the source palette provenance");
        assertEquals(2, dest.getPaletteRefCastLib());
        assertEquals(77, dest.getPaletteRefMemberNum());
        assertEquals(0xFF6C5230, dest.getPixel(0, 0));
    }

    @Test
    void copyPixelsDoesNotRemapCopiedIndicesThroughAnUnrelatedDestinationPalette() {
        int[] systemMacLike = new int[256];
        systemMacLike[248] = 0xBEBEBE;
        Palette sourcePalette = new Palette(systemMacLike, "systemMac-like");
        int[] windowPaletteColors = new int[256];
        windowPaletteColors[248] = 0x000000;
        Palette windowPalette = new Palette(windowPaletteColors, "window");

        Bitmap src = new Bitmap(1, 1, 8);
        src.setImagePalette(sourcePalette);
        src.fillRectPaletteIndex(0, 0, 1, 1, 248, 0xFFBEBEBE);

        Bitmap dest = new Bitmap(1, 1, 32);
        dest.setImagePalette(windowPalette);
        dest.fill(0xFFFFFFFF);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1)));

        assertEquals(0xFFBEBEBE, dest.getPixel(0, 0),
                "Copied RGB must survive when source and destination palette refs differ");
        assertNull(dest.getPaletteIndices(),
                "Incompatible palette indices should not be preserved into the destination");
    }

    @Test
    void quadCopiedPalettedWrapperKeepsIndicesAndDynamicMatteRemap() {
        Palette sourcePalette = new Palette(new int[]{0xFFFFFF, 0xFF808080, 0xFF000000}, "furni-ramp");
        Bitmap src = new Bitmap(2, 3, 8, new int[]{
                0xFFFFFFFF, 0xFF000000,
                0xFF808080, 0xFF000000,
                0xFFFFFFFF, 0xFF808080
        });
        src.setImagePalette(sourcePalette);
        src.setPaletteRefCastMember(4, 12);
        src.setPaletteIndices(new byte[]{
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

        assertSame(sourcePalette, dest.getImagePalette(),
                "Rotated wrapper images must keep the source palette for later sprite recoloring");
        assertEquals(4, dest.getPaletteRefCastLib());
        assertEquals(12, dest.getPaletteRefMemberNum());
        assertArrayEquals(new byte[]{
                0, (byte) 128, 0,
                (byte) 128, (byte) 255, (byte) 255
        }, dest.getPaletteIndices(),
                "copyPixels quad rotation should preserve per-pixel palette ramp information");

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
    void imageMutationCallbackFiresForScriptImageOps() {
        Bitmap bmp = new Bitmap(2, 2, 32);
        AtomicInteger callbackCount = new AtomicInteger();
        ImageMethodDispatcher.setImageMutationCallback(callbackCount::incrementAndGet);
        try {
            ImageMethodDispatcher.dispatch(new Datum.ImageRef(bmp), "fill",
                    List.of(new Datum.Rect(0, 0, 2, 2), new Datum.Color(255, 0, 0)));
            assertEquals(1, callbackCount.get());

            Bitmap src = new Bitmap(1, 1, 32);
            src.fill(0xFF00FF00);
            ImageMethodDispatcher.dispatch(new Datum.ImageRef(bmp), "copyPixels",
                    List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                            new Datum.Rect(0, 0, 1, 1)));
            assertEquals(2, callbackCount.get());
        } finally {
            ImageMethodDispatcher.setImageMutationCallback(null);
        }
    }

    @Test
    void copyPixelsTransfersAnchorMetadataIntoBlankDestination() {
        Bitmap src = new Bitmap(4, 4, 32);
        src.setAnchorPoint(2, 3);
        src.fill(0xFF00FF00);

        Bitmap dest = new Bitmap(8, 8, 32);
        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(1, 2, 5, 6),
                        new Datum.Rect(0, 0, 4, 4)));

        assertTrue(dest.hasAnchorPoint());
        assertEquals(3, dest.getAnchorX());
        assertEquals(5, dest.getAnchorY());
    }

    @Test
    void memberImageAssignmentPreservesMemberRegPoint() {
        CastMember member = new CastMember(1, 10001, MemberType.BITMAP);
        assertTrue(member.setProp("regPoint", new Datum.Point(9, 11)));
        Bitmap preview = new Bitmap(10, 12, 32);
        preview.setAnchorPoint(4, 7);
        preview.fill(0xFF123456);

        assertTrue(member.setProp("image", new Datum.ImageRef(preview)));

        assertEquals(9, member.getRegPointX());
        assertEquals(11, member.getRegPointY());
        assertTrue(member.getBitmap().hasAnchorPoint());
        assertEquals(9, member.getBitmap().getAnchorX());
        assertEquals(11, member.getBitmap().getAnchorY());
    }

    @Test
    void dynamicMemberImageAssignmentAdoptsImageAnchorWhenRegPointIsNotPinned() {
        CastMember member = new CastMember(1, 10002, MemberType.BITMAP);
        Bitmap preview = new Bitmap(10, 12, 32);
        preview.setAnchorPoint(4, 7);
        preview.fill(0xFF123456);

        assertTrue(member.setProp("image", new Datum.ImageRef(preview)));

        assertEquals(4, member.getRegPointX());
        assertEquals(7, member.getRegPointY());
        assertTrue(member.getBitmap().hasAnchorPoint());
        assertEquals(4, member.getBitmap().getAnchorX());
        assertEquals(7, member.getBitmap().getAnchorY());
    }

    @Test
    void liveDynamicMemberImageCopyPixelsAdoptsTransferredAnchor() {
        CastMember member = new CastMember(1, 10003, MemberType.BITMAP);
        member.setBitmapDirectly(new Bitmap(8, 8, 32));

        Datum.ImageRef liveImage = (Datum.ImageRef) member.getProp("image");
        Bitmap source = new Bitmap(4, 4, 32);
        source.setAnchorPoint(2, 3);
        source.fill(0xFF00FF00);

        ImageMethodDispatcher.dispatch(liveImage, "copyPixels",
                List.of(new Datum.ImageRef(source), new Datum.Rect(1, 2, 5, 6),
                        new Datum.Rect(0, 0, 4, 4)));

        Bitmap memberBitmap = member.getBitmap();
        assertTrue(memberBitmap.hasAnchorPoint());
        assertEquals(3, memberBitmap.getAnchorX());
        assertEquals(5, memberBitmap.getAnchorY());
        assertEquals(3, member.getRegPointX());
        assertEquals(5, member.getRegPointY());
    }

    @Test
    void livePinnedMemberImageCopyPixelsKeepsExplicitMemberRegPoint() {
        CastMember member = new CastMember(1, 10004, MemberType.BITMAP);
        member.setBitmapDirectly(new Bitmap(8, 8, 32));
        assertTrue(member.setProp("regPoint", new Datum.Point(9, 11)));

        Datum.ImageRef liveImage = (Datum.ImageRef) member.getProp("image");
        Bitmap source = new Bitmap(4, 4, 32);
        source.setAnchorPoint(2, 3);
        source.fill(0xFF00FF00);

        ImageMethodDispatcher.dispatch(liveImage, "copyPixels",
                List.of(new Datum.ImageRef(source), new Datum.Rect(1, 2, 5, 6),
                        new Datum.Rect(0, 0, 4, 4)));

        Bitmap memberBitmap = member.getBitmap();
        assertTrue(memberBitmap.hasAnchorPoint());
        assertEquals(9, memberBitmap.getAnchorX());
        assertEquals(11, memberBitmap.getAnchorY());
        assertEquals(9, member.getRegPointX());
        assertEquals(11, member.getRegPointY());
    }
}
