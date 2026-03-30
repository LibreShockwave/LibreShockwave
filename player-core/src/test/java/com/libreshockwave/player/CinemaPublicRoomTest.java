package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.lingo.decompiler.LingoDecompiler;
import com.libreshockwave.player.cast.CastMember;
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
 * Diagnostic runner for the public-space Cinema room.
 *
 * Flow:
 * 1. SSO login, wait for hotel view + navigator
 * 2. Click "Public Spaces"
 * 3. Click "Entertainment" open button
 * 4. Click the first visible "Go" row in the expanded entertainment list
 * 5. Wait for room load and dump screenshots + sprite metadata
 *
 * Usage:
 *   ./gradlew :player-core:runCinemaPublicRoomTest
 */
public class CinemaPublicRoomTest {

    private static final int NAV_X = 350, NAV_Y = 60, NAV_W = 370, NAV_H = 440;

    private static final int PUBLIC_SPACES_X = 421, PUBLIC_SPACES_Y = 76;
    private static final int ENTERTAINMENT_OPEN_X = 657, ENTERTAINMENT_OPEN_Y = 197;
    private static final int CINEMA_GO_X = 657, CINEMA_GO_Y = 141;
    private static final int GO_BUTTON_X = 671, GO_BUTTON_Y = 441;

    private static final Path OUTPUT_DIR = Path.of("build/cinema-public-room");

