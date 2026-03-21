package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.player.input.HitTester;
import com.libreshockwave.player.render.pipeline.FrameSnapshot;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Repro test for the room-entry bug triggered from the navigator.
 *
 * Flow:
 * 1. Wait for navigator to load
 * 2. mouseUp at 425,82
 * 3. tick a bit
 * 4. mouseUp at 635,137
 * 5. tick until the room-entry error appears
 *
 * Usage:
 *   ./gradlew :player-core:runNavigatorWelcomeLobbyErrorTest
 */
public class NavigatorWelcomeLobbyErrorTest {

    private static final int NAV_X = 350, NAV_Y = 60, NAV_W = 370, NAV_H = 440;

    private static final int FIRST_CLICK_X = 425;
    private static final int FIRST_CLICK_Y = 82;
    private static final int ENTER_CLICK_X = 635;
    private static final int ENTER_CLICK_Y = 137;

    private static final int TICKS_AFTER_FIRST_CLICK = 30;
    private static final int TICKS_AFTER_SECOND_CLICK = 3;
    private static final int ERROR_WAIT_TICKS = 450;

    private static final Path OUTPUT_DIR = Path.of("build/navigator-welcome-lobby-error");

    public static void main(String[] args) throws Exception {
        Files.createDirectories(OUTPUT_DIR);
        System.out.println("=== Navigator Welcome Lobby Error Test ===");

        Player player = createPlayer();
        List<String> allErrors = new ArrayList<>();
        List<String> targetErrors = new ArrayList<>();
        player.setErrorListener((msg, ex) -> {
            allErrors.add(msg);
            if (isTargetError(msg)) {
                targetErrors.add(msg);
                System.out.println("[TargetError] " + msg);
            } else if (allErrors.size() <= 20) {
                System.out.println("[Error] " + msg);
            }
        });

        try {
            startAndLoadNavigator(player);

            FrameSnapshot navigatorSnap = player.getFrameSnapshot();
            saveSnapshot(navigatorSnap, "01_navigator_loaded");
            writeClickReport(player, navigatorSnap, OUTPUT_DIR.resolve("01_click_targets.txt"));

            performClick(player, FIRST_CLICK_X, FIRST_CLICK_Y, "02_after_first_click", TICKS_AFTER_FIRST_CLICK);
            performClick(player, ENTER_CLICK_X, ENTER_CLICK_Y, "03_after_enter_click", TICKS_AFTER_SECOND_CLICK);

            waitForRoomEntryError(player, targetErrors);

            FrameSnapshot finalSnap = player.getFrameSnapshot();
            saveSnapshot(finalSnap, targetErrors.isEmpty() ? "04_final_no_target_error" : "04_error_frame");
            NavigatorSSOTest.dumpSpriteInfo(finalSnap, OUTPUT_DIR.resolve("04_sprite_info.txt"));
            Files.writeString(OUTPUT_DIR.resolve("errors.txt"), String.join(System.lineSeparator(), allErrors));

            if (targetErrors.isEmpty()) {
                System.out.println("FAIL: Timed out without hitting the expected room-entry error.");
                System.out.println("Checked for: \"User object not found\", \"No good object:\", \"Failed to define room object\".");
            } else {
                System.out.printf(Locale.ROOT, "Captured %d target error(s).%n", targetErrors.size());
            }

            // Tick extra frames to let the room update cycle fully activate
            System.out.println("Ticking 200 extra frames for room update cycle...");
            tickAndSleep(player, 200);
            FrameSnapshot lateSnap = player.getFrameSnapshot();
            saveSnapshot(lateSnap, "05_late_snapshot");
            NavigatorSSOTest.dumpSpriteInfo(lateSnap, OUTPUT_DIR.resolve("05_late_sprite_info.txt"));
        } finally {
            player.shutdown();
        }

        System.out.println("Output: " + OUTPUT_DIR.toAbsolutePath());
    }

