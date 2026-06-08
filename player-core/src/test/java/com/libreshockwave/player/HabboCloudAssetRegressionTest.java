package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.Drawing;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.id.InkMode;
import com.libreshockwave.player.cast.CastLib;
import com.libreshockwave.player.render.pipeline.InkProcessor;
import org.junit.jupiter.api.Test;

import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.TreeMap;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class HabboCloudAssetRegressionTest {

    private static final Path ENTRY_CAST =
            Path.of("/opt/git/Kepler-www/dcr/14.1_b8/hh_entry_us.cct");
    private static final Path BALLOON_MIDDLE_PNG =
            Path.of("/opt/git/v31_assets/hh_interface/bitmaps/02102_balloon.middle.png");
    private static final Path BALLOON_LEFT_PNG =
            Path.of("/opt/git/v31_assets/hh_interface/bitmaps/02103_balloon.left.png");
    private static final Path BALLOON_PULSE_PNG =
            Path.of("/opt/git/v31_assets/hh_interface/bitmaps/02101_balloon.pulse.png");
    private static final Path CHAT_BUBBLE_RIGHT_PNG =
            Path.of("/opt/git/v31_assets/hh_interface/bitmaps/01448_chat_bubble_right.png");
    private static final Path CHAT_BUBBLE_LEFT_PNG =
            Path.of("/opt/git/v31_assets/hh_interface/bitmaps/01503_chat_bubble_left.png");
    private static final Path CHAT_BUBBLE_MIDDLE_PNG =
            Path.of("/opt/git/v31_assets/hh_interface/bitmaps/02090_chat_bubble_middle.png");
    private static final Path CHAT_BUBBLE_POINTER_PNG =
            Path.of("/opt/git/v31_assets/hh_interface/bitmaps/01993_chat_bubble_pointer.png");

    @Test
    void v31CloudAssetRetainsOpaqueWhiteBodyAfterMatte() throws Exception {
        if (!Files.isRegularFile(ENTRY_CAST)) {
            return;
        }

        DirectorFile file = DirectorFile.load(ENTRY_CAST);
        CastLib castLib = new CastLib(1, null, null);
        castLib.setSourceFile(file);
        castLib.load();

        CastMemberChunk member = file.getCastMembers().stream()
                .filter(m -> "cloud_1_left".equalsIgnoreCase(m.name()))
                .findFirst()
                .orElse(null);
        assertNotNull(member);

        Bitmap bmp = castLib.getMemberByName(member.name()).getBitmap();
        assertNotNull(bmp);

        Bitmap inked = InkProcessor.applyInk(bmp, InkMode.MATTE.code(), 0, false, bmp.getImagePalette());

        assertTrue(countOpaqueWhite(bmp) > 0, "cloud asset must start with authored white body pixels");
        assertTrue(countOpaqueWhite(inked) > 0,
                "MATTE should preserve some opaque white cloud body pixels for v31 cloud assets");
    }

    @Test
    void v31BalloonMiddleRetainsOpaqueWhiteBodyAfterMatte() throws Exception {
        assertBalloonBodySurvivesMatte(BALLOON_MIDDLE_PNG);
        assertBalloonBodySurvivesMatte(BALLOON_LEFT_PNG);
        assertBalloonBodySurvivesMatte(BALLOON_PULSE_PNG);
    }

    @Test
    void v31ScriptBuiltRoomChatBackgroundRetainsBodyAndTextAfterMatte() throws Exception {
        if (!Files.isRegularFile(CHAT_BUBBLE_LEFT_PNG)
                || !Files.isRegularFile(CHAT_BUBBLE_MIDDLE_PNG)
                || !Files.isRegularFile(CHAT_BUBBLE_RIGHT_PNG)
                || !Files.isRegularFile(CHAT_BUBBLE_POINTER_PNG)) {
            return;
        }

        Bitmap left = readPngAsBitmap(CHAT_BUBBLE_LEFT_PNG);
        Bitmap middle = readPngAsBitmap(CHAT_BUBBLE_MIDDLE_PNG);
        Bitmap right = readPngAsBitmap(CHAT_BUBBLE_RIGHT_PNG);
        Bitmap pointer = readPngAsBitmap(CHAT_BUBBLE_POINTER_PNG);
        int bodyHeight = left.getHeight();
        int width = left.getWidth() + 48 + right.getWidth();
        int height = bodyHeight + pointer.getHeight();
        Bitmap background = new Bitmap(width, height, 32);
        background.fill(0xFFFFFFFF);

        copy(background, left, 0, 0);
        for (int x = left.getWidth(); x < width - right.getWidth(); x++) {
            copy(background, middle, x, 0);
        }
        copy(background, right, width - right.getWidth(), 0);

        Bitmap text = new Bitmap(18, 5, 32);
        text.fill(0xFFFFFFFF);
        for (int x = 2; x <= 6; x++) {
            text.setPixel(x, 1, 0xFF000000);
        }
        text.setPixel(2, 2, 0xFF000000);
        text.setPixel(6, 2, 0xFF000000);
        for (int x = 10; x <= 14; x++) {
            text.setPixel(x, 3, 0xFF000000);
        }
        int textX = left.getWidth() + 5;
        int textY = 7;
        copy(background, text, textX, textY);
        copy(background, pointer, 12, bodyHeight);
        background.markScriptModified();

        Bitmap inked = InkProcessor.applyInkPreservingOutlinedWhiteBody(background,
                InkMode.MATTE.code(), 0, false, null, false);

        assertTrue(countOpaqueWhite(background) > 0, "script-built chat background starts with white body pixels");
        assertTrue(countOpaqueWhite(inked) > 0, "MATTE should preserve the white room chat bubble body");
        assertTrue(countTransparent(inked) > 0, "MATTE should still clear the white canvas outside the bubble");
        assertEquals(0xFF000000, inked.getPixel(textX + 2, textY + 1),
                "MATTE should preserve black text copied onto the script-built bubble");
    }

    @Test
    void dumpCloudBitmapPaletteDiagnostics() throws Exception {
        if (!Files.isRegularFile(ENTRY_CAST)) {
            return;
        }

        DirectorFile file = DirectorFile.load(ENTRY_CAST);
        CastLib castLib = new CastLib(1, null, null);
        castLib.setSourceFile(file);
        castLib.load();

        for (CastMemberChunk member : file.getCastMembers()) {
            String name = member.name();
            if (name == null || !name.toLowerCase().contains("cloud")) {
                continue;
            }
            Bitmap bmp = castLib.getMemberByName(name).getBitmap();
            if (bmp == null) {
                System.out.println("cloud member " + name + " has null bitmap");
                continue;
            }
            System.out.println("cloud member " + member.id().value() + " " + name
                    + " size=" + bmp.getWidth() + "x" + bmp.getHeight()
                    + " depth=" + bmp.getBitDepth());
            dumpIndexStats(bmp);
        }
    }

    private static int countOpaqueWhite(Bitmap bmp) {
        int count = 0;
        for (int pixel : bmp.getPixels()) {
            if (((pixel >>> 24) & 0xFF) != 0 && (pixel & 0xFFFFFF) == 0xFFFFFF) {
                count++;
            }
        }
        return count;
    }

    private static int countTransparent(Bitmap bmp) {
        int count = 0;
        for (int pixel : bmp.getPixels()) {
            if (((pixel >>> 24) & 0xFF) == 0) {
                count++;
            }
        }
        return count;
    }

    private static void copy(Bitmap dest, Bitmap src, int x, int y) {
        Drawing.copyPixels(dest, src, x, y, 0, 0, src.getWidth(), src.getHeight(),
                com.libreshockwave.bitmap.Palette.InkMode.COPY, 255);
    }

    private static Bitmap readPngAsBitmap(Path path) throws Exception {
        BufferedImage image = ImageIO.read(path.toFile());
        assertNotNull(image);
        int w = image.getWidth();
        int h = image.getHeight();
        int[] pixels = new int[w * h];
        image.getRGB(0, 0, w, h, pixels, 0, w);
        return new Bitmap(w, h, 8, pixels);
    }

    private static void assertBalloonBodySurvivesMatte(Path path) throws Exception {
        if (!Files.isRegularFile(path)) {
            return;
        }

        Bitmap bmp = readPngAsBitmap(path);
        Bitmap inked = InkProcessor.applyInk(bmp, InkMode.MATTE.code(), 0, false, bmp.getImagePalette());

        assertTrue(countOpaqueWhite(bmp) > 0, path.getFileName() + " must start with authored white body pixels");
        assertTrue(countOpaqueWhite(inked) > 0,
                path.getFileName() + " should preserve opaque white speech balloon body pixels under MATTE");
    }

    private static void dumpIndexStats(Bitmap bmp) {
        byte[] paletteIndices = bmp.getPaletteIndices();
        if (paletteIndices == null) {
            return;
        }
        Map<Integer, Integer> counts = new TreeMap<>();
        Map<Integer, Integer> rgbByIndex = new TreeMap<>();
        int w = bmp.getWidth();
        for (int i = 0; i < paletteIndices.length; i++) {
            int index = paletteIndices[i] & 0xFF;
            counts.merge(index, 1, Integer::sum);
            rgbByIndex.putIfAbsent(index, bmp.getPixels()[i] & 0xFFFFFF);
        }
        for (Map.Entry<Integer, Integer> entry : counts.entrySet()) {
            int index = entry.getKey();
            int count = entry.getValue();
            int rgb = rgbByIndex.get(index);
            System.out.printf("  idx=%3d rgb=%06X count=%d%n", index, rgb, count);
        }

        int[] samples = {
                0,
                Math.min(paletteIndices.length - 1, 1),
                Math.min(paletteIndices.length - 1, w - 1),
                Math.min(paletteIndices.length - 1, w),
                Math.min(paletteIndices.length - 1, (bmp.getHeight() / 2) * w + (bmp.getWidth() / 2))
        };
        for (int sample : samples) {
            System.out.printf("  sample[%d]=idx=%d rgb=%06X%n",
                    sample,
                    paletteIndices[sample] & 0xFF,
                    bmp.getPixels()[sample] & 0xFFFFFF);
        }
    }
}