    public static void main(String[] args) throws Exception {
        boolean extractOnly = args.length > 0 && "--extract-only".equals(args[0]);
        Files.createDirectories(OUTPUT_DIR);
        System.out.println("=== Cinema Public Room Test ===");

        Player player = createPlayer();
        List<String> allErrors = new ArrayList<>();
        player.setErrorListener((msg, ex) -> {
            allErrors.add(msg);
            if (allErrors.size() <= 50) {
                System.out.println("[Error] " + msg);
            }
        });

        try {
            player.play();
            player.preloadAllCasts();
            System.out.println("Waiting 8000ms for casts to load...");
            Thread.sleep(8000);

            if (extractOnly) {
                dumpStandaloneCinemaCast(player);
                Files.writeString(OUTPUT_DIR.resolve("errors.txt"), String.join(System.lineSeparator(), allErrors));
                return;
            }

            startAndLoadNavigator(player);

            FrameSnapshot navSnap = player.getFrameSnapshot();
            saveSnapshot(navSnap, "01_navigator_loaded");

            System.out.println("\n--- Clicking Public Spaces tab ---");
            performClick(player, PUBLIC_SPACES_X, PUBLIC_SPACES_Y, "02_public_spaces", 45);

            FrameSnapshot publicSpacesSnap = player.getFrameSnapshot();
            saveSnapshot(publicSpacesSnap, "02b_public_spaces_list");

            System.out.println("\n--- Clicking Entertainment open button ---");
            performClick(player, ENTERTAINMENT_OPEN_X, ENTERTAINMENT_OPEN_Y, "03_entertainment_open", 45);

            FrameSnapshot entertainmentSnap = player.getFrameSnapshot();
            saveSnapshot(entertainmentSnap, "03b_entertainment_expanded");

            System.out.println("\n--- Clicking Cinema room Go button ---");
            performClick(player, CINEMA_GO_X, CINEMA_GO_Y, "04_cinema_go", 30);

            FrameSnapshot afterRowClick = player.getFrameSnapshot();
            saveSnapshot(afterRowClick, "04b_after_row_go");

            System.out.println("\n--- Clicking bottom panel Go button ---");
            performClick(player, GO_BUTTON_X, GO_BUTTON_Y, "04c_panel_go", 30);

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
            saveSnapshot(roomSnap, "05_room_loaded");
            dumpRoomSprites(roomSnap);

            System.out.println("\nTicking 200 extra frames for room to settle...");
            tickAndSleep(player, 200);

            FrameSnapshot settledSnap = player.getFrameSnapshot();
            saveSnapshot(settledSnap, "06_room_settled");
            dumpRoomSprites(settledSnap);
            dumpCinemaMembers(player,
                    "lite", "hiliter_pub",
                    "VizWrap_roomShadow_uid:43:37701",
                    "light_blue", "light_red",
                    "tv_studio_spotlights1", "tv_studio_spotlights2",
                    "lightpole_a_0_0_0", "lightpole_b_0_0_0",
                    "cinema_mask1", "cinema_mask2", "cinema_mask3", "cinema_mask4", "cinema_mask5");

            Files.writeString(OUTPUT_DIR.resolve("errors.txt"), String.join(System.lineSeparator(), allErrors));
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
            try { player.tick(); } catch (Exception ignored) {}

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

        double navChange = NavigatorClickTest.computeRegionChangeFraction(
                beforeSnap.renderFrame().toBufferedImage(),
                afterSnap.renderFrame().toBufferedImage(),
                0, 0,
                beforeSnap.renderFrame().getWidth(),
                beforeSnap.renderFrame().getHeight());
        System.out.printf(Locale.ROOT,
                "  click (%d,%d) -> screen change %.2f%%%n", x, y, navChange * 100.0);
    }

    private static void saveSnapshot(FrameSnapshot snapshot, String stem) throws Exception {
        Bitmap bitmap = snapshot.renderFrame();
        NavigatorSSOTest.savePng(bitmap, OUTPUT_DIR.resolve(stem + ".png"));
        NavigatorSSOTest.dumpSpriteInfo(snapshot, OUTPUT_DIR.resolve(stem + "_sprite_info.txt"));
    }

    private static void dumpRoomSprites(FrameSnapshot snapshot) throws Exception {
        StringBuilder sb = new StringBuilder();
        sb.append("=== Room Sprites ===\n");
        sb.append(String.format("Total sprites: %d%n", snapshot.sprites().size()));
        for (var sprite : snapshot.sprites()) {
            String vis = sprite.isVisible() ? "VIS" : "HID";
            sb.append(String.format("[%s] ch=%d loc=(%d,%d) size=(%dx%d) ink=%d blend=%d foreColor=%d backColor=%d hasFore=%s hasBack=%s member='%s'",
                    vis,
                    sprite.getChannel(),
                    sprite.getX(),
                    sprite.getY(),
                    sprite.getWidth(),
                    sprite.getHeight(),
                    sprite.getInk(),
                    sprite.getBlend(),
                    sprite.getForeColor(),
                    sprite.getBackColor(),
                    sprite.hasForeColor(),
                    sprite.hasBackColor(),
                    sprite.getMemberName()));
            Bitmap baked = sprite.getBakedBitmap();
            if (baked != null && sprite.isVisible() && baked.getWidth() > 0 && baked.getHeight() > 0) {
                int cx = baked.getWidth() / 2;
                int cy = baked.getHeight() / 2;
                sb.append(String.format(" centerPixel=0x%08X", baked.getPixel(cx, cy)));
            }
            sb.append("\n");
        }
        String stem = snapshot.frameNumber() > 0 ? "room_sprites_frame" + snapshot.frameNumber() : "room_sprites";
        Files.writeString(OUTPUT_DIR.resolve(stem + ".txt"), sb.toString());
    }

    private static void tickAndSleep(Player player, int ticks) throws Exception {
        for (int i = 0; i < ticks; i++) {
            try { player.tick(); } catch (Exception ignored) {}
            Thread.sleep(67);
        }
    }

    private static void dumpCinemaMembers(Player player, String... memberNames) throws Exception {
        var castLibManager = getCastLibManager(player);
        if (castLibManager == null) {
            return;
        }

        StringBuilder sb = new StringBuilder();
        sb.append("=== Cinema Member Dumps ===\n");
        for (String memberName : memberNames) {
            CastMember runtimeMember = castLibManager.findCastMemberByName(memberName);
            if (runtimeMember != null) {
                Bitmap runtimeBitmap = runtimeMember.getBitmap();
                if (runtimeBitmap != null) {
                    NavigatorSSOTest.savePng(runtimeBitmap, OUTPUT_DIR.resolve(memberName + "_runtime.png"));
                    int cx = runtimeBitmap.getWidth() / 2;
                    int cy = runtimeBitmap.getHeight() / 2;
                    sb.append(String.format(Locale.ROOT,
                            "%s runtime=%dx%d center=0x%08X regPoint=(%d,%d)%n",
                            memberName,
                            runtimeBitmap.getWidth(),
                            runtimeBitmap.getHeight(),
                            runtimeBitmap.getPixel(cx, cy),
                            runtimeMember.getRegPointX(),
                            runtimeMember.getRegPointY()));
                } else {
                    sb.append(memberName).append(" runtime bitmap: null\n");
                }
            } else {
                sb.append(memberName).append(" runtime member: null\n");
            }

            var chunk = castLibManager.getCastMemberByName(memberName);
            if (chunk != null) {
                var decoded = player.getBitmapResolver().decodeBitmap(chunk);
                if (decoded.isPresent()) {
                    Bitmap raw = decoded.get();
                    NavigatorSSOTest.savePng(raw, OUTPUT_DIR.resolve(memberName + "_decoded.png"));
                    int cx = raw.getWidth() / 2;
                    int cy = raw.getHeight() / 2;
                    sb.append(String.format(Locale.ROOT,
                            "%s decoded=%dx%d center=0x%08X%n",
                            memberName,
                            raw.getWidth(),
                            raw.getHeight(),
                            raw.getPixel(cx, cy)));
                } else {
                    sb.append(memberName).append(" decoded: FAILED\n");
                }
            } else {
                sb.append(memberName).append(" chunk: null\n");
            }
        }

        Files.writeString(OUTPUT_DIR.resolve("cinema_member_dump.txt"), sb.toString());
    }

    private static void dumpStandaloneCinemaCast(Player player) throws Exception {
        byte[] data = NavigatorSSOTest.httpGet("https://sandbox.h4bbo.net/dcr/14.1_b8/hh_room_cinema.cct");
        if (!player.loadExternalCastFromCachedData(12, data)) {
            throw new IllegalStateException("Failed to load hh_room_cinema.cct into cast 12");
        }
        dumpCinemaTextMembers(player, 12);
        dumpCinemaScripts(player, 12);
        dumpCinemaMembers(player,
                "lite", "hiliter_pub",
                "light_blue", "light_red",
                "tv_studio_spotlights1", "tv_studio_spotlights2",
                "lightpole_a_0_0_0", "lightpole_b_0_0_0",
                "cinema_mask1", "cinema_mask2", "cinema_mask3", "cinema_mask4", "cinema_mask5");
    }

    private static void dumpCinemaTextMembers(Player player, int castLibNumber) throws Exception {
        var castLibManager = getCastLibManager(player);
        var castLib = castLibManager.getCastLib(castLibNumber);
        if (castLib == null) {
            return;
        }

        StringBuilder index = new StringBuilder();
        index.append("=== Cinema Text Members ===\n");
        for (var entry : castLib.getMemberChunks().entrySet()) {
            int memberNumber = entry.getKey();
            var chunk = entry.getValue();
            String name = chunk.name() == null ? "" : chunk.name();
            if (chunk.memberType() != MemberType.TEXT && !name.contains(".room")) {
                continue;
            }

            CastMember member = castLib.getMember(memberNumber);
            String text = member != null ? member.getTextContent() : "";
            index.append(String.format(Locale.ROOT, "#%d type=%s name='%s' len=%d%n",
                    memberNumber, chunk.memberType(), name, text.length()));

            if (!text.isEmpty()) {
                String safeName = name.isEmpty() ? ("member_" + memberNumber) : name;
                safeName = safeName.replaceAll("[^A-Za-z0-9._-]", "_");
                Files.writeString(OUTPUT_DIR.resolve("text_" + memberNumber + "_" + safeName + ".txt"), text);
            }
        }
        Files.writeString(OUTPUT_DIR.resolve("cinema_text_members.txt"), index.toString());
    }

    private static void dumpCinemaScripts(Player player, int castLibNumber) throws Exception {
        var castLibManager = getCastLibManager(player);
        var castLib = castLibManager.getCastLib(castLibNumber);
        if (castLib == null || castLib.getSourceFile() == null) {
            return;
        }

        DirectorFile sourceFile = castLib.getSourceFile();
        StringBuilder summary = new StringBuilder();
        summary.append("=== Cinema Scripts ===\n");
        LingoDecompiler decompiler = new LingoDecompiler();

        for (var script : sourceFile.getScripts()) {
            String scriptName = script.getScriptName();
            summary.append(script.getDisplayName()).append("\n");
            for (var handler : script.handlers()) {
                summary.append("  - ").append(script.getHandlerName(handler)).append("\n");
            }

            String normalizedName = scriptName == null ? "" : scriptName.toLowerCase(Locale.ROOT);
            if (normalizedName.contains("light") || normalizedName.contains("cinema")
                    || normalizedName.contains("shadow") || normalizedName.contains("wrapper")) {
                String decompiled = decompiler.decompile(script, sourceFile.getScriptNamesForScript(script));
                String safeName = scriptName == null || scriptName.isEmpty()
                        ? ("script_" + script.id().value())
                        : scriptName.replaceAll("[^A-Za-z0-9._-]", "_");
                Files.writeString(OUTPUT_DIR.resolve("script_" + safeName + ".lingo.txt"), decompiled);
            }
        }

        Files.writeString(OUTPUT_DIR.resolve("cinema_scripts.txt"), summary.toString());
    }

    private static com.libreshockwave.player.cast.CastLibManager getCastLibManager(Player player) throws Exception {
        var f = Player.class.getDeclaredField("castLibManager");
        f.setAccessible(true);
        return (com.libreshockwave.player.cast.CastLibManager) f.get(player);
    }
}
