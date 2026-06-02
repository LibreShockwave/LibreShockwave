package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.id.InkMode;
import com.libreshockwave.player.cast.CastLib;
import com.libreshockwave.player.render.pipeline.InkProcessor;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.TreeMap;

import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class HabboCloudAssetRegressionTest {

    private static final Path ENTRY_CAST =
            Path.of("/opt/git/Kepler-www/dcr/14.1_b8/hh_entry_us.cct");

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
