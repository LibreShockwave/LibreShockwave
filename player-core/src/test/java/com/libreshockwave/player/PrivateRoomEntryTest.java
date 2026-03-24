package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.player.render.pipeline.FrameSnapshot;
import com.libreshockwave.player.render.pipeline.RenderSprite;

import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;

/**
 * Private room entry test: SSO login, navigate to "Own Room(s)", click "Go" on
 * room 123456789, wait for the room to load, render to PNG, and pixel diff
 * against reference.
 *
 * Reference image:
 *   C:/Users/alexm/Documents/ShareX/Screenshots/2026-03/room-private-reference.png
 *
 * Requires: Habbo game server on localhost:30087 and MUS on localhost:38101.
 *
 * Usage:
 *   ./gradlew :player-core:runPrivateRoomEntryTest
 */
public class PrivateRoomEntryTest {

    private static final String DCR_URL = "https://sandbox.h4bbo.net/dcr/14.1_b8/habbo.dcr";

    // Navigator region (right side of stage)
    private static final int NAV_X = 350, NAV_Y = 60, NAV_W = 370, NAV_H = 440;

    // Click coordinates
    // Ch81 "Own Room(s)" tab: pos=(469,106) 115x24 -> center (526, 118)
    private static final int OWN_ROOMS_TAB_X = 526;
    private static final int OWN_ROOMS_TAB_Y = 118;
    // "Go" button for room 123456789 in the "Own Room(s)" list
    private static final int GO_BUTTON_X = 668;
    private static final int GO_BUTTON_Y = 233;

    // Timing
    private static final int TICK_DELAY_MS = 67;        // ~15fps
    private static final int OWN_ROOMS_SETTLE_TICKS = 45;
    private static final int GO_SETTLE_TICKS = 30;
    private static final int ROOM_LOAD_TICKS = 700;     // ~47 seconds

    private static final Path OUTPUT_DIR = Path.of("build/private-room-entry");

    private static final String REFERENCE_IMAGE =
            "C:/Users/alexm/Documents/ShareX/Screenshots/2026-03/room-private-reference.png";

