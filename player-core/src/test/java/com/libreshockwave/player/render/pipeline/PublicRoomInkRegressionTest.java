package com.libreshockwave.player.render.pipeline;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.id.InkMode;
import com.libreshockwave.player.cast.CastLib;
import com.libreshockwave.player.cast.CastLibManager;
import com.libreshockwave.player.cast.CastMember;
import com.libreshockwave.chunks.CastMemberChunk;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertSame;

class PublicRoomInkRegressionTest {

    private static final Path POOL_CAST =
            Path.of("temp/habbo_origins_src/archive/unprotected_client_dump_20_06_2024/hh_room_pool.cst");

    @Test
    void poolDoorMaskUsesMatteTransparencyWhenAssignedAsDynamicMember() throws Exception {
        if (!Files.isRegularFile(POOL_CAST)) {
            return;
        }

        CastLib castLib = loadCast(POOL_CAST);
        CastMember doorMask = castLib.getMemberByName("ovimaski");
        assertNotNull(doorMask);

        Bitmap raw = doorMask.getBitmap();
        assertNotNull(raw);

        RenderSprite sprite = new RenderSprite(
                1, 0, 0, raw.getWidth(), raw.getHeight(), 0, true,
                RenderSprite.SpriteType.BITMAP,
                null, doorMask,
                0, 0xFFFFFF, false, false,
                InkMode.MATTE.code(), 100,
                false, false, null, false
        );

        Bitmap baked = new SpriteBaker(new BitmapCache(), null, null)
                .bake(sprite)
                .getBakedBitmap();

        assertNotNull(baked);
        // Sample point inside the doorway opening. This room member is authored as a
        // white matte mask, so the opening must disappear after Matte ink processing.
        assertEquals(0x00000000, baked.getPixel(40, 45));
    }

    @Test
    void poolWaterDynamicCanvasStillUsesMaskInkAfterScriptFill() throws Exception {
        if (!Files.isRegularFile(POOL_CAST)) {
            return;
        }

        CastLib castLib = loadCast(POOL_CAST);
        CastMember water = castLib.getMemberByName("vesi2");
        assertNotNull(water);

        Bitmap live = water.getBitmap();
        assertNotNull(live);
        live.fill(0xFF009999);
        live.markScriptModified();

        RenderSprite sprite = new RenderSprite(
                1, 0, 0, live.getWidth(), live.getHeight(), 0, true,
                RenderSprite.SpriteType.BITMAP,
                null, water,
                0, 0xFFFFFF, false, false,
                InkMode.MASK.code(), 60,
                false, false, null, false
        );

        Bitmap baked = new SpriteBaker(new BitmapCache(), null, null)
                .bake(sprite)
                .getBakedBitmap();

        assertNotNull(baked);
        assertEquals(0x6B009999, baked.getPixel(10, 10));
    }

    @Test
    void poolWaterScoreSpriteUsesRuntimeMemberBitmapAfterScriptFill() throws Exception {
        if (!Files.isRegularFile(POOL_CAST)) {
            return;
        }

        DirectorFile file = DirectorFile.load(POOL_CAST);
        CastLibManager castLibManager = new CastLibManager(file, null);
        CastLib castLib = castLibManager.getCastLib(1);
        assertNotNull(castLib);

        CastMemberChunk waterChunk = file.getCastMembers().stream()
                .filter(member -> "vesi2".equalsIgnoreCase(member.name()))
                .findFirst()
                .orElse(null);
        assertNotNull(waterChunk);

        CastMember water = castLib.getMemberByName("vesi2");
        assertNotNull(water);
        assertSame(water, castLibManager.findRuntimeMember(waterChunk));

        Bitmap live = water.getBitmap();
        assertNotNull(live);
        live.fill(0xFF009999);
        live.markScriptModified();

        RenderSprite sprite = new RenderSprite(
                1, 0, 0, live.getWidth(), live.getHeight(), 0, true,
                RenderSprite.SpriteType.BITMAP,
                waterChunk, null,
                0, 0xFFFFFF, false, false,
                InkMode.MASK.code(), 60,
                false, false, null, false
        );

        Bitmap baked = new SpriteBaker(new BitmapCache(), castLibManager, null)
                .bake(sprite)
                .getBakedBitmap();

        assertNotNull(baked);
        assertEquals(0x6B009999, baked.getPixel(10, 10));
    }

    private static CastLib loadCast(Path castPath) throws Exception {
        DirectorFile file = DirectorFile.load(castPath);
        CastLib castLib = new CastLib(1, null, null);
        castLib.setSourceFile(file);
        castLib.load();
        return castLib;
    }
}
