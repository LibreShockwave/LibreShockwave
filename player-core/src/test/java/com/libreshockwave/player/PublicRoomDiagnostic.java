package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.player.input.HitTester;
import com.libreshockwave.player.render.pipeline.FrameSnapshot;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Generic public-room diagnostic runner.
 *
 * Usage:
 *   ./gradlew :player-core:runPublicRoomDiagnostic -Pargs="oriental-tearoom 237 257"
 *   ./gradlew :player-core:runPublicRoomDiagnostic -Pargs="kitchen 217 237"
 */
public final class PublicRoomDiagnostic {

    private static final int NAV_X = 350, NAV_Y = 60;
    private static final int PUBLIC_SPACES_X = 421, PUBLIC_SPACES_Y = 76;
    private static final int LIST_GO_X = 657;
    private static final int GO_BUTTON_X = 671, GO_BUTTON_Y = 441;

    private PublicRoomDiagnostic() {}

    public static void main(String[] args) throws Exception {
        if (args.length < 3) {
            throw new IllegalArgumentException("Usage: <label> <categoryOpenY> <roomGoY>");
        }

        String label = args[0];
        int categoryOpenY = Integer.parseInt(args[1]);
        int roomGoY = Integer.parseInt(args[2]);
        String slug = slugify(label);
        Path outputDir = Path.of("build/public-room-diagnostic", slug);
        Files.createDirectories(outputDir);

        System.out.printf("=== Public Room Diagnostic: %s ===%n", label);

        Player player = createPlayer();
        List<String> allErrors = new ArrayList<>();
        player.setErrorListener((msg, ex) -> {
            allErrors.add(msg);
            if (allErrors.size() <= 50) {
                System.out.println("[Error] " + msg);
            }
        });

        try {
            startAndLoadNavigator(player);

            saveSnapshot(player.getFrameSnapshot(), outputDir, "01_navigator_loaded");

            System.out.printf("%n--- Clicking Public Spaces tab at (%d,%d) ---%n", PUBLIC_SPACES_X, PUBLIC_SPACES_Y);
            performClick(player, PUBLIC_SPACES_X, PUBLIC_SPACES_Y, outputDir, "02_public_spaces", 45);
            saveSnapshot(player.getFrameSnapshot(), outputDir, "02b_public_spaces_list");

            System.out.printf("%n--- Clicking category open button at (%d,%d) ---%n", LIST_GO_X, categoryOpenY);
            performClick(player, LIST_GO_X, categoryOpenY, outputDir, "03_category_open", 45);
            saveSnapshot(player.getFrameSnapshot(), outputDir, "03b_category_expanded");

            System.out.printf("%n--- Clicking room row at (%d,%d) ---%n", LIST_GO_X, roomGoY);
            performClick(player, LIST_GO_X, roomGoY, outputDir, "04_room_row", 30);
            saveSnapshot(player.getFrameSnapshot(), outputDir, "04b_after_room_row");

            System.out.printf("%n--- Clicking bottom panel Go button at (%d,%d) ---%n", GO_BUTTON_X, GO_BUTTON_Y);
            performClick(player, GO_BUTTON_X, GO_BUTTON_Y, outputDir, "05_panel_go", 30);

            System.out.println("\nWaiting for room to load (ticking 600 frames ~40s)...");
            for (int i = 0; i < 600; i++) {
                try {
                    player.tick();
                } catch (Exception ignored) {}
                Thread.sleep(67);
                if (i % 60 == 0) {
                    FrameSnapshot snap = player.getFrameSnapshot();
                    System.out.printf("  room load tick %d, sprites=%d%n", i, snap.sprites().size());
                }
            }

            FrameSnapshot loaded = player.getFrameSnapshot();
            saveSnapshot(loaded, outputDir, "06_room_loaded");

            System.out.println("\nTicking 200 extra frames for room to settle...");
            tickAndSleep(player, 200);

            FrameSnapshot settled = player.getFrameSnapshot();
            saveSnapshot(settled, outputDir, "07_room_settled");
            dumpInterestingSprites(settled, outputDir);
            Files.writeString(outputDir.resolve("errors.txt"), String.join(System.lineSeparator(), allErrors));
        } finally {
            player.shutdown();
        }

        System.out.println("Output: " + outputDir.toAbsolutePath());
    }

    private static String slugify(String label) {
        return label.toLowerCase(Locale.ROOT).replaceAll("[^a-z0-9]+", "-").replaceAll("(^-|-$)", "");
    }

    private static Player createPlayer() throws Exception {
        byte[] dcrBytes = NavigatorSSOTest.httpGet("https://sandbox.h4bbo.net/dcr/14.1_b8/habbo.dcr");
        DirectorFile dirFile = DirectorFile.load(dcrBytes);
        dirFile.setBasePath("https://sandbox.h4bbo.net/dcr/14.1_b8/");
        Player player = new Player(dirFile);

        Map<String, String> params = new LinkedHashMap<>();
        params.put("sw1", "site.url=http://www.habbo.co.uk;url.prefix=http://www.habbo.co.uk");
        params.put("sw2", "connection.info.host=au.h4bbo.net;connection.info.port=30101");
        params.put("sw3", "client.reload.url=https://sandbox.h4bbo.net/");
        params.put("sw4", "connection.mus.host=au.h4bbo.net;connection.mus.port=38101");
        params.put("sw5", "external.variables.txt=https://sandbox.h4bbo.net/gamedata/external_variables.txt;"
                + "external.texts.txt=https://sandbox.h4bbo.net/gamedata/external_texts.txt");
        params.put("sw6", "use.sso.ticket=1;sso.ticket=123");
        player.setExternalParams(params);
        return player;
    }