    public static void main(String[] args) throws Exception {
        Files.createDirectories(OUTPUT_DIR);
        System.out.println("=== Private Room Entry Test ===");

        Player player = createPlayer();
        List<String> errors = new ArrayList<>();
        player.setErrorListener((msg, ex) -> {
            errors.add(msg);
            if (errors.size() <= 30) System.out.println("[Error] " + msg);
        });

        try {
            // 1. Load navigator
            startAndLoadNavigator(player);
            FrameSnapshot navSnap = player.getFrameSnapshot();
            saveSnapshot(navSnap, "01_navigator_loaded");
            System.out.printf("Navigator loaded: %d sprites, bgColor=0x%06X%n",
                    navSnap.sprites().size(),
                    player.getStageRenderer().getBackgroundColor() & 0xFFFFFF);

            // 2. Click "Own Room(s)" tab
            System.out.println("\n--- Clicking 'Own Room(s)' tab ---");
            performClick(player, OWN_ROOMS_TAB_X, OWN_ROOMS_TAB_Y,
                    "02_own_rooms_tab", OWN_ROOMS_SETTLE_TICKS);

            // 3. Click "Go" button for room 123456789
            System.out.println("\n--- Clicking 'Go' button ---");
            performClick(player, GO_BUTTON_X, GO_BUTTON_Y,
                    "03_go_button", GO_SETTLE_TICKS);

            // 4. Wait for room to load
            System.out.printf("Ticking %d frames for room to load...%n", ROOM_LOAD_TICKS);
            waitForRoomLoad(player);

            // 5. Capture final room state
            FrameSnapshot roomSnap = player.getFrameSnapshot();
            saveSnapshot(roomSnap, "04_room_loaded");

            // Diagnostic: log background color and stage image state
            int bgColor = player.getStageRenderer().getBackgroundColor();
            Bitmap stageImage = player.getStageRenderer().getStageImage();
            System.out.printf("\nRoom state: bgColor=0x%06X, stageImage=%s, sprites=%d%n",
                    bgColor & 0xFFFFFF,
                    stageImage != null ? stageImage.getWidth() + "x" + stageImage.getHeight() : "null",
                    roomSnap.sprites().size());

            // Diagnostic: dump ink/color info for visible sprites
            dumpInkDiagnostics(roomSnap);

            // Diagnostic: save stageImage and raw room bitmap (Ch0) before ink processing
            if (stageImage != null) {
                NavigatorSSOTest.savePng(stageImage, OUTPUT_DIR.resolve("diag_stageImage.png"));
                System.out.println("Saved stageImage to diag_stageImage.png");
            }
            for (RenderSprite s : roomSnap.sprites()) {
                if (s.isVisible() && s.getDynamicMember() != null && s.getWidth() >= 400) {
                    Bitmap rawBmp = s.getDynamicMember().getBitmap();
                    if (rawBmp != null) {
                        NavigatorSSOTest.savePng(rawBmp, OUTPUT_DIR.resolve(
                                "diag_raw_ch" + s.getChannel() + "_" + rawBmp.getWidth() + "x" + rawBmp.getHeight() + ".png"));
                        System.out.printf("Saved raw bitmap for Ch%d: %dx%d bitDepth=%d scriptModified=%s%n",
                                s.getChannel(), rawBmp.getWidth(), rawBmp.getHeight(),
                                rawBmp.getBitDepth(), rawBmp.isScriptModified());
                    }
                }
            }

            // 6. Pixel diff against reference
            BufferedImage roomImage = roomSnap.renderFrame().toBufferedImage();
            BufferedImage refImage = loadReference();

            if (refImage != null) {
                BufferedImage diff = createPixelDiff(roomImage, refImage);
                ImageIO.write(diff, "PNG", OUTPUT_DIR.resolve("05_diff_vs_reference.png").toFile());

                BufferedImage sbs = createSideBySide(roomImage, refImage, diff);
                ImageIO.write(sbs, "PNG", OUTPUT_DIR.resolve("06_side_by_side.png").toFile());

                analyzePixelDiffs(roomImage, refImage, "Room vs Reference");
            } else {
                System.out.println("Reference image not found: " + REFERENCE_IMAGE);
            }

            // 7. Verdict
            System.out.println("\n=== VERDICT ===");
            boolean looksBlack = isRenderMostlyBlack(roomImage);
            if (looksBlack) {
                System.out.println("FAIL: Room appears mostly black — rendering bug present");
            } else {
                System.out.println("PASS: Room has non-black content");
            }

            if (!errors.isEmpty()) {
                System.out.println("\nTotal errors: " + errors.size());
                Files.writeString(OUTPUT_DIR.resolve("errors.txt"),
                        String.join(System.lineSeparator(), errors));
            }
        } finally {
            player.shutdown();
        }

        System.out.println("\n=== Test complete ===");
        System.out.println("Output: " + OUTPUT_DIR.toAbsolutePath());
    }

    // ---- Setup ----

