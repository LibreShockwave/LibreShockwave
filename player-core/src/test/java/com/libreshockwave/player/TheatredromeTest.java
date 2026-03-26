package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.player.input.HitTester;
import com.libreshockwave.player.render.pipeline.FrameSnapshot;
import com.libreshockwave.player.render.pipeline.RenderSprite;
import com.libreshockwave.vm.datum.Datum;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Test for navigating into the Theatredrome public space room.
 *
 * Flow:
 * 1. SSO login, wait for hotel view + navigator
 * 2. Click "Public Spaces" tab at (421, 76)
 * 3. Click the Theatredrome entry (second row, below Welcome Lounge)
 * 4. Click Go button to enter
 * 5. Wait for the room to load
 * 6. Screenshot and analyze rendering
 *
 * Usage:
 *   ./gradlew :player-core:runTheatredromeTest
 */
public class TheatredromeTest {

    private static final int NAV_X = 350, NAV_Y = 60, NAV_W = 370, NAV_H = 440;

    // Step coordinates (720x540 stage coordinate space)
    private static final int PUBLIC_SPACES_X = 421, PUBLIC_SPACES_Y = 76;
    // Theatredrome is the second entry in the list, below Welcome Lounge (y=137)
    private static final int THEATREDROME_GO_X = 657, THEATREDROME_GO_Y = 157;
    private static final int GO_BUTTON_X = 671, GO_BUTTON_Y = 441; // bottom panel "Go" button

    private static final Path OUTPUT_DIR = Path.of("build/theatredrome");

