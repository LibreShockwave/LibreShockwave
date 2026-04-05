package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.player.render.pipeline.FrameSnapshot;
import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.builtin.flow.ControlFlowBuiltins;
import com.libreshockwave.vm.builtin.flow.UpdateProvider;
import com.libreshockwave.vm.builtin.movie.MoviePropertyProvider;
import com.libreshockwave.vm.builtin.net.ExternalParamProvider;
import com.libreshockwave.vm.builtin.net.NetBuiltins;
import com.libreshockwave.vm.builtin.media.SoundProvider;
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

/**
 * Regression harness for switching between two public rooms through the
 * navigator-backed room entry flow.
 *
 * Usage:
 *   ./gradlew :player-core:runPublicRoomSwitchTest
 */
public final class PublicRoomSwitchTest {

    private static final int NAV_X = 350;
    private static final int NAV_Y = 60;
    private static final int PUBLIC_SPACES_X = 421;
    private static final int PUBLIC_SPACES_Y = 76;
    private static final int ROOM_ROW_GO_X = 657;
    private static final int WELCOME_LOUNGE_GO_Y = 137;
    private static final int PANEL_GO_X = 671;
    private static final int PANEL_GO_Y = 441;

    private static final String FIRST_ROOM_UNIT = "welcome_lounge";
    private static final String SECOND_ROOM_UNIT = "theatredrome";

    private static final Path OUTPUT_DIR = Path.of("build/public-room-switch");

    private PublicRoomSwitchTest() {}