    private static Player createPlayer() throws Exception {
        byte[] dcrBytes = NavigatorSSOTest.httpGet("https://sandbox.h4bbo.net/dcr/14.1_b8/habbo.dcr");
        DirectorFile dirFile = DirectorFile.load(dcrBytes);
        dirFile.setBasePath("https://sandbox.h4bbo.net/dcr/14.1_b8/");
        Player player = new Player(dirFile);

        Map<String, String> params = new LinkedHashMap<>();
        params.put("sw1", "site.url=http://www.habbo.co.uk;url.prefix=http://www.habbo.co.uk");
        params.put("sw2", "connection.info.host=localhost;connection.info.port=30087");
        params.put("sw3", "client.reload.url=https://sandbox.h4bbo.net/");
        params.put("sw4", "connection.mus.host=localhost;connection.mus.port=38101");
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
            } catch (Exception ignored) {
            }
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
            } catch (Exception e) {
                if (tick < 5) {
                    System.out.println("[Tick " + tick + "] " + e.getMessage());
                }
            }

            if (tick % 30 == 0) {
                FrameSnapshot snap = player.getFrameSnapshot();
                boolean hasNavigator = snap.sprites().stream().anyMatch(s ->
                        s.isVisible() && s.getX() >= NAV_X && s.getChannel() >= 60);
                if (hasNavigator) {
                    System.out.printf("Navigator appeared at tick %d (%d sprites)%n",
                            tick, snap.sprites().size());
                    return;
                }
                if (tick % 150 == 0) {
                    System.out.printf("  tick %d, sprites=%d, elapsed=%ds%n",
                            tick, snap.sprites().size(), (System.currentTimeMillis() - startMs) / 1000);
                }
            }

            tick++;
            Thread.sleep(67);
        }

        throw new IllegalStateException("Navigator did not appear within 120 seconds.");
    }

    private static void performClick(Player player, int x, int y, String outputStem, int settleTicks) throws Exception {
        FrameSnapshot beforeSnap = player.getFrameSnapshot();
        saveSnapshot(beforeSnap, outputStem + "_before");

        player.getInputHandler().onMouseMove(x, y);
        tickAndSleep(player, 1);
        player.getInputHandler().onMouseDown(x, y, false);
        tickAndSleep(player, 1);
        player.getInputHandler().onMouseUp(x, y, false);
        tickAndSleep(player, settleTicks);

        FrameSnapshot afterSnap = player.getFrameSnapshot();
        saveSnapshot(afterSnap, outputStem);

        double navChange = NavigatorClickTest.computeRegionChangeFraction(
                beforeSnap.renderFrame().toBufferedImage(),
                afterSnap.renderFrame().toBufferedImage(),
                NAV_X, NAV_Y, NAV_W, NAV_H);
        System.out.printf(Locale.ROOT,
                "click (%d,%d) -> nav change %.2f%%%n", x, y, navChange * 100.0);
    }

    private static void waitForRoomEntryError(Player player, List<String> targetErrors) throws Exception {
        System.out.printf("Ticking up to %d frames waiting for room-entry error...%n", ERROR_WAIT_TICKS);
        for (int i = 0; i < ERROR_WAIT_TICKS; i++) {
            try {
                player.tick();
            } catch (Exception e) {
                System.out.println("[TickLoop " + i + "] " + e.getMessage());
            }
            Thread.sleep(67);

            if (!targetErrors.isEmpty()) {
                System.out.printf("Target error reached at tick +%d, stopping%n", i);
                return;
            }

            if (i % 30 == 0) {
                FrameSnapshot snap = player.getFrameSnapshot();
                System.out.printf("  post-enter tick %d, frame=%d, sprites=%d%n",
                        i, snap.frameNumber(), snap.sprites().size());
            }
        }
    }

    private static void saveSnapshot(FrameSnapshot snap, String stem) throws IOException {
        Bitmap frame = snap.renderFrame();
        NavigatorSSOTest.savePng(frame, OUTPUT_DIR.resolve(stem + ".png"));
        NavigatorSSOTest.dumpSpriteInfo(snap, OUTPUT_DIR.resolve(stem + "_sprite_info.txt"));
    }

    private static void writeClickReport(Player player, FrameSnapshot snap, Path outFile) throws IOException {
        StringBuilder sb = new StringBuilder();
        appendClickInfo(sb, player, snap, FIRST_CLICK_X, FIRST_CLICK_Y, "first");
        appendClickInfo(sb, player, snap, ENTER_CLICK_X, ENTER_CLICK_Y, "enter");
        Files.writeString(outFile, sb.toString());
    }

    private static void appendClickInfo(StringBuilder sb, Player player, FrameSnapshot snap,
                                        int x, int y, String label) {
        int hitChannel = HitTester.hitTest(player.getStageRenderer(), snap.frameNumber(), x, y,
                channel -> player.getEventDispatcher().isSpriteMouseInteractive(channel));
        sb.append(label)
                .append(": point=(").append(x).append(',').append(y).append(')')
                .append(" hitChannel=").append(hitChannel)
                .append(System.lineSeparator());
    }

    private static boolean isTargetError(String msg) {
        return msg.contains("User object not found")
                || msg.contains("No good object:")
                || msg.contains("Failed to define room object");
    }

    private static void tickAndSleep(Player player, int steps) throws InterruptedException {
        for (int i = 0; i < steps; i++) {
            try {
                player.tick();
            } catch (Exception ignored) {
            }
            Thread.sleep(67);
        }
    }
}
