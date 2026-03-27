package com.libreshockwave.player;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.Drawing;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.player.cast.CastMember;
import com.libreshockwave.player.render.pipeline.BitmapCache;
import com.libreshockwave.player.render.pipeline.RenderSprite;
import com.libreshockwave.player.render.pipeline.SpriteBaker;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.opcode.dispatch.ImageMethodDispatcher;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Tests that Lingo image modifications (fill, copyPixels) are visible
 * to the renderer via SpriteBaker.
 */
public class ScriptModifiedBitmapTest {

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
    void copyPixelsIgnoresMaskImageWhenSourceUsesNativeAlpha() {
        Bitmap dest = new Bitmap(1, 1, 32);
        dest.fill(0xFFFFFFFF);

        Bitmap src = new Bitmap(1, 1, 32, new int[] { 0x80000000 });
        src.setNativeAlpha(true);

        Bitmap mask = new Bitmap(1, 1, 32, new int[] { 0x00000000 });

        Datum.PropList props = new Datum.PropList();
        props.add("maskImage", new Datum.ImageRef(mask), true);

        ImageMethodDispatcher.dispatch(new Datum.ImageRef(dest), "copyPixels",
                List.of(new Datum.ImageRef(src), new Datum.Rect(0, 0, 1, 1),
                        new Datum.Rect(0, 0, 1, 1), props));

        assertEquals(0xFF7F7F7F, dest.getPixel(0, 0),
                "Native alpha should take precedence over #maskImage");
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

        assertEquals(0xFF808080, dest.getPixel(0, 0),
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

        assertEquals(0xFFC0C0C0, dest.getPixel(0, 0),
                "copyPixels blend should scale, not replace, the source alpha");
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
}