    public static void main(String[] args) throws Exception {
        Files.createDirectories(OUTPUT_DIR);
        System.out.println("=== Public Room Switch Test ===");

        Player player = createPlayer();
        List<String> allErrors = new ArrayList<>();
        player.setErrorListener((msg, ex) -> {
            allErrors.add(msg);
            System.out.println("[Error] " + msg);
        });

        try {
            player.getVM().addTraceHandler("registerclient");
            player.getVM().addTraceHandler("registerprocedure");
            player.getVM().addTraceHandler("closethread");
            player.getVM().addTraceHandler("leaveroom");
            player.getVM().addTraceHandler("roomconnected");
            player.getVM().addTraceHandler("showroombar");
            player.getVM().addTraceHandler("showinterface");
            player.getVM().addTraceHandler("handle_interstitialdata");
            player.getVM().addTraceHandler("roomprepartfinished");
            player.getVM().addTraceHandler("adfinished");
            player.getVM().addTraceHandler("connect");
            player.getVM().addTraceHandler("disconnect");
            player.getVM().addTraceHandler("send");
            player.getVM().addTraceHandler("xtramsghandler");

            startAndLoadNavigator(player);
            saveSnapshot(player.getFrameSnapshot(), "01_navigator_loaded");

            System.out.printf("%n--- Clicking Public Spaces tab at (%d,%d) ---%n",
                    PUBLIC_SPACES_X, PUBLIC_SPACES_Y);
            click(player, PUBLIC_SPACES_X, PUBLIC_SPACES_Y, 45);
            saveSnapshot(player.getFrameSnapshot(), "02_public_spaces");

            Map<String, String> publicRooms = discoverPublicRoomIds(player);
            String firstRoomId = publicRooms.get(FIRST_ROOM_UNIT);
            String secondRoomId = publicRooms.get(SECOND_ROOM_UNIT);
            if (firstRoomId == null || secondRoomId == null) {
                throw new IllegalStateException("Failed to discover both room IDs from navigator cache: "
                        + publicRooms);
            }

            Files.writeString(OUTPUT_DIR.resolve("public_room_ids.txt"),
                    "first=" + FIRST_ROOM_UNIT + " -> " + firstRoomId + System.lineSeparator()
                            + "second=" + SECOND_ROOM_UNIT + " -> " + secondRoomId + System.lineSeparator()
                            + "all=" + publicRooms + System.lineSeparator());

            System.out.printf("%n--- Clicking %s row at (%d,%d) ---%n",
                    FIRST_ROOM_UNIT, ROOM_ROW_GO_X, WELCOME_LOUNGE_GO_Y);
            click(player, ROOM_ROW_GO_X, WELCOME_LOUNGE_GO_Y, 30);
            saveSnapshot(player.getFrameSnapshot(), "03_first_room_row");

            System.out.printf("%n--- Clicking bottom panel Go button at (%d,%d) ---%n",
                    PANEL_GO_X, PANEL_GO_Y);
            click(player, PANEL_GO_X, PANEL_GO_Y, 30);
            saveSnapshot(player.getFrameSnapshot(), "04_first_room_go");

            waitForRoomSwitch(player, "05_first_room_wait", allErrors, 300);
            saveSnapshot(player.getFrameSnapshot(), "04_first_room_settled");
            writeDiag(player, "after_first_room", allErrors);

            enterPublicRoom(player, secondRoomId, SECOND_ROOM_UNIT, "06_second_room");
            waitForRoomSwitch(player, "07_second_room_wait", allErrors, 600);
            writeDiag(player, "after_second_room", allErrors);

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

    private static void enterPublicRoom(Player player, String roomId, String unitName, String stem) throws Exception {
        System.out.printf("%n--- Entering %s (%s) ---%n", unitName, roomId);
        withProviders(player, () -> {
            Datum navigatorThread = player.getVM().callHandler("getThread", List.of(Datum.symbol("navigator")));
            if (!(navigatorThread instanceof Datum.ScriptInstance threadInstance)) {
                throw new IllegalStateException("Navigator thread not found");
            }

            Datum component = ControlFlowBuiltins.callHandlerOnInstance(
                    player.getVM(), threadInstance, "getComponent", List.of());
            if (!(component instanceof Datum.ScriptInstance componentInstance)) {
                throw new IllegalStateException("Navigator component not found");
            }

            ControlFlowBuiltins.callHandlerOnInstance(
                    player.getVM(),
                    componentInstance,
                    "prepareRoomEntry",
                    List.of(Datum.of(roomId), Datum.symbol("public")));
            return null;
        });
        saveSnapshot(player.getFrameSnapshot(), stem + "_issued");
    }

    private static Map<String, String> discoverPublicRoomIds(Player player) throws Exception {
        return withProviders(player, () -> {
            Map<String, String> found = new LinkedHashMap<>();
            Datum navigatorThread = player.getVM().callHandler("getThread", List.of(Datum.symbol("navigator")));
            if (!(navigatorThread instanceof Datum.ScriptInstance threadInstance)) {
                throw new IllegalStateException("Navigator thread not found");
            }

            Datum component = ControlFlowBuiltins.callHandlerOnInstance(
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
                        found.putIfAbsent(str.value().toLowerCase(Locale.ROOT), nodeId);
                    }
                }
            }
            return found;
        });
    }

    private static void waitForRoomSwitch(Player player, String stem, List<String> allErrors, int frames)
            throws Exception {
        System.out.printf("Waiting for room state to settle (ticking %d frames)...%n", frames);
        for (int i = 0; i < frames; i++) {
            try {
                player.tick();
            } catch (Exception ignored) {}
            Thread.sleep(67);

            if (i % 30 == 0) {
                writeDiag(player, stem + "_tick_" + i, allErrors);
            }

            if (i % 60 == 0) {
                FrameSnapshot snap = player.getFrameSnapshot();
                saveSnapshot(snap, stem + "_frame_" + i);
                System.out.printf("  second-room tick %d, sprites=%d%n", i, snap.sprites().size());
            }
        }
    }

    private static void click(Player player, int x, int y, int settleTicks) throws Exception {
        player.getInputHandler().onMouseMove(x, y);
        tickAndSleep(player, 1);
        player.getInputHandler().onMouseDown(x, y, false);
        tickAndSleep(player, 1);
        player.getInputHandler().onMouseUp(x, y, false);
        tickAndSleep(player, settleTicks);
    }

