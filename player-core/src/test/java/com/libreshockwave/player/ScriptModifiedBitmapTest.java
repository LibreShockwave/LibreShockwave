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
                "Transparent source background should not be remapped into an opaque bgColor fill");
        assertEquals(0xFF000000, dest.getPixel(3, 3),
                "Black text pixels should still copy through");
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
                "Non-background-color pixel should remain unchanged");
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
}
