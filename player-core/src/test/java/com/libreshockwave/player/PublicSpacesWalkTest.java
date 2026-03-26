package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.player.input.HitTester;
import com.libreshockwave.player.render.pipeline.FrameSnapshot;
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
 * Test for navigating into a public space room and interacting.
 *
 * Flow:
 * 1. SSO login, wait for hotel view + navigator
 * 2. Click "Public Spaces" tab at (421, 76)
 * 3. Click "Welcome Lounge" entry
 * 4. Wait for the room to load
 * 5. Click the wave button at (629, 473)
 * 6. Click a floor tile at (341, 272) to walk
 *
 * This test investigates whether click events reach sprites in the room view.
 *
 * Usage:
 *   ./gradlew :player-core:runPublicSpacesWalkTest
 */
public class PublicSpacesWalkTest {

    private static final int NAV_X = 350, NAV_Y = 60, NAV_W = 370, NAV_H = 440;

    // Step coordinates
    // All coordinates are in the 720x540 stage coordinate space (same for WASM and Java)
    private static final int PUBLIC_SPACES_X = 421, PUBLIC_SPACES_Y = 76;
    private static final int WELCOME_LOUNGE_GO_X = 657, WELCOME_LOUNGE_GO_Y = 137; // row-level "Go" button
    private static final int GO_BUTTON_X = 671, GO_BUTTON_Y = 441; // bottom panel "Go" button
    private static final int WAVE_X = 629, WAVE_Y = 473;
    private static final int WALK_X = 341, WALK_Y = 272;

    private static final Path OUTPUT_DIR = Path.of("build/public-spaces-walk");

    public static void main(String[] args) throws Exception {
        Files.createDirectories(OUTPUT_DIR);
        System.out.println("=== Public Spaces Walk Test ===");

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
            writeClickReport(player, navSnap, "01_navigator");

            // --- Step 1: Click Public Spaces tab ---
            System.out.println("\n--- Clicking Public Spaces tab at (" + PUBLIC_SPACES_X + "," + PUBLIC_SPACES_Y + ") ---");
            performClick(player, PUBLIC_SPACES_X, PUBLIC_SPACES_Y, "02_public_spaces", 45);

            // --- Step 2: Click Welcome Lounge "Go" button (row-level) ---
            System.out.println("\n--- Clicking Welcome Lounge Go button at (" + WELCOME_LOUNGE_GO_X + "," + WELCOME_LOUNGE_GO_Y + ") ---");
            hitTestReport(player, WELCOME_LOUNGE_GO_X, WELCOME_LOUNGE_GO_Y, "welcome_lounge_go");
            performClick(player, WELCOME_LOUNGE_GO_X, WELCOME_LOUNGE_GO_Y, "03_welcome_lounge_go", 30);

            // Trace registerProcedure calls during room load
            player.getVM().addTraceHandler("eventprocinterface");
            player.getVM().addTraceHandler("eventprocroom");

            // Also try the bottom panel "Go" button in case the row click just selected
            FrameSnapshot afterRowGo = player.getFrameSnapshot();
            saveSnapshot(afterRowGo, "03b_after_row_go");
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
            writeClickReport(player, roomSnap, "04_room");

            // Diagnostics before interaction
            quickDiag(player, "ROOM_LOADED");
            // Trace registerProcedure calls
            player.getVM().addTraceHandler("eventprocinterface");
            player.getVM().addTraceHandler("eventprocroom");

            // Dump pProcList for wave button to verify registerProcedure worked
            {
                var sr = player.getStageRenderer().getSpriteRegistry();
                var spr = sr.get(259);
                if (spr != null) {
                    var scripts = spr.getScriptInstanceList();
                    if (scripts != null && !scripts.isEmpty()) {
                        for (var s : scripts) {
                            if (s instanceof Datum.ScriptInstance si) {
                                var f = Player.class.getDeclaredField("castLibManager");
                                f.setAccessible(true);
                                var clm = (com.libreshockwave.player.cast.CastLibManager) f.get(player);
                                com.libreshockwave.vm.builtin.cast.CastLibProvider.setProvider(clm);
                                Datum pl = com.libreshockwave.vm.util.AncestorChainWalker.getProperty(si, "pProcList");
                                System.out.println("[DIAG] ch=259 pProcList=" + pl);
                                com.libreshockwave.vm.builtin.cast.CastLibProvider.clearProvider();
                            }
                        }
                    }
                }
            }

            // --- Step 3: Click wave button ---
            System.out.println("\n--- Clicking wave button at (" + WAVE_X + "," + WAVE_Y + ") ---");
            hitTestReport(player, WAVE_X, WAVE_Y, "wave");
            performClick(player, WAVE_X, WAVE_Y, "05_wave_click", 60);

            // --- Step 4: Click floor tile to walk ---
            System.out.println("\n--- Clicking floor tile at (" + WALK_X + "," + WALK_Y + ") ---");
            hitTestReport(player, WALK_X, WALK_Y, "walk");
            performClick(player, WALK_X, WALK_Y, "06_walk_click", 90);

            // Final diagnostics
            quickDiag(player, "AFTER_INTERACTIONS");

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

        // Hit test before clicking
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

        double navChange = NavigatorClickTest.computeRegionChangeFraction(
                beforeSnap.renderFrame().toBufferedImage(),
                afterSnap.renderFrame().toBufferedImage(),
                0, 0,
                beforeSnap.renderFrame().getWidth(),
                beforeSnap.renderFrame().getHeight());
        System.out.printf(Locale.ROOT,
                "  click (%d,%d) -> screen change %.2f%%%n", x, y, navChange * 100.0);
    }