    private static void startAndLoadNavigator(Player player) throws Exception {
        player.play();
        player.preloadAllCasts();
        System.out.println("Waiting 8000ms for casts to load...");
        Thread.sleep(8000);

        System.out.println("Loading hotel view...");
        long loadStart = System.currentTimeMillis();
        for (int i = 0; i < 3000 && (System.currentTimeMillis() - loadStart) < 60_000; i++) {
            try {
                player.tick();
            } catch (Exception ignored) {}
            Thread.sleep(10);
            if (i % 50 == 0) {
                int spriteCount = player.getFrameSnapshot().sprites().size();
                System.out.printf("  startup tick %d, sprites=%d, elapsed=%ds%n",
                        i, spriteCount, (System.currentTimeMillis() - loadStart) / 1000);
                if (spriteCount > 20 && i > 100) {
                    System.out.printf("  Hotel view loaded at tick %d (%d sprites)%n", i, spriteCount);
                    tickAndSleep(player, 200);
                    break;
                }
            }
        }

        System.out.println("Waiting for navigator...");
        long startMs = System.currentTimeMillis();
        int tick = 0;
        while (System.currentTimeMillis() - startMs < 120_000) {
            try {
                player.tick();
            } catch (Exception ignored) {}

            if (tick % 30 == 0) {
                FrameSnapshot snap = player.getFrameSnapshot();
                boolean hasNavigator = snap.sprites().stream().anyMatch(s ->
                        s.isVisible() && s.getX() >= NAV_X && s.getChannel() >= 60);
                if (hasNavigator) {
                    System.out.printf("Navigator appeared at tick %d (%d sprites)%n",
                            tick, snap.sprites().size());
                    return;
                }
            }
            tick++;
            Thread.sleep(67);
        }

        throw new IllegalStateException("Navigator did not appear within 120 seconds.");
    }

    private static void performClick(Player player, int x, int y, Path outputDir, String outputStem, int settleTicks)
            throws Exception {
        FrameSnapshot beforeSnap = player.getFrameSnapshot();
        saveSnapshot(beforeSnap, outputDir, outputStem + "_before");

        int hitChannel = HitTester.hitTest(player.getStageRenderer(), beforeSnap.frameNumber(), x, y,
                channel -> player.getEventDispatcher().isSpriteMouseInteractive(channel));
        System.out.printf("  hitTest(%d,%d) -> channel %d%n", x, y, hitChannel);

        player.getInputHandler().onMouseMove(x, y);
        tickAndSleep(player, 1);
        player.getInputHandler().onMouseDown(x, y, false);
        tickAndSleep(player, 1);
        player.getInputHandler().onMouseUp(x, y, false);
        tickAndSleep(player, settleTicks);

        FrameSnapshot afterSnap = player.getFrameSnapshot();
        saveSnapshot(afterSnap, outputDir, outputStem);

        double change = NavigatorClickTest.computeRegionChangeFraction(
                beforeSnap.renderFrame().toBufferedImage(),
                afterSnap.renderFrame().toBufferedImage(),
                0, 0,
                beforeSnap.renderFrame().getWidth(),
                beforeSnap.renderFrame().getHeight());
        System.out.printf(Locale.ROOT, "  click (%d,%d) -> screen change %.2f%%%n", x, y, change * 100.0);
    }

    private static void saveSnapshot(FrameSnapshot snapshot, Path outputDir, String stem) throws Exception {
        Bitmap bitmap = snapshot.renderFrame();
        NavigatorSSOTest.savePng(bitmap, outputDir.resolve(stem + ".png"));
        NavigatorSSOTest.dumpSpriteInfo(snapshot, outputDir.resolve(stem + "_sprite_info.txt"));
    }

    private static void tickAndSleep(Player player, int ticks) throws Exception {
        for (int i = 0; i < ticks; i++) {
            try {
                player.tick();
            } catch (Exception ignored) {}
            Thread.sleep(67);
        }
    }

    private static void dumpInterestingSprites(FrameSnapshot snapshot, Path outputDir) throws Exception {
        StringBuilder sb = new StringBuilder();
        sb.append("=== Interesting Room Sprites ===\n");
        for (var sprite : snapshot.sprites()) {
            if (!sprite.isVisible()) {
                continue;
            }
            String name = sprite.getMemberName();
            if (name == null) {
                name = "(null)";
            }
            String lower = name.toLowerCase(Locale.ROOT);
            if (!lower.contains("mask")
                    && !lower.contains("door")
                    && !lower.contains("entry")
                    && !lower.contains("canvas")
                    && !lower.contains("shadow")) {
                continue;
            }
            int centerX = Math.max(0, sprite.getWidth() / 2);
            int centerY = Math.max(0, sprite.getHeight() / 2);
            int center = sprite.getBakedBitmap() != null
                    && centerX < sprite.getBakedBitmap().getWidth()
                    && centerY < sprite.getBakedBitmap().getHeight()
                    ? sprite.getBakedBitmap().getPixel(centerX, centerY)
                    : 0;
            sb.append(String.format(Locale.ROOT,
                    "ch=%d pos=(%d,%d) size=(%dx%d) ink=%d blend=%d member='%s' center=0x%08X%n",
                    sprite.getChannel(),
                    sprite.getX(),
                    sprite.getY(),
                    sprite.getWidth(),
                    sprite.getHeight(),
                    sprite.getInk(),
                    sprite.getBlend(),
                    name,
                    center));
        }
        Files.writeString(outputDir.resolve("interesting_sprites.txt"), sb.toString());
    }
}