    private static void saveSnapshot(FrameSnapshot snap, String stem) throws Exception {
        Bitmap frame = snap.renderFrame();
        NavigatorSSOTest.savePng(frame, OUTPUT_DIR.resolve(stem + ".png"));
        NavigatorSSOTest.dumpSpriteInfo(snap, OUTPUT_DIR.resolve(stem + "_sprite_info.txt"));
    }

    private static void tickAndSleep(Player player, int ticks) throws Exception {
        for (int i = 0; i < ticks; i++) {
            try {
                player.tick();
            } catch (Exception ignored) {}
            Thread.sleep(67);
        }
    }

    private static void writeDiag(Player player, String stem, List<String> allErrors) throws Exception {
        String diag = withProviders(player, () -> {
            Datum roomThread = player.getVM().callHandler("getThread", List.of(Datum.symbol("room")));
            Datum roomInterface = player.getVM().callHandler("getObject", List.of(Datum.symbol("room_interface")));
            Datum roomHandler = player.getVM().callHandler("getObject", List.of(Datum.symbol("room_handler")));
            Datum roomComponentObject = player.getVM().callHandler("getObject", List.of(Datum.symbol("room_component")));
            Datum roomBar = player.getVM().callHandler("getObject", List.of(Datum.of("Room_bar")));
            Datum session = player.getVM().callHandler("getObject", List.of(Datum.symbol("session")));
            Datum connectionManager = player.getVM().callHandler("getObject", List.of(Datum.symbol("connection_manager")));
            Datum interstitial = player.getVM().callHandler("getObject", List.of(Datum.of("Interstitial system")));

            StringBuilder sb = new StringBuilder();
            sb.append("roomThread=").append(describeDatum(roomThread)).append(System.lineSeparator());
            sb.append("roomInterface=").append(describeDatum(roomInterface)).append(System.lineSeparator());
            sb.append("roomHandler=").append(describeDatum(roomHandler)).append(System.lineSeparator());
            sb.append("roomComponentObject=").append(describeDatum(roomComponentObject)).append(System.lineSeparator());
            sb.append("roomBar=").append(describeDatum(roomBar)).append(System.lineSeparator());
            sb.append("interstitial=").append(describeDatum(interstitial)).append(System.lineSeparator());
            if (roomThread instanceof Datum.ScriptInstance roomThreadInstance) {
                Datum threadHandler = ControlFlowBuiltins.callHandlerOnInstance(
                        player.getVM(), roomThreadInstance, "getHandler", List.of());
                Datum component = ControlFlowBuiltins.callHandlerOnInstance(
                        player.getVM(), roomThreadInstance, "getComponent", List.of());
                sb.append("threadHandler=").append(describeDatum(threadHandler)).append(System.lineSeparator());
                sb.append("roomComponent=").append(describeDatum(component)).append(System.lineSeparator());
                if (component instanceof Datum.ScriptInstance componentInstance) {
                    sb.append("pRoomId=").append(AncestorChainWalker.getProperty(componentInstance, "pRoomId"))
                            .append(System.lineSeparator());
                    sb.append("pActiveFlag=").append(AncestorChainWalker.getProperty(componentInstance, "pActiveFlag"))
                            .append(System.lineSeparator());
                    sb.append("pProcessList=").append(AncestorChainWalker.getProperty(componentInstance, "pProcessList"))
                            .append(System.lineSeparator());
                    sb.append("pSaveData=").append(AncestorChainWalker.getProperty(componentInstance, "pSaveData"))
                            .append(System.lineSeparator());
                    sb.append("pCastLoaded=").append(AncestorChainWalker.getProperty(componentInstance, "pCastLoaded"))
                            .append(System.lineSeparator());
                    Datum roomConnection = ControlFlowBuiltins.callHandlerOnInstance(
                            player.getVM(), componentInstance, "getRoomConnection", List.of());
                    sb.append("roomConnection=").append(describeDatum(roomConnection)).append(System.lineSeparator());
                    if (roomConnection instanceof Datum.ScriptInstance roomConnectionInstance) {
                        sb.append("roomConnection.pConnectionShouldBeKilled=")
                                .append(AncestorChainWalker.getProperty(
                                        roomConnectionInstance, "pConnectionShouldBeKilled"))
                                .append(System.lineSeparator());
                        sb.append("roomConnection.pConnectionOk=")
                                .append(AncestorChainWalker.getProperty(roomConnectionInstance, "pConnectionOk"))
                                .append(System.lineSeparator());
                        sb.append("roomConnection.pConnectionSecured=")
                                .append(AncestorChainWalker.getProperty(
                                        roomConnectionInstance, "pConnectionSecured"))
                                .append(System.lineSeparator());
                        sb.append("roomConnection.pHost=")
                                .append(AncestorChainWalker.getProperty(roomConnectionInstance, "pHost"))
                                .append(System.lineSeparator());
                        sb.append("roomConnection.pPort=")
                                .append(AncestorChainWalker.getProperty(roomConnectionInstance, "pPort"))
                                .append(System.lineSeparator());
                        sb.append("roomConnection.pXtra=")
                                .append(AncestorChainWalker.getProperty(roomConnectionInstance, "pXtra"))
                                .append(System.lineSeparator());
                    }
                }
            }
            if (interstitial instanceof Datum.ScriptInstance interstitialInstance) {
                sb.append("interstitial.pAdFinished=")
                        .append(AncestorChainWalker.getProperty(interstitialInstance, "pAdFinished"))
                        .append(System.lineSeparator());
                sb.append("interstitial.pAdLoaded=")
                        .append(AncestorChainWalker.getProperty(interstitialInstance, "pAdLoaded"))
                        .append(System.lineSeparator());
                sb.append("interstitial.pAdError=")
                        .append(AncestorChainWalker.getProperty(interstitialInstance, "pAdError"))
                        .append(System.lineSeparator());
                sb.append("interstitial.pClickURL=")
                        .append(AncestorChainWalker.getProperty(interstitialInstance, "pClickURL"))
                        .append(System.lineSeparator());
            }
            if (connectionManager instanceof Datum.ScriptInstance connectionManagerInstance) {
                sb.append("connectionManager.pListenerList=")
                        .append(AncestorChainWalker.getProperty(connectionManagerInstance, "pListenerList"))
                        .append(System.lineSeparator());
                sb.append("connectionManager.pCommandsList=")
                        .append(AncestorChainWalker.getProperty(connectionManagerInstance, "pCommandsList"))
                        .append(System.lineSeparator());
            }
            if (session instanceof Datum.ScriptInstance sessionInstance) {
                Datum lastRoom = ControlFlowBuiltins.callHandlerOnInstance(
                        player.getVM(), sessionInstance, "GET", List.of(Datum.of("lastroom")));
                sb.append("session.lastroom=").append(lastRoom).append(System.lineSeparator());
            }
            if (!allErrors.isEmpty()) {
                sb.append("lastError=").append(allErrors.getLast()).append(System.lineSeparator());
            }
            return sb.toString();
        });

        Files.writeString(OUTPUT_DIR.resolve(stem + ".txt"), diag);
        System.out.printf("[%s]%n%s", stem, diag);
    }

    private static String describeDatum(Datum datum) {
        if (datum == null) {
            return "<null>";
        }
        if (datum.isVoid()) {
            return "<VOID>";
        }
        return datum.getClass().getSimpleName() + ":" + datum;
    }

    @FunctionalInterface
    private interface ProviderAction<T> {
        T run() throws Exception;
    }

    private static <T> T withProviders(Player player, ProviderAction<T> action) throws Exception {
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
        try {
            return action.run();
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