    private static Player createPlayer() throws Exception {
        byte[] dcrBytes = NavigatorSSOTest.httpGet(DCR_URL);
        System.out.printf("Downloaded DCR: %d bytes%n", dcrBytes.length);

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

    // ---- Navigator loading (same pattern as NavigatorWelcomeLobbyErrorTest) ----

    private static void startAndLoadNavigator(Player player) throws Exception {
        player.play();
        player.preloadAllCasts();
        System.out.println("Waiting 8000ms for casts to load...");
        Thread.sleep(8000);

        System.out.println("Loading hotel view...");
        long loadStart = System.currentTimeMillis();
        for (int i = 0; i < 3000 && (System.currentTimeMillis() - loadStart) < 60_000; i++) {
            try { player.tick(); } catch (Exception ignored) { }
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
            try { player.tick(); } catch (Exception e) {
                if (tick < 5) System.out.println("[Tick " + tick + "] " + e.getMessage());
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
            Thread.sleep(TICK_DELAY_MS);
        }

        throw new IllegalStateException("Navigator did not appear within 120 seconds.");
    }

    // ---- Click handling ----

    private static void performClick(Player player, int x, int y, String outputStem,
                                     int settleTicks) throws Exception {
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

    // ---- Room loading ----

    private static void waitForRoomLoad(Player player) throws Exception {
        for (int i = 0; i < ROOM_LOAD_TICKS; i++) {
            try { player.tick(); } catch (Exception e) {
                if (i < 10) System.out.println("[RoomLoad " + i + "] " + e.getMessage());
            }
            Thread.sleep(TICK_DELAY_MS);

            if (i % 60 == 0) {
                FrameSnapshot snap = player.getFrameSnapshot();
                int bgColor = player.getStageRenderer().getBackgroundColor();
                System.out.printf("  room tick %d, frame=%d, sprites=%d, bgColor=0x%06X%n",
                        i, snap.frameNumber(), snap.sprites().size(), bgColor & 0xFFFFFF);
            }

            // Periodic snapshots during loading
            if (i == 100 || i == 300 || i == 500) {
                saveSnapshot(player.getFrameSnapshot(), "load_tick_" + i);
            }
        }
    }

    // ---- Diagnostics ----

    private static void dumpInkDiagnostics(FrameSnapshot snap) {
        System.out.println("\n--- Ink/Color Diagnostics ---");
        int bgTransCount = 0;
        for (RenderSprite s : snap.sprites()) {
            if (!s.isVisible()) continue;
            String inkName = s.getInkMode().toString();

            // Log sprites with BACKGROUND_TRANSPARENT ink or notable foreColor/backColor
            if (s.getInkMode() == com.libreshockwave.id.InkMode.BACKGROUND_TRANSPARENT
                    || s.hasForeColor() || s.hasBackColor()) {
                System.out.printf("  Ch%-3d %-8s %dx%d ink=%-20s fg=0x%06X bg=0x%06X hasFg=%s hasBg=%s baked=%s%n",
                        s.getChannel(), s.getType(),
                        s.getWidth(), s.getHeight(),
                        inkName,
                        s.getForeColor() & 0xFFFFFF, s.getBackColor() & 0xFFFFFF,
                        s.hasForeColor(), s.hasBackColor(),
                        s.getBakedBitmap() != null
                                ? s.getBakedBitmap().getWidth() + "x" + s.getBakedBitmap().getHeight()
                                : "null");
                if (s.getInkMode() == com.libreshockwave.id.InkMode.BACKGROUND_TRANSPARENT) {
                    bgTransCount++;
                }
            }
        }
        System.out.println("BACKGROUND_TRANSPARENT sprites: " + bgTransCount);
    }

    private static boolean isRenderMostlyBlack(BufferedImage img) {
        // Sample center region of the room area (skip UI bars)
        int sampleX = 100, sampleY = 100, sampleW = 400, sampleH = 300;
        int total = 0, blackish = 0;
        for (int y = sampleY; y < Math.min(sampleY + sampleH, img.getHeight()); y += 3) {
            for (int x = sampleX; x < Math.min(sampleX + sampleW, img.getWidth()); x += 3) {
                total++;
                int px = img.getRGB(x, y);
                int r = (px >> 16) & 0xFF;
                int g = (px >> 8) & 0xFF;
                int b = px & 0xFF;
                if (r < 20 && g < 20 && b < 20) blackish++;
            }
        }
        double blackFraction = total == 0 ? 0 : (double) blackish / total;
        System.out.printf("Black pixel fraction (center region): %.1f%%%n", blackFraction * 100.0);
        return blackFraction > 0.7;
    }

    // ---- Snapshot helpers ----

    private static void saveSnapshot(FrameSnapshot snap, String stem) throws IOException {
        Bitmap frame = snap.renderFrame();
        NavigatorSSOTest.savePng(frame, OUTPUT_DIR.resolve(stem + ".png"));
        NavigatorSSOTest.dumpSpriteInfo(snap, OUTPUT_DIR.resolve(stem + "_sprite_info.txt"));
    }

    private static void tickAndSleep(Player player, int steps) throws InterruptedException {
        for (int i = 0; i < steps; i++) {
            try { player.tick(); } catch (Exception ignored) { }
            Thread.sleep(TICK_DELAY_MS);
        }
    }

    // ---- Image utilities ----

    private static BufferedImage loadReference() {
        File f = new File(REFERENCE_IMAGE);
        if (f.exists()) {
            try { return ImageIO.read(f); } catch (IOException e) { /* fall through */ }
        }
        return null;
    }

    static BufferedImage createPixelDiff(BufferedImage a, BufferedImage b) {
        int w = Math.min(a.getWidth(), b.getWidth());
        int h = Math.min(a.getHeight(), b.getHeight());
        BufferedImage d = new BufferedImage(w, h, BufferedImage.TYPE_INT_ARGB);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int pa = a.getRGB(x, y), pb = b.getRGB(x, y);
                if (pa == pb) {
                    d.setRGB(x, y, 0xFF000000);
                } else {
                    int dr = Math.min(255, Math.abs(((pa >> 16) & 0xFF) - ((pb >> 16) & 0xFF)) * 4);
                    int dg = Math.min(255, Math.abs(((pa >> 8) & 0xFF) - ((pb >> 8) & 0xFF)) * 4);
                    int db = Math.min(255, Math.abs((pa & 0xFF) - (pb & 0xFF)) * 4);
                    d.setRGB(x, y, 0xFF000000 | (dr << 16) | (dg << 8) | db);
                }
            }
        }
        return d;
    }

    static BufferedImage createSideBySide(BufferedImage a, BufferedImage b, BufferedImage diff) {
        int h = Math.max(a.getHeight(), Math.max(b.getHeight(), diff.getHeight()));
        BufferedImage r = new BufferedImage(a.getWidth() + b.getWidth() + diff.getWidth() + 4, h,
                BufferedImage.TYPE_INT_ARGB);
        var g = r.createGraphics();
        g.drawImage(a, 0, 0, null);
        g.drawImage(b, a.getWidth() + 2, 0, null);
        g.drawImage(diff, a.getWidth() + b.getWidth() + 4, 0, null);
        g.dispose();
        return r;
    }

    static void analyzePixelDiffs(BufferedImage a, BufferedImage b, String label) {
        int w = Math.min(a.getWidth(), b.getWidth());
        int h = Math.min(a.getHeight(), b.getHeight());
        int total = w * h, identical = 0, close = 0, different = 0;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int pa = a.getRGB(x, y), pb = b.getRGB(x, y);
                if (pa == pb) {
                    identical++;
                } else {
                    int dr = Math.abs(((pa >> 16) & 0xFF) - ((pb >> 16) & 0xFF));
                    int dg = Math.abs(((pa >> 8) & 0xFF) - ((pb >> 8) & 0xFF));
                    int db = Math.abs((pa & 0xFF) - (pb & 0xFF));
                    if (dr <= 5 && dg <= 5 && db <= 5) close++;
                    else different++;
                }
            }
        }
        System.out.printf("\n--- %s ---\n", label);
        System.out.printf("  Total: %d | Identical: %d (%.1f%%) | Close: %d (%.1f%%) | Different: %d (%.1f%%)%n",
                total, identical, 100.0 * identical / total,
                close, 100.0 * close / total,
                different, 100.0 * different / total);
    }
}