    public static void main(String[] args) throws Exception {
        Files.createDirectories(OUTPUT_DIR);
        System.out.println("=== Theatredrome Test ===");

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

            // --- Screenshot hotel view with navigator ---
            FrameSnapshot navSnap = player.getFrameSnapshot();
            saveSnapshot(navSnap, "01_navigator_loaded");

            // --- Step 1: Click Public Spaces tab ---
            System.out.println("\n--- Clicking Public Spaces tab at (" + PUBLIC_SPACES_X + "," + PUBLIC_SPACES_Y + ") ---");
            performClick(player, PUBLIC_SPACES_X, PUBLIC_SPACES_Y, "02_public_spaces", 45);

            // Screenshot after Public Spaces tab to see the list
            FrameSnapshot psSnap = player.getFrameSnapshot();
            saveSnapshot(psSnap, "02b_public_spaces_list");
            dumpNavigatorList(psSnap);

            // --- Step 2: Click Theatredrome entry (second row) ---
            System.out.println("\n--- Clicking Theatredrome row at (" + THEATREDROME_GO_X + "," + THEATREDROME_GO_Y + ") ---");
            hitTestReport(player, THEATREDROME_GO_X, THEATREDROME_GO_Y, "theatredrome_row");
            performClick(player, THEATREDROME_GO_X, THEATREDROME_GO_Y, "03_theatredrome_click", 30);

            // Also try the bottom panel "Go" button
            FrameSnapshot afterRowClick = player.getFrameSnapshot();
            saveSnapshot(afterRowClick, "03b_after_row_click");
            System.out.println("\n--- Clicking bottom panel Go button at (" + GO_BUTTON_X + "," + GO_BUTTON_Y + ") ---");
            hitTestReport(player, GO_BUTTON_X, GO_BUTTON_Y, "go_button_panel");
            performClick(player, GO_BUTTON_X, GO_BUTTON_Y, "03c_panel_go", 30);

            // Wait for room to load (tick extensively)
            System.out.println("\nWaiting for room to load (ticking 600 frames ~40s)...");
            for (int i = 0; i < 600; i++) {
                try { player.tick(); } catch (Exception ignored) {}
                Thread.sleep(67);
                if (i % 60 == 0) {
                    FrameSnapshot snap = player.getFrameSnapshot();
                    System.out.printf("  room load tick %d, sprites=%d%n", i, snap.sprites().size());
                }
            }

            FrameSnapshot roomSnap = player.getFrameSnapshot();
            saveSnapshot(roomSnap, "04_room_loaded");

            // Dump detailed sprite info for room analysis
            dumpRoomSprites(roomSnap);

            // Dump palette information for all visible room sprites
            dumpPaletteInfo(player, roomSnap);

            // Extra ticks to let room fully settle
            System.out.println("\nTicking 200 extra frames for room to settle...");
            tickAndSleep(player, 200);

            FrameSnapshot settledSnap = player.getFrameSnapshot();
            saveSnapshot(settledSnap, "05_room_settled");
            dumpRoomSprites(settledSnap);
            dumpPaletteInfo(player, settledSnap);

            // Diagnostic: check white box sprites
            dumpRoomBarDiag(player, settledSnap);

            // Save avatar canvas bitmap for inspection
            for (var sprite : settledSnap.sprites()) {
                String name = sprite.getMemberName();
                if (name != null && name.contains("Canvas") && sprite.isVisible()) {
                    System.out.printf("=== Avatar canvas: ch=%d '%s' ink=%d blend=%d type=%s ===%n",
                            sprite.getChannel(), name, sprite.getInk(), sprite.getBlend(), sprite.getType());
                    var dynMem = sprite.getDynamicMember();
                    if (dynMem != null) {
                        Bitmap dynBmp = dynMem.getBitmap();
                        if (dynBmp != null) {
                            System.out.printf("  dynBmp: %dx%d %dbpp scriptMod=%s%n",
                                    dynBmp.getWidth(), dynBmp.getHeight(), dynBmp.getBitDepth(), dynBmp.isScriptModified());
                            NavigatorSSOTest.savePng(dynBmp, OUTPUT_DIR.resolve("avatar_canvas_raw.png"));
                            // Check bottom-left/right corners (shoe area)
                            int w = dynBmp.getWidth(), h = dynBmp.getHeight();
                            // Dump exact hex values for shoe-area pixels to find near-white artifacts
                            System.out.println("  Shoe area exact pixels:");
                            for (int y = Math.max(0, h - 18); y < h; y++) {
                                for (int x = 0; x < w; x++) {
                                    int p = dynBmp.getPixel(x, y);
                                    int rgb = p & 0xFFFFFF;
                                    int a = (p >>> 24);
                                    // Report near-white pixels (not exactly white, but close)
                                    int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
                                    if (rgb != 0xFFFFFF && r > 200 && g > 200 && b > 200 && a > 0) {
                                        System.out.printf("    NEAR-WHITE at (%d,%d): 0x%06X  R=%d G=%d B=%d%n",
                                                x, y, rgb, r, g, b);
                                    }
                                }
                            }
                        }
                    }
                    Bitmap baked = sprite.getBakedBitmap();
                    if (baked != null) {
                        NavigatorSSOTest.savePng(baked, OUTPUT_DIR.resolve("avatar_canvas_baked.png"));
                        int w = baked.getWidth(), h = baked.getHeight();
                        System.out.println("  Baked bottom pixels (y=45 to end):");
                        for (int y = Math.max(0, h - 18); y < h; y++) {
                            StringBuilder row = new StringBuilder();
                            row.append(String.format("    y=%d: ", y));
                            for (int x = 0; x < w; x++) {
                                int p = baked.getPixel(x, y);
                                int a = (p >>> 24);
                                if (a == 0) row.append(".");
                                else if ((p & 0xFFFFFF) == 0xFFFFFF && a == 0xFF) row.append("W");
                                else if (a < 0xFF) row.append("t");
                                else row.append("#");
                            }
                            System.out.println(row);
                        }
                    }
                }
            }

            // Crop avatar areas from room render for close inspection
            Bitmap roomRender = settledSnap.renderFrame();
            // In-room avatar at (341,377) size 32x62 - crop wider area around it
            {
                int cx = 341, cy = 377, cw = 52, ch2 = 82;
                Bitmap crop = new Bitmap(cw, ch2, 32);
                for (int y = 0; y < ch2; y++) {
                    for (int x = 0; x < cw; x++) {
                        int sx = cx - 10 + x, sy = cy - 10 + y;
                        if (sx >= 0 && sx < roomRender.getWidth() && sy >= 0 && sy < roomRender.getHeight()) {
                            crop.setPixel(x, y, roomRender.getPixel(sx, sy));
                        }
                    }
                }
                NavigatorSSOTest.savePng(crop, OUTPUT_DIR.resolve("crop_avatar_inroom.png"));
            }
            // Info stand at (631,302)
            {
                int cx = 621, cy = 292, cw = 94, ch2 = 122;
                Bitmap crop = new Bitmap(cw, ch2, 32);
                for (int y = 0; y < ch2; y++) {
                    for (int x = 0; x < cw; x++) {
                        int sx = cx + x, sy = cy + y;
                        if (sx >= 0 && sx < roomRender.getWidth() && sy >= 0 && sy < roomRender.getHeight()) {
                            crop.setPixel(x, y, roomRender.getPixel(sx, sy));
                        }
                    }
                }
                NavigatorSSOTest.savePng(crop, OUTPUT_DIR.resolve("crop_infostand.png"));
            }

            // Check paletteRef on avatar body part members
            var clm = getCastLibManager(player);
            if (clm != null) {
                String[] bodyParts = {"bd_std_hr_001_0_0", "lg_std_hr_001_0_0", "sh_std_hr_001_0_0",
                        "hd_std_hr_001_0_0", "bd_std_sd_001_0_0", "sh_std_sd_001_0_0"};
                System.out.println("=== Body part paletteRef check ===");
                for (String partName : bodyParts) {
                    var member = clm.findCastMemberByName(partName);
                    if (member != null) {
                        System.out.printf("  '%s': hasPaletteOverride=%s palCastLib=%d palMemberNum=%d palVersion=%d%n",
                                partName, member.hasPaletteOverride(),
                                member.getPaletteRefCastLib(), member.getPaletteRefMemberNum(),
                                member.getPaletteVersion());
                    }
                }
                // Check some common avatar part names
                for (String p : new String[]{"hd_std_hr_001_1_0", "bd_std_hr_001_1_0",
                        "lg_std_hr_001_1_0", "sh_std_hr_001_1_0",
                        "hd_std_hr_001_0_0", "sh_std_sd_001_0_0"}) {
                    var m = clm.findCastMemberByName(p);
                    if (m != null) {
                        System.out.printf("  '%s': paletteOverride=%s ver=%d%n",
                                p, m.hasPaletteOverride(), m.getPaletteVersion());
                    }
                }
            }

            // Save info stand avatar for inspection
            for (var sprite : settledSnap.sprites()) {
                String name = sprite.getMemberName();
                if (name != null && name.contains("info_image") && sprite.isVisible()) {
                    System.out.printf("=== Info stand image: ch=%d '%s' ink=%d blend=%d ===%n",
                            sprite.getChannel(), name, sprite.getInk(), sprite.getBlend());
                    var dynMem = sprite.getDynamicMember();
                    if (dynMem != null && dynMem.getBitmap() != null) {
                        Bitmap dynBmp = dynMem.getBitmap();
                        System.out.printf("  dynBmp: %dx%d %dbpp scriptMod=%s%n",
                                dynBmp.getWidth(), dynBmp.getHeight(), dynBmp.getBitDepth(), dynBmp.isScriptModified());
                        NavigatorSSOTest.savePng(dynBmp, OUTPUT_DIR.resolve("infostand_image_raw.png"));
                        // Check shoe area for trapped white pixels
                        int w = dynBmp.getWidth(), h = dynBmp.getHeight();
                        System.out.println("  Raw shoe area (bottom 20 rows):");
                        for (int y = Math.max(0, h - 20); y < h; y++) {
                            StringBuilder row = new StringBuilder();
                            row.append(String.format("    y=%2d: ", y));
                            for (int x = 0; x < w; x++) {
                                int p = dynBmp.getPixel(x, y);
                                int rgb = p & 0xFFFFFF;
                                if (rgb == 0xFFFFFF) row.append("W");
                                else if (rgb == 0xEEEEEE) row.append("e");
                                else if (rgb == 0x000000) row.append(".");
                                else row.append("#");
                            }
                            System.out.println(row);
                        }
                    }
                    Bitmap baked = sprite.getBakedBitmap();
                    if (baked != null) {
                        NavigatorSSOTest.savePng(baked, OUTPUT_DIR.resolve("infostand_image_baked.png"));
                        int w = baked.getWidth(), h = baked.getHeight();
                        System.out.println("  Baked shoe area (bottom 20 rows):");
                        for (int y = Math.max(0, h - 20); y < h; y++) {
                            StringBuilder row = new StringBuilder();
                            row.append(String.format("    y=%2d: ", y));
                            for (int x = 0; x < w; x++) {
                                int p = baked.getPixel(x, y);
                                int a = (p >>> 24);
                                int rgb = p & 0xFFFFFF;
                                if (a == 0) row.append(".");
                                else if (rgb == 0xFFFFFF) row.append("W");
                                else if (rgb == 0xEEEEEE) row.append("e");
                                else if (a < 0xFF) row.append("t");
                                else row.append("#");
                            }
                            System.out.println(row);
                        }
                    }
                }
            }

            // Specific dooredmask diagnostic
            for (var sprite : settledSnap.sprites()) {
                if ("dooredmask".equals(sprite.getMemberName())) {
                    System.out.println("=== dooredmask sprite diagnostic ===");
                    System.out.printf("  type: %s ink: %d inkMode: %s%n",
                            sprite.getType(), sprite.getInk(), sprite.getInkMode());
                    System.out.printf("  castMember: %s%n", sprite.getCastMember() != null ? "present (id=" + sprite.getCastMember().id().value() + ")" : "null");
                    System.out.printf("  dynamicMember: %s%n", sprite.getDynamicMember() != null ? "present" : "null");
                    if (sprite.getDynamicMember() != null) {
                        Bitmap dynBmp = sprite.getDynamicMember().getBitmap();
                        System.out.printf("  dynBmp: %s scriptMod=%s%n",
                                dynBmp != null ? dynBmp.getWidth() + "x" + dynBmp.getHeight() + " " + dynBmp.getBitDepth() + "bpp" : "null",
                                dynBmp != null ? dynBmp.isScriptModified() : "N/A");
                        if (dynBmp != null) {
                            System.out.printf("  dynBmp pixels: center=0x%08X (0,0)=0x%08X%n",
                                    dynBmp.getPixel(dynBmp.getWidth()/2, dynBmp.getHeight()/2),
                                    dynBmp.getPixel(0, 0));
                        }
                    }
                    Bitmap baked = sprite.getBakedBitmap();
                    if (baked != null) {
                        System.out.printf("  baked: %dx%d %dbpp center=0x%08X alpha=%d%n",
                                baked.getWidth(), baked.getHeight(), baked.getBitDepth(),
                                baked.getPixel(baked.getWidth()/2, baked.getHeight()/2),
                                (baked.getPixel(baked.getWidth()/2, baked.getHeight()/2) >>> 24));
                    }
                }
            }

            // Save error log
            Files.writeString(OUTPUT_DIR.resolve("errors.txt"), String.join(System.lineSeparator(), allErrors));
            System.out.printf("%nTotal errors: %d%n", allErrors.size());

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
            try { player.tick(); } catch (Exception ignored) {}
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
            Thread.sleep(67);
        }