    private static void hitTestReport(Player player, int x, int y, String label) {
        FrameSnapshot snap = player.getFrameSnapshot();
        int hitChannel = HitTester.hitTest(player.getStageRenderer(), snap.frameNumber(), x, y,
                channel -> player.getEventDispatcher().isSpriteMouseInteractive(channel));
        int hitAny = HitTester.hitTest(player.getStageRenderer(), snap.frameNumber(), x, y,
                channel -> true);
        System.out.printf("  [%s] hitTest(%d,%d): interactive=%d, any=%d%n", label, x, y, hitChannel, hitAny);

        // List sprites near click point
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
                sb.append(String.format("    ch=%d loc=(%d,%d) size=(%dx%d) member='%s'%n",
                        sprite.getChannel(), sx, sy, sw, sh, sprite.getMemberName()));
            }
        }
        try {
            Files.writeString(OUTPUT_DIR.resolve(label + "_hit_report.txt"), sb.toString());
        } catch (IOException e) {
            System.out.println("Failed to write hit report: " + e.getMessage());
        }
    }

    private static void saveSnapshot(FrameSnapshot snap, String stem) throws IOException {
        Bitmap frame = snap.renderFrame();
        NavigatorSSOTest.savePng(frame, OUTPUT_DIR.resolve(stem + ".png"));
        NavigatorSSOTest.dumpSpriteInfo(snap, OUTPUT_DIR.resolve(stem + "_sprite_info.txt"));
    }

    private static void writeClickReport(Player player, FrameSnapshot snap, String label) throws IOException {
        StringBuilder sb = new StringBuilder();
        sb.append(String.format("=== Click Report: %s ===%n", label));
        int[][] points = {
                {PUBLIC_SPACES_X, PUBLIC_SPACES_Y},
                {WELCOME_LOUNGE_GO_X, WELCOME_LOUNGE_GO_Y},
                {GO_BUTTON_X, GO_BUTTON_Y},
                {WAVE_X, WAVE_Y},
                {WALK_X, WALK_Y}
        };
        String[] names = {"public_spaces", "welcome_lounge_go", "go_button_panel", "wave", "walk"};
        for (int i = 0; i < points.length; i++) {
            int px = points[i][0], py = points[i][1];
            int hitChannel = HitTester.hitTest(player.getStageRenderer(), snap.frameNumber(), px, py,
                    channel -> player.getEventDispatcher().isSpriteMouseInteractive(channel));
            int hitAny = HitTester.hitTest(player.getStageRenderer(), snap.frameNumber(), px, py,
                    channel -> true);
            sb.append(String.format("%s: point=(%d,%d) interactive=%d any=%d%n", names[i], px, py, hitChannel, hitAny));
        }
        Files.writeString(OUTPUT_DIR.resolve(label + "_click_report.txt"), sb.toString());
    }

    private static void quickDiag(Player player, String label) {
        var vm = player.getVM();
        try {
            var f = Player.class.getDeclaredField("castLibManager");
            f.setAccessible(true);
            var clm = (com.libreshockwave.player.cast.CastLibManager) f.get(player);
            com.libreshockwave.vm.builtin.cast.CastLibProvider.setProvider(clm);
            var f2 = Player.class.getDeclaredField("spriteProperties");
            f2.setAccessible(true);
            com.libreshockwave.vm.builtin.sprite.SpritePropertyProvider.setProvider(
                    (com.libreshockwave.vm.builtin.sprite.SpritePropertyProvider) f2.get(player));
            var f3 = Player.class.getDeclaredField("movieProperties");
            f3.setAccessible(true);
            com.libreshockwave.vm.builtin.movie.MoviePropertyProvider.setProvider(
                    (com.libreshockwave.vm.builtin.movie.MoviePropertyProvider) f3.get(player));

            Datum guiObj = vm.callHandler("getObject",
                    java.util.List.of(Datum.of("Room_gui_program")));
            Datum barObj = vm.callHandler("getObject",
                    java.util.List.of(Datum.of("RoomBarProgram")));
            Datum roomThread = vm.callHandler("getThread",
                    java.util.List.of(Datum.symbol("room")));

            String processInfo = "";
            if (roomThread instanceof Datum.ScriptInstance roomSI) {
                Datum comp = com.libreshockwave.vm.builtin.flow.ControlFlowBuiltins
                        .callHandlerOnInstance(vm, roomSI, "getComponent", java.util.List.of());
                if (comp instanceof Datum.ScriptInstance compSI) {
                    Datum procList = compSI.properties().getOrDefault("pProcessList", Datum.VOID);
                    Datum activeFlag = compSI.properties().getOrDefault("pActiveFlag", Datum.VOID);
                    Datum roomId = compSI.properties().getOrDefault("pRoomId", Datum.VOID);
                    processInfo = " proc=" + procList + " active=" + activeFlag + " roomId=" + roomId;
                }
            }
            System.out.printf("[%s] Room_gui=%s RoomBar=%s thread=%s%s%n",
                    label,
                    guiObj.isVoid() ? "VOID" : guiObj,
                    barObj.isVoid() ? "VOID" : barObj,
                    roomThread.isVoid() ? "VOID" : "exists",
                    processInfo);
        } catch (Exception e) {
            System.out.println("[" + label + "] diag error: " + e.getMessage());
        } finally {
            com.libreshockwave.vm.builtin.cast.CastLibProvider.clearProvider();
            com.libreshockwave.vm.builtin.sprite.SpritePropertyProvider.clearProvider();
            com.libreshockwave.vm.builtin.movie.MoviePropertyProvider.clearProvider();
        }
    }

    /**
     * Dump the Event_Broker_Behavior's pProcList for a sprite channel
     * to verify that mouse event procedures are registered.
     */
    private static void dumpSpriteBroker(Player player, int channel, String label) {
        try {
            var regField = Player.class.getDeclaredField("spriteProperties");
            regField.setAccessible(true);
            var f = Player.class.getDeclaredField("castLibManager");
            f.setAccessible(true);
            var clm = (com.libreshockwave.player.cast.CastLibManager) f.get(player);
            com.libreshockwave.vm.builtin.cast.CastLibProvider.setProvider(clm);
            com.libreshockwave.vm.builtin.sprite.SpritePropertyProvider.setProvider(
                    (com.libreshockwave.vm.builtin.sprite.SpritePropertyProvider) regField.get(player));

            var sr = player.getStageRenderer().getSpriteRegistry();
            var sprite = sr.get(channel);
            if (sprite == null) {
                System.out.printf("  [%s] ch=%d: sprite is null%n", label, channel);
                return;
            }
            var scriptInstances = sprite.getScriptInstanceList();
            System.out.printf("  [%s] ch=%d: scriptInstanceList size=%d%n",
                    label, channel, scriptInstances == null ? 0 : scriptInstances.size());
            if (scriptInstances != null) {
                for (int i = 0; i < scriptInstances.size(); i++) {
                    Datum si = scriptInstances.get(i);
                    if (si instanceof Datum.ScriptInstance inst) {
                        Datum procList = com.libreshockwave.vm.util.AncestorChainWalker
                                .getProperty(inst, "pProcList");
                        Datum id = com.libreshockwave.vm.util.AncestorChainWalker
                                .getProperty(inst, "id");
                        System.out.printf("  [%s]   instance[%d]: id=%s pProcList=%s%n",
                                label, i, id, procList);
                        // Check for mouseDown/mouseUp specifically
                        boolean hasDown = com.libreshockwave.vm.util.AncestorChainWalker
                                .hasHandler(inst, "mouseDown");
                        boolean hasUp = com.libreshockwave.vm.util.AncestorChainWalker
                                .hasHandler(inst, "mouseUp");
                        System.out.printf("  [%s]   instance[%d]: hasMouseDown=%s hasMouseUp=%s%n",
                                label, i, hasDown, hasUp);
                    }
                }
            }
            // Also check session client_lastclick
            Datum lastClick = player.getVM().callHandler("getObject",
                    java.util.List.of(Datum.symbol("session")));
            if (lastClick instanceof Datum.ScriptInstance sess) {
                Datum clk = com.libreshockwave.vm.builtin.flow.ControlFlowBuiltins
                        .callHandlerOnInstance(player.getVM(), sess, "GET",
                                java.util.List.of(Datum.of("client_lastclick")));
                System.out.printf("  [%s] session.client_lastclick = %s%n", label, clk);
            }
        } catch (Exception e) {
            System.out.printf("  [%s] broker dump error: %s%n", label, e.getMessage());
        } finally {
            com.libreshockwave.vm.builtin.cast.CastLibProvider.clearProvider();
            com.libreshockwave.vm.builtin.sprite.SpritePropertyProvider.clearProvider();
        }
    }

    private static void tickAndSleep(Player player, int steps) throws InterruptedException {
        for (int i = 0; i < steps; i++) {
            try { player.tick(); } catch (Exception ignored) {}
            Thread.sleep(67);
        }
    }
}
