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

            // Quick check at hotel view
            quickDiag(player, "HOTEL_VIEW");

            FrameSnapshot navigatorSnap = player.getFrameSnapshot();
            saveSnapshot(navigatorSnap, "01_navigator_loaded");
            writeClickReport(player, navigatorSnap, OUTPUT_DIR.resolve("01_click_targets.txt"));

            performClick(player, FIRST_CLICK_X, FIRST_CLICK_Y, "02_after_first_click", TICKS_AFTER_FIRST_CLICK);
            performClick(player, ENTER_CLICK_X, ENTER_CLICK_Y, "03_after_enter_click", TICKS_AFTER_SECOND_CLICK);

            quickDiag(player, "BEFORE_ROOM_ENTRY");

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

            // Diagnostic: check room bar state
            diagnosticRoomBar(player);
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

            if (i >= 20 && i <= 45) {
                quickDiag(player, "WAIT_TICK_" + i);
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
            // Check process list on room component
            String processInfo = "";
            if (roomThread instanceof Datum.ScriptInstance roomSI) {
                Datum comp = com.libreshockwave.vm.builtin.flow.ControlFlowBuiltins
                        .callHandlerOnInstance(vm, roomSI, "getComponent", java.util.List.of());
                if (comp instanceof Datum.ScriptInstance compSI) {
                    // Direct property access on script instance
                    Datum procList = compSI.properties().getOrDefault("pProcessList", Datum.VOID);
                    Datum activeFlag = compSI.properties().getOrDefault("pActiveFlag", Datum.VOID);
                    Datum roomId = compSI.properties().getOrDefault("pRoomId", Datum.VOID);
                    Datum saveData = compSI.properties().getOrDefault("pSaveData", Datum.VOID);
                    processInfo = " proc=" + procList + " active=" + activeFlag + " roomId=" + roomId + " saveData=" + saveData;
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

    private static void diagnosticRoomBar(Player player) {
        System.out.println("=== Room Bar Diagnostics ===");
        var vm = player.getVM();
        // Set up providers so VM handlers work outside tick loop
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
        } catch (Exception e) {
            System.out.println("Provider setup failed: " + e.getMessage());
        }
        try {
            // Check if windowExists("RoomBarID") works
            Datum windowExists = vm.callHandler("windowExists",
                    java.util.List.of(Datum.of("RoomBarID")));
            System.out.println("windowExists('RoomBarID') = " + windowExists);

            // Check if the room thread exists
            Datum roomThread = vm.callHandler("getThread",
                    java.util.List.of(Datum.symbol("room")));
            System.out.println("getThread(#room) = " + (roomThread.isVoid() ? "VOID" : roomThread.getClass().getSimpleName()));

            if (roomThread instanceof Datum.ScriptInstance roomSI) {
                // Get the interface
                Datum intf = com.libreshockwave.vm.builtin.flow.ControlFlowBuiltins
                        .callHandlerOnInstance(vm, roomSI, "getInterface", java.util.List.of());
                System.out.println("room.getInterface() = " + (intf.isVoid() ? "VOID" : intf.getClass().getSimpleName()));
            }

            // Check if the object "Room_gui_program" exists
            Datum guiObj = vm.callHandler("getObject",
                    java.util.List.of(Datum.of("Room_gui_program")));
            System.out.println("getObject('Room_gui_program') = " + (guiObj.isVoid() ? "VOID" : guiObj));

            // Check sprite manager free count
            Datum sprMgr = vm.callHandler("getSpriteManager", java.util.List.of());
            if (sprMgr instanceof Datum.ScriptInstance sprSI) {
                Datum freeSpr = com.libreshockwave.vm.builtin.flow.ControlFlowBuiltins
                        .callHandlerOnInstance(vm, sprSI, "getProperty",
                                java.util.List.of(Datum.symbol("freeSprCount")));
                Datum totalSpr = com.libreshockwave.vm.builtin.flow.ControlFlowBuiltins
                        .callHandlerOnInstance(vm, sprSI, "getProperty",
                                java.util.List.of(Datum.symbol("totalSprCount")));
                System.out.println("Sprite manager: total=" + totalSpr + " free=" + freeSpr);
            }

            // Check memberExists for key members
            for (String memName : new String[]{"room_bar.window", "newbie_lobby.room",
                    "alapalkki_bg", "newbie_lobby.room.txt", "background"}) {
                Datum me2 = vm.callHandler("memberExists",
                        java.util.List.of(Datum.of(memName)));
                System.out.println("memberExists('" + memName + "') = " + me2);
            }

            // Check resource manager getmemnum
            Datum resMgr = vm.callHandler("getResourceManager", java.util.List.of());
            if (resMgr instanceof Datum.ScriptInstance resSI) {
                Datum memNum = com.libreshockwave.vm.builtin.flow.ControlFlowBuiltins
                        .callHandlerOnInstance(vm, resSI, "getmemnum",
                                java.util.List.of(Datum.of("room_bar.window")));
                System.out.println("getResourceManager().getmemnum('room_bar.window') = " + memNum);

                // Check if "Room GUI Class" script can be found
                Datum guiClassNum = com.libreshockwave.vm.builtin.flow.ControlFlowBuiltins
                        .callHandlerOnInstance(vm, resSI, "getmemnum",
                                java.util.List.of(Datum.of("Room GUI Class")));
                System.out.println("getmemnum('Room GUI Class') = " + guiClassNum);

                // Check other critical classes
                for (String cls : new String[]{"Room Bar Class", "Room Interface Class",
                        "Room Component Class", "Window Manager Class", "Layout Parser Class",
                        "Info Stand Class", "Object Mover Class"}) {
                    Datum cn = com.libreshockwave.vm.builtin.flow.ControlFlowBuiltins
                            .callHandlerOnInstance(vm, resSI, "getmemnum",
                                    java.util.List.of(Datum.of(cls)));
                    System.out.println("getmemnum('" + cls + "') = " + cn);
                }
            }

            // Try calling roomConnected directly
            if (roomThread instanceof Datum.ScriptInstance roomSI2) {
                Datum comp = com.libreshockwave.vm.builtin.flow.ControlFlowBuiltins
                        .callHandlerOnInstance(vm, roomSI2, "getComponent", java.util.List.of());
                if (comp instanceof Datum.ScriptInstance compSI2) {
                    System.out.println("Calling roomConnected directly...");
                    Datum result = com.libreshockwave.vm.builtin.flow.ControlFlowBuiltins
                            .callHandlerOnInstance(vm, compSI2, "roomConnected",
                                    java.util.List.of(Datum.of("newbie_lobby"), Datum.of("ROOM_READY")));
                    System.out.println("roomConnected result = " + result);
                    Datum procAfter = compSI2.properties().getOrDefault("pProcessList", Datum.VOID);
                    System.out.println("pProcessList after roomConnected = " + procAfter);
                }
            }
        } catch (Exception e) {
            System.out.println("Diagnostic error: " + e.getMessage());
            e.printStackTrace();
        }
        System.out.println("=== End Room Bar Diagnostics ===");
        com.libreshockwave.vm.builtin.cast.CastLibProvider.clearProvider();
        com.libreshockwave.vm.builtin.sprite.SpritePropertyProvider.clearProvider();
        com.libreshockwave.vm.builtin.movie.MoviePropertyProvider.clearProvider();
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