        throw new IllegalStateException("Navigator did not appear within 120 seconds.");
    }

    private static void performClick(Player player, int x, int y, String outputStem, int settleTicks) throws Exception {
        FrameSnapshot beforeSnap = player.getFrameSnapshot();
        saveSnapshot(beforeSnap, outputStem + "_before");

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
        saveSnapshot(afterSnap, outputStem);

        double change = NavigatorClickTest.computeRegionChangeFraction(
                beforeSnap.renderFrame().toBufferedImage(),
                afterSnap.renderFrame().toBufferedImage(),
                0, 0,
                beforeSnap.renderFrame().getWidth(),
                beforeSnap.renderFrame().getHeight());
        System.out.printf(Locale.ROOT,
                "  click (%d,%d) -> screen change %.2f%%%n", x, y, change * 100.0);
    }

    private static void hitTestReport(Player player, int x, int y, String label) {
        FrameSnapshot snap = player.getFrameSnapshot();
        int hitChannel = HitTester.hitTest(player.getStageRenderer(), snap.frameNumber(), x, y,
                channel -> player.getEventDispatcher().isSpriteMouseInteractive(channel));
        int hitAny = HitTester.hitTest(player.getStageRenderer(), snap.frameNumber(), x, y,
                channel -> true);
        System.out.printf("  [%s] hitTest(%d,%d): interactive=%d, any=%d%n", label, x, y, hitChannel, hitAny);

        StringBuilder sb = new StringBuilder();
        sb.append(String.format("Hit report for '%s' at (%d,%d):%n", label, x, y));
        sb.append(String.format("  interactive channel: %d%n", hitChannel));
        sb.append(String.format("  any channel: %d%n", hitAny));
        sb.append("  Nearby visible sprites:\n");
        for (var sprite : snap.sprites()) {
            if (!sprite.isVisible()) continue;
            int sx = sprite.getX(), sy = sprite.getY();
            int sw = sprite.getWidth(), sh = sprite.getHeight();
            if (x >= sx && x < sx + sw && y >= sy && y < sy + sh) {
                sb.append(String.format("    ch=%d loc=(%d,%d) size=(%dx%d) ink=%d member='%s'%n",
                        sprite.getChannel(), sx, sy, sw, sh, sprite.getInk(), sprite.getMemberName()));
            }
        }
        try {
            Files.writeString(OUTPUT_DIR.resolve(label + "_hit_report.txt"), sb.toString());
        } catch (IOException e) {
            System.out.println("Failed to write hit report: " + e.getMessage());
        }
    }

    /**
     * Dump all visible sprites in the navigator list area for coordinate debugging.
     */
    private static void dumpNavigatorList(FrameSnapshot snap) throws IOException {
        StringBuilder sb = new StringBuilder();
        sb.append("=== Navigator List Sprites ===\n");
        for (var sprite : snap.sprites()) {
            if (!sprite.isVisible()) continue;
            int sx = sprite.getX(), sy = sprite.getY();
            // Only sprites in the navigator area (right side)
            if (sx >= NAV_X - 50 && sx < NAV_X + NAV_W + 50 && sy >= NAV_Y && sy < NAV_Y + NAV_H) {
                sb.append(String.format("ch=%d loc=(%d,%d) size=(%dx%d) ink=%d member='%s'%n",
                        sprite.getChannel(), sx, sy, sprite.getWidth(), sprite.getHeight(),
                        sprite.getInk(), sprite.getMemberName()));
            }
        }
        Files.writeString(OUTPUT_DIR.resolve("navigator_list_sprites.txt"), sb.toString());
        System.out.println("Navigator list sprites written to navigator_list_sprites.txt");
    }

    /**
     * Dump all room sprites with details for debugging rendering issues.
     */
    private static void dumpRoomSprites(FrameSnapshot snap) throws IOException {
        StringBuilder sb = new StringBuilder();
        sb.append("=== Room Sprites ===\n");
        sb.append(String.format("Total sprites: %d%n", snap.sprites().size()));
        for (var sprite : snap.sprites()) {
            String vis = sprite.isVisible() ? "VIS" : "HID";
            sb.append(String.format("[%s] ch=%d loc=(%d,%d) size=(%dx%d) ink=%d blend=%d " +
                            "foreColor=%d backColor=%d hasFore=%s hasBack=%s member='%s'",
                    vis, sprite.getChannel(), sprite.getX(), sprite.getY(),
                    sprite.getWidth(), sprite.getHeight(),
                    sprite.getInk(), sprite.getBlend(),
                    sprite.getForeColor(), sprite.getBackColor(),
                    sprite.hasForeColor(), sprite.hasBackColor(),
                    sprite.getMemberName()));
            // Check if this sprite has a baked bitmap and sample some pixel colors
            Bitmap baked = sprite.getBakedBitmap();
            if (baked != null && sprite.isVisible()) {
                int w = baked.getWidth(), h = baked.getHeight();
                // Sample center pixel
                if (w > 0 && h > 0) {
                    int cx = w / 2, cy = h / 2;
                    int pixel = baked.getPixel(cx, cy);
                    sb.append(String.format(" centerPixel=0x%08X", pixel));
                }
            }
            sb.append("\n");
        }
        String stem = snap.frameNumber() > 0 ? "room_sprites_frame" + snap.frameNumber() : "room_sprites";
        Files.writeString(OUTPUT_DIR.resolve(stem + ".txt"), sb.toString());
    }

    /**
     * Dump palette information for room bitmap sprites.
     */
    private static void dumpPaletteInfo(Player player, FrameSnapshot snap) throws IOException {
        StringBuilder sb = new StringBuilder();
        sb.append("=== Palette Info ===\n");

        // Movie-level palette
        var bitmapResolver = player.getBitmapResolver();
        var moviePalette = bitmapResolver.getMoviePalette();
        sb.append(String.format("Movie palette: %s%n", moviePalette != null ? moviePalette : "null"));
        if (moviePalette != null) {
            sb.append("  First 16 colors: ");
            for (int i = 0; i < 16; i++) {
                sb.append(String.format("0x%06X ", moviePalette.getColor(i) & 0xFFFFFF));
            }
            sb.append("\n");
        }

        // Check each visible room sprite's cast member palette
        for (var sprite : snap.sprites()) {
            if (!sprite.isVisible() || sprite.getCastMember() == null) continue;
            String name = sprite.getMemberName();
            if (name == null || name.isEmpty()) continue;

            var member = sprite.getCastMember();
            if (member.specificData() != null && member.specificData().length >= 10) {
                var memberFile = member.file();
                int dirVer = 1200;
                if (memberFile != null && memberFile.getConfig() != null) {
                    dirVer = memberFile.getConfig().directorVersion();
                }
                var info = com.libreshockwave.cast.BitmapInfo.parse(member.specificData(), dirVer);
                sb.append(String.format("  ch=%d '%s' bitDepth=%d paletteId=%d paletteCastLib=%d size=%dx%d%n",
                        sprite.getChannel(), name, info.bitDepth(), info.paletteId(),
                        info.paletteCastLib(), info.width(), info.height()));

                // Try to resolve the palette
                if (memberFile != null) {
                    var pal = memberFile.resolvePalette(info.paletteId());
                    var palExact = memberFile.resolvePaletteExact(info.paletteId());
                    sb.append(String.format("    resolved palette: %s (exact: %s)%n",
                            pal != null ? "found" : "null",
                            palExact != null ? "found" : "null"));
                    if (pal != null) {
                        sb.append("    palette first 16: ");
                        for (int i = 0; i < 16; i++) {
                            sb.append(String.format("0x%06X ", pal.getColor(i) & 0xFFFFFF));
                        }
                        sb.append("\n");
                    }
                }
            }
        }

        // Also dump full palette for 'teatteri'
        for (var sprite : snap.sprites()) {
            if (!sprite.isVisible() || sprite.getCastMember() == null) continue;
            String name = sprite.getMemberName();
            if (!"teatteri".equals(name)) continue;

            var member = sprite.getCastMember();
            if (member.specificData() != null && member.specificData().length >= 10) {
                var memberFile = member.file();
                int dirVer = 1200;
                if (memberFile != null && memberFile.getConfig() != null) {
                    dirVer = memberFile.getConfig().directorVersion();
                }
                var info = com.libreshockwave.cast.BitmapInfo.parse(member.specificData(), dirVer);
                if (memberFile != null) {
                    var pal = memberFile.resolvePalette(info.paletteId());
                    if (pal != null) {
                        sb.append("\n=== Full palette for 'teatteri' (paletteId=" + info.paletteId() + ") ===\n");
                        for (int i = 0; i < 256; i++) {
                            int c = pal.getColor(i);
                            sb.append(String.format("  [%3d] 0x%06X  R=%3d G=%3d B=%3d%n",
                                    i, c & 0xFFFFFF,
                                    (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
                        }
                    }
                }

                // Sample some pixel values from the baked bitmap
                Bitmap baked = sprite.getBakedBitmap();
                if (baked != null) {
                    sb.append("\n=== 'teatteri' pixel samples ===\n");
                    int w = baked.getWidth(), h = baked.getHeight();
                    // Sample floor area (center-bottom), wall area (center-left), curtain area (center-top)
                    int[][] samples = {
                        {w/2, h*3/4, 0}, // floor area
                        {w/4, h/2, 0},   // left wall
                        {w/2, h/4, 0},   // upper/curtain area
                        {w/2, h/2, 0},   // center
                        {50, 50, 0},     // top-left
                        {w-50, 50, 0},   // top-right
                    };
                    String[] labels = {"floor", "left-wall", "curtain", "center", "top-left", "top-right"};
                    for (int i = 0; i < samples.length; i++) {
                        int sx = Math.min(samples[i][0], w-1);
                        int sy = Math.min(samples[i][1], h-1);
                        int pixel = baked.getPixel(sx, sy);
                        sb.append(String.format("  %s (%d,%d): 0x%08X  R=%d G=%d B=%d A=%d%n",
                                labels[i], sx, sy, pixel,
                                (pixel >> 16) & 0xFF, (pixel >> 8) & 0xFF, pixel & 0xFF, (pixel >> 24) & 0xFF));
                    }
                }

                // Also decode raw bitmap to check pixel indices before palette mapping
                var rawBitmap = memberFile.decodeBitmap(member);
                if (rawBitmap.isPresent()) {
                    Bitmap raw = rawBitmap.get();
                    sb.append("\n=== 'teatteri' raw decoded pixel samples ===\n");
                    int w2 = raw.getWidth(), h2 = raw.getHeight();
                    int[][] rawSamples = {
                        {w2/2, h2*3/4},
                        {w2/4, h2/2},
                        {w2/2, h2/4},
                        {w2/2, h2/2},
                    };
                    String[] rawLabels = {"floor", "left-wall", "curtain", "center"};
                    for (int i = 0; i < rawSamples.length; i++) {
                        int sx = Math.min(rawSamples[i][0], w2-1);
                        int sy = Math.min(rawSamples[i][1], h2-1);
                        int pixel = raw.getPixel(sx, sy);
                        sb.append(String.format("  %s (%d,%d): 0x%08X  R=%d G=%d B=%d%n",
                                rawLabels[i], sx, sy, pixel,
                                (pixel >> 16) & 0xFF, (pixel >> 8) & 0xFF, pixel & 0xFF));
                    }
                }
            }
        }

        Files.writeString(OUTPUT_DIR.resolve("palette_info.txt"), sb.toString());
    }

    private static void saveSnapshot(FrameSnapshot snap, String stem) throws IOException {
        Bitmap frame = snap.renderFrame();
        NavigatorSSOTest.savePng(frame, OUTPUT_DIR.resolve(stem + ".png"));
        NavigatorSSOTest.dumpSpriteInfo(snap, OUTPUT_DIR.resolve(stem + "_sprite_info.txt"));
    }

    /**
     * Diagnose room bar rendering issue.
     */
    private static void dumpRoomBarDiag(Player player, FrameSnapshot snap) throws Exception {
        StringBuilder sb = new StringBuilder();
        sb.append("=== Room Bar Diagnostics ===\n");

        // Find the room bar background sprite
        for (var sprite : snap.sprites()) {
            if (!sprite.isVisible()) continue;
            String name = sprite.getMemberName();
            if (name == null) continue;

            // Check room bar elements
            if (name.contains("RoomBarID") || name.contains("alapalkki")) {
                sb.append(String.format("ch=%d '%s' loc=(%d,%d) size=%dx%d ink=%d blend=%d%n",
                        sprite.getChannel(), name,
                        sprite.getX(), sprite.getY(),
                        sprite.getWidth(), sprite.getHeight(),
                        sprite.getInk(), sprite.getBlend()));

                // Check dynamic member
                var dynMember = sprite.getDynamicMember();
                if (dynMember != null) {
                    Bitmap dynBmp = dynMember.getBitmap();
                    sb.append(String.format("  dynamic member: bitmap=%s scriptMod=%s%n",
                            dynBmp != null ? dynBmp.getWidth() + "x" + dynBmp.getHeight() + " " + dynBmp.getBitDepth() + "bpp" : "null",
                            dynBmp != null ? dynBmp.isScriptModified() : "N/A"));
                    if (dynBmp != null) {
                        int cx = dynBmp.getWidth() / 2, cy = dynBmp.getHeight() / 2;
                        int pixel = dynBmp.getPixel(cx, cy);
                        sb.append(String.format("  dynamic center pixel: 0x%08X%n", pixel));
                        // Sample grid
                        for (int sy = 0; sy < dynBmp.getHeight(); sy += 10) {
                            sb.append(String.format("  row y=%d: ", sy));
                            for (int sx = 0; sx < dynBmp.getWidth(); sx += 100) {
                                sb.append(String.format("0x%06X ", dynBmp.getPixel(sx, sy) & 0xFFFFFF));
                            }
                            sb.append("\n");
                        }
                        // Save as PNG
                        NavigatorSSOTest.savePng(dynBmp, OUTPUT_DIR.resolve("roombar_bg_dynamic.png"));
                    }
                } else {
                    sb.append("  dynamic member: null\n");
                }

                // Check file-loaded member
                if (sprite.getCastMember() != null) {
                    var member = sprite.getCastMember();
                    sb.append(String.format("  file member: id=%d name='%s'%n",
                            member.id().value(), member.name()));
                    var decoded = player.getBitmapResolver().decodeBitmap(member);
                    if (decoded.isPresent()) {
                        Bitmap raw = decoded.get();
                        int cx = raw.getWidth() / 2, cy = raw.getHeight() / 2;
                        sb.append(String.format("  decoded: %dx%d %dbpp center=0x%08X%n",
                                raw.getWidth(), raw.getHeight(), raw.getBitDepth(),
                                raw.getPixel(cx, cy)));
                    } else {
                        sb.append("  decoded: FAILED\n");
                    }
                }
            }
        }

        // Also check the original alapalkki_bg member from cast
        var castLibManager = getCastLibManager(player);
        if (castLibManager != null) {
            var member = castLibManager.findCastMemberByName("alapalkki_bg");
            if (member != null) {
                sb.append("\n--- Original 'alapalkki_bg' cast member ---\n");
                Bitmap bmp = member.getBitmap();
                sb.append(String.format("  runtime bitmap: %s%n",
                        bmp != null ? bmp.getWidth() + "x" + bmp.getHeight() + " " + bmp.getBitDepth() + "bpp scriptMod=" + bmp.isScriptModified() : "null"));
                if (bmp != null) {
                    int cx = bmp.getWidth() / 2, cy = bmp.getHeight() / 2;
                    sb.append(String.format("  center pixel: 0x%08X%n", bmp.getPixel(cx, cy)));
                    // Check all pixels in a small area
                    sb.append("  first row sample: ");
                    for (int x = 0; x < Math.min(10, bmp.getWidth()); x++) {
                        sb.append(String.format("0x%06X ", bmp.getPixel(x, 0) & 0xFFFFFF));
                    }
                    sb.append("\n");
                }

                // Also check if the file-loaded version is different
                var fileChunk = castLibManager.getCastMemberByName("alapalkki_bg");
                if (fileChunk != null) {
                    var decoded = player.getBitmapResolver().decodeBitmap(fileChunk);
                    if (decoded.isPresent()) {
                        Bitmap raw = decoded.get();
                        sb.append(String.format("  file-decoded: %dx%d %dbpp%n", raw.getWidth(), raw.getHeight(), raw.getBitDepth()));
                        int cx = raw.getWidth() / 2, cy = raw.getHeight() / 2;
                        sb.append(String.format("  file-decoded center: 0x%08X%n", raw.getPixel(cx, cy)));
                        sb.append("  file-decoded first row: ");
                        for (int x = 0; x < Math.min(10, raw.getWidth()); x++) {
                            sb.append(String.format("0x%06X ", raw.getPixel(x, 0) & 0xFFFFFF));
                        }
                        sb.append("\n");
                    }
                }
            } else {
                sb.append("\n'alapalkki_bg' NOT FOUND in cast members\n");
            }
        }

        Files.writeString(OUTPUT_DIR.resolve("roombar_diag.txt"), sb.toString());
        System.out.println("Room bar diagnostics written to roombar_diag.txt");
    }

    private static com.libreshockwave.player.cast.CastLibManager getCastLibManager(Player player) throws Exception {
        var f = Player.class.getDeclaredField("castLibManager");
        f.setAccessible(true);
        return (com.libreshockwave.player.cast.CastLibManager) f.get(player);
    }

    private static void tickAndSleep(Player player, int steps) throws InterruptedException {
        for (int i = 0; i < steps; i++) {
            try { player.tick(); } catch (Exception ignored) {}
            Thread.sleep(67);
        }
    }
}
