package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.player.input.HitTester;
import com.libreshockwave.player.render.pipeline.FrameSnapshot;
import com.libreshockwave.vm.DebugConfig;
import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.builtin.flow.UpdateProvider;
import com.libreshockwave.vm.builtin.media.SoundProvider;
import com.libreshockwave.vm.builtin.movie.MoviePropertyProvider;
import com.libreshockwave.vm.builtin.net.ExternalParamProvider;
import com.libreshockwave.vm.builtin.net.NetBuiltins;
import com.libreshockwave.vm.builtin.sprite.SpritePropertyProvider;
import com.libreshockwave.vm.builtin.timeout.TimeoutProvider;
import com.libreshockwave.vm.builtin.xtra.XtraBuiltins;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.util.AncestorChainWalker;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.Callable;

/**
 * BattleBall room-entry regression harness.
 *
 * Flow:
 * 1. Login with SSO and wait for hotel view + navigator
 * 2. Click Public Spaces
 * 3. Click Games open row
 * 4. Click BattleBall row
 * 5. Click bottom-panel Go
 * 6. Tick through room entry and capture errors/snapshots
 *
 * Usage:
 *   ./gradlew :player-core:runBattleBallRoomTest
 */
public final class BattleBallRoomTest {

    private static final int NAV_X = 350;
    private static final int NAV_Y = 60;

    private static final int PUBLIC_SPACES_X = 421;
    private static final int PUBLIC_SPACES_Y = 76;
    private static final int GAMES_OPEN_X = 657;
    private static final int GAMES_OPEN_Y = 334;
    private static final int BATTLEBALL_ROW_X = 657;
    private static final int BATTLEBALL_ROW_Y = 354;
    private static final int GO_BUTTON_X = 671;
    private static final int GO_BUTTON_Y = 441;

    private static final Path OUTPUT_DIR = Path.of("build/battleball-room");

    private BattleBallRoomTest() {}

    public static void main(String[] args) throws Exception {
        Files.createDirectories(OUTPUT_DIR);
        System.out.println("=== BattleBall Room Test ===");

        Player player = createPlayer();
        DebugConfig.setDebugPlaybackEnabled(true);
        List<String> allErrors = new ArrayList<>();
        player.setErrorListener((msg, ex) -> {
            allErrors.add(msg);
            if (allErrors.size() <= 100) {
                System.out.println("[Error] " + msg);
                if (ex != null) {
                    System.out.println("  exception: " + ex.getMessage());
                    String stack = ex.formatLingoCallStack();
                    if (stack != null && !stack.isBlank()) {
                        System.out.println(stack);
                    }
                }
            }
        });

        try {
            startAndLoadNavigator(player);
            saveSnapshot(player.getFrameSnapshot(), "01_navigator_loaded");

            System.out.printf("%n--- Clicking Public Spaces tab at (%d,%d) ---%n",
                    PUBLIC_SPACES_X, PUBLIC_SPACES_Y);
            performClick(player, PUBLIC_SPACES_X, PUBLIC_SPACES_Y, "02_public_spaces", 45);
            saveSnapshot(player.getFrameSnapshot(), "02b_public_spaces_list");

            System.out.printf("%n--- Clicking Games open row at (%d,%d) ---%n",
                    GAMES_OPEN_X, GAMES_OPEN_Y);
            performClick(player, GAMES_OPEN_X, GAMES_OPEN_Y, "03_games_open", 45);
            saveSnapshot(player.getFrameSnapshot(), "03b_games_expanded");
            dumpNavigatorList(player.getFrameSnapshot(), "03b_games_expanded");
            writePublicRoomIds(player);

            System.out.printf("%n--- Clicking BattleBall row at (%d,%d) ---%n",
                    BATTLEBALL_ROW_X, BATTLEBALL_ROW_Y);
            performClick(player, BATTLEBALL_ROW_X, BATTLEBALL_ROW_Y, "04_battleball_row", 30);
            saveSnapshot(player.getFrameSnapshot(), "04b_after_battleball_row");

            System.out.printf("%n--- Clicking bottom panel Go button at (%d,%d) ---%n",
                    GO_BUTTON_X, GO_BUTTON_Y);
            performClick(player, GO_BUTTON_X, GO_BUTTON_Y, "05_panel_go", 30);

            System.out.println("\nWaiting for room to load (ticking 600 frames ~40s)...");
            for (int i = 0; i < 600; i++) {
                try {
                    player.tick();
                } catch (Exception ignored) {}
                Thread.sleep(67);
                if (i % 60 == 0) {
                    FrameSnapshot snap = player.getFrameSnapshot();
                    saveSnapshot(snap, "06_wait_frame_" + i);
                    System.out.printf("  room load tick %d, sprites=%d%n", i, snap.sprites().size());
                }
            }

            saveSnapshot(player.getFrameSnapshot(), "07_room_loaded");
            tickAndSleep(player, 200);
            saveSnapshot(player.getFrameSnapshot(), "08_room_settled");

            Files.writeString(OUTPUT_DIR.resolve("errors.txt"),
                    String.join(System.lineSeparator(), allErrors));
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
                        s.isVisible() && s.getX() >= NAV_X && s.getY() >= NAV_Y && s.getChannel() >= 60);
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

    private static void performClick(Player player, int x, int y, String stem, int settleTicks) throws Exception {
        FrameSnapshot beforeSnap = player.getFrameSnapshot();
        saveSnapshot(beforeSnap, stem + "_before");

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
        saveSnapshot(afterSnap, stem);

        double change = NavigatorClickTest.computeRegionChangeFraction(
                beforeSnap.renderFrame().toBufferedImage(),
                afterSnap.renderFrame().toBufferedImage(),
                0, 0,
                beforeSnap.renderFrame().getWidth(),
                beforeSnap.renderFrame().getHeight());
        System.out.printf(Locale.ROOT, "  click (%d,%d) -> screen change %.2f%%%n", x, y, change * 100.0);
    }

    private static void saveSnapshot(FrameSnapshot snapshot, String stem) throws Exception {
        Bitmap bitmap = snapshot.renderFrame();
        NavigatorSSOTest.savePng(bitmap, OUTPUT_DIR.resolve(stem + ".png"));
        NavigatorSSOTest.dumpSpriteInfo(snapshot, OUTPUT_DIR.resolve(stem + "_sprite_info.txt"));
    }

    private static void tickAndSleep(Player player, int ticks) throws Exception {
        for (int i = 0; i < ticks; i++) {
            try {
                player.tick();
            } catch (Exception ignored) {}
            Thread.sleep(67);
        }
    }

    private static void dumpNavigatorList(FrameSnapshot snap, String stem) throws Exception {
        StringBuilder sb = new StringBuilder();
        sb.append("=== Navigator List Sprites ===\n");
        for (var sprite : snap.sprites()) {
            if (!sprite.isVisible()) {
                continue;
            }
            if (sprite.getX() < NAV_X || sprite.getY() < NAV_Y) {
                continue;
            }
            sb.append(String.format(Locale.ROOT,
                    "ch=%d pos=(%d,%d) size=(%dx%d) member='%s'%n",
                    sprite.getChannel(),
                    sprite.getX(),
                    sprite.getY(),
                    sprite.getWidth(),
                    sprite.getHeight(),
                    sprite.getMemberName()));
        }
        Files.writeString(OUTPUT_DIR.resolve(stem + "_navigator_list.txt"), sb.toString());
    }

    private static void writePublicRoomIds(Player player) throws Exception {
        Map<String, String> found = withProviders(player, () -> {
            Map<String, String> ids = new LinkedHashMap<>();
            Datum navigatorThread = player.getVM().callHandler("getThread", List.of(Datum.symbol("navigator")));
            if (!(navigatorThread instanceof Datum.ScriptInstance threadInstance)) {
                throw new IllegalStateException("Navigator thread not found");
            }

            Datum component = com.libreshockwave.vm.builtin.flow.ControlFlowBuiltins.callHandlerOnInstance(
                    player.getVM(), threadInstance, "getComponent", List.of());
            if (!(component instanceof Datum.ScriptInstance componentInstance)) {
                throw new IllegalStateException("Navigator component not found");
            }

            Datum cacheDatum = AncestorChainWalker.getProperty(componentInstance, "pNodeCache");
            if (!(cacheDatum instanceof Datum.PropList cache)) {
                throw new IllegalStateException("Navigator pNodeCache missing: " + cacheDatum);
            }

            for (int i = 0; i < cache.size(); i++) {
                Datum nodeDatum = cache.getValue(i);
                if (!(nodeDatum instanceof Datum.PropList nodeInfo)) {
                    continue;
                }
                Datum childrenDatum = nodeInfo.get("children");
                if (!(childrenDatum instanceof Datum.PropList children)) {
                    continue;
                }
                for (int j = 0; j < children.size(); j++) {
                    String nodeId = children.getKey(j);
                    Datum childDatum = children.getValue(j);
                    if (!(childDatum instanceof Datum.PropList childInfo)) {
                        continue;
                    }
                    Datum unitStr = childInfo.get("unitStrId");
                    if (unitStr instanceof Datum.Str str) {
                        ids.putIfAbsent(str.value().toLowerCase(Locale.ROOT), nodeId);
                    }
                }
            }
            return ids;
        });

        StringBuilder sb = new StringBuilder();
        for (Map.Entry<String, String> entry : found.entrySet()) {
            sb.append(entry.getKey()).append(" -> ").append(entry.getValue()).append(System.lineSeparator());
        }
        Files.writeString(OUTPUT_DIR.resolve("public_room_ids.txt"), sb.toString());
    }

    private static <T> T withProviders(Player player, Callable<T> action) throws Exception {
        var castLibField = Player.class.getDeclaredField("castLibManager");
        castLibField.setAccessible(true);
        var spriteField = Player.class.getDeclaredField("spriteProperties");
        spriteField.setAccessible(true);
        var movieField = Player.class.getDeclaredField("movieProperties");
        movieField.setAccessible(true);
        var netField = Player.class.getDeclaredField("netManager");
        netField.setAccessible(true);
        var xtraField = Player.class.getDeclaredField("xtraManager");
        xtraField.setAccessible(true);
        var timeoutField = Player.class.getDeclaredField("timeoutManager");
        timeoutField.setAccessible(true);
        var soundField = Player.class.getDeclaredField("soundManager");
        soundField.setAccessible(true);
        var externalParamField = Player.class.getDeclaredField("externalParamProvider");
        externalParamField.setAccessible(true);

        try {
            NetBuiltins.setProvider((com.libreshockwave.player.net.NetManager) netField.get(player));
            XtraBuiltins.setManager((com.libreshockwave.vm.xtra.XtraManager) xtraField.get(player));
            CastLibProvider.setProvider((com.libreshockwave.player.cast.CastLibManager) castLibField.get(player));
            SpritePropertyProvider.setProvider(
                    (com.libreshockwave.vm.builtin.sprite.SpritePropertyProvider) spriteField.get(player));
            MoviePropertyProvider.setProvider(
                    (com.libreshockwave.vm.builtin.movie.MoviePropertyProvider) movieField.get(player));
            TimeoutProvider.setProvider((com.libreshockwave.player.timeout.TimeoutManager) timeoutField.get(player));
            UpdateProvider.setProvider(player);
            ExternalParamProvider.setProvider(
                    (com.libreshockwave.vm.builtin.net.ExternalParamProvider) externalParamField.get(player));
            SoundProvider.setProvider((com.libreshockwave.player.audio.SoundManager) soundField.get(player));
            return action.call();
        } finally {
            NetBuiltins.clearProvider();
            XtraBuiltins.clearManager();
            CastLibProvider.clearProvider();
            SpritePropertyProvider.clearProvider();
            MoviePropertyProvider.clearProvider();
            TimeoutProvider.clearProvider();
            UpdateProvider.clearProvider();
            ExternalParamProvider.clearProvider();
            SoundProvider.clearProvider();
        }
    }
}
