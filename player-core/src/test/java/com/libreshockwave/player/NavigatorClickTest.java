package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.player.cast.CastMember;
import com.libreshockwave.player.input.HitTester;
import com.libreshockwave.player.render.pipeline.FrameSnapshot;
import com.libreshockwave.player.render.pipeline.RenderSprite;

import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.*;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.*;

/**
 * Navigator category row click test: loads habbo.dcr, waits for the navigator
 * to appear with Guest Rooms categories, clicks the first category row
 * ("Restaurant, Bar & Night Club Rooms"), and verifies the navigator changes
 * to show that category expanded.
 *
 * Reference images:
 *   docs/navigator-before-click.png        — navigator with category list
 *   docs/navigator-before-after-restaurant-category.png — after clicking first category
 *
 * Requires: Habbo game server on au.h4bbo.net:30101 and MUS on au.h4bbo.net:38101.
 *
 * Usage:
 *   ./gradlew :player-core:runNavigatorClickTest
 */
public class NavigatorClickTest {

    private static final String DCR_URL = "https://sandbox.h4bbo.net/dcr/14.1_b8/habbo.dcr";

    // Navigator region (right side of stage)
    private static final int NAV_X = 350, NAV_Y = 60, NAV_W = 370, NAV_H = 440;

    private static final Path OUTPUT_DIR = Path.of("build/navigator-click");

    // Timing
    private static final int TICK_DELAY_MS = 67;        // ~15fps
    private static final int MAX_WAIT_MS   = 120_000;   // 2 minutes max
    private static final int CAST_LOAD_WAIT_MS = 8000;  // wait for casts
    private static final int CLICK_SETTLE_TICKS = 120;
    private static final double CLICK_CHANGE_THRESHOLD = 0.02;

    public static void main(String[] args) throws Exception {
        Files.createDirectories(OUTPUT_DIR);
        System.out.println("=== Navigator Click Test ===");
        System.out.println("Loading DCR from " + DCR_URL);
        System.out.println("Server: au.h4bbo.net:30101 (game) / au.h4bbo.net:38101 (MUS)");

        // 1. Download DCR over HTTP
        byte[] dcrBytes = httpGet(DCR_URL);
        System.out.printf("Downloaded DCR: %d bytes%n", dcrBytes.length);

        DirectorFile dirFile = DirectorFile.load(dcrBytes);
        dirFile.setBasePath("https://sandbox.h4bbo.net/dcr/14.1_b8/");
        Player player = new Player(dirFile);

        // 2. External params
        Map<String, String> params = new LinkedHashMap<>();
        params.put("sw1", "site.url=http://www.habbo.co.uk;url.prefix=http://www.habbo.co.uk");
        params.put("sw2", "connection.info.host=au.h4bbo.net;connection.info.port=30101");
        params.put("sw3", "client.reload.url=https://sandbox.h4bbo.net/");
        params.put("sw4", "connection.mus.host=au.h4bbo.net;connection.mus.port=38101");
        params.put("sw5", "external.variables.txt=https://sandbox.h4bbo.net/gamedata/external_variables.txt;"
                + "external.texts.txt=https://sandbox.h4bbo.net/gamedata/external_texts.txt");
        params.put("sw6", "use.sso.ticket=1;sso.ticket=123");
        player.setExternalParams(params);

        List<String> errors = new ArrayList<>();
        player.setErrorListener((msg, ex) -> {
            errors.add(msg);
            if (errors.size() <= 20) System.out.println("[Error] " + msg);
        });

        // 3. Start playback and wait for external casts to load
        player.play();
        player.preloadAllCasts();
        System.out.println("Waiting " + CAST_LOAD_WAIT_MS + "ms for casts to load...");
        Thread.sleep(CAST_LOAD_WAIT_MS);

        // 4. Tick until hotel view loads (>20 sprites)
        System.out.println("Loading hotel view...");
        long loadStart = System.currentTimeMillis();
        for (int i = 0; i < 3000 && (System.currentTimeMillis() - loadStart) < 60_000; i++) {
            try { player.tick(); } catch (Exception e) { }
            Thread.sleep(10);
            if (i % 50 == 0) {
                int sc = player.getFrameSnapshot().sprites().size();
                System.out.printf("  startup tick %d, sprites=%d, elapsed=%ds%n",
                        i, sc, (System.currentTimeMillis() - loadStart) / 1000);
                if (sc > 20 && i > 100) {
                    System.out.printf("  Hotel view loaded at tick %d (%d sprites)%n", i, sc);
                    for (int j = 0; j < 200; j++) {
                        try { player.tick(); } catch (Exception ignored) {}
                        Thread.sleep(10);
                    }
                    break;
                }
            }
        }

        // 5. Wait for navigator to appear
        Bitmap navigatorBitmap = null;
        int tick = 0;
        long startMs = System.currentTimeMillis();

        System.out.println("Waiting for navigator...");
        while (System.currentTimeMillis() - startMs < MAX_WAIT_MS) {
            try { player.tick(); } catch (Exception e) {
                if (tick < 5) System.out.println("[Tick " + tick + "] " + e.getMessage());
            }

            if (tick % 30 == 0) {
                FrameSnapshot snap = player.getFrameSnapshot();
                boolean hasNavigator = snap.sprites().stream().anyMatch(s ->
                        s.isVisible() && s.getX() >= NAV_X && s.getChannel() >= 60);

                if (hasNavigator && navigatorBitmap == null) {
                    System.out.printf("Navigator appeared at tick %d (%d sprites)%n",
                            tick, snap.sprites().size());
                    navigatorBitmap = snap.renderFrame();
                    break;
                }
                if (tick % 150 == 0) {
                    System.out.printf("  tick %d, sprites=%d, elapsed=%ds%n",
                            tick, snap.sprites().size(), (System.currentTimeMillis() - startMs) / 1000);
                }
            }

            tick++;
            Thread.sleep(TICK_DELAY_MS);
        }

        if (navigatorBitmap == null) {
            System.out.println("FAIL: Navigator did NOT appear within " + MAX_WAIT_MS / 1000 + "s");
            System.out.println("Is the Habbo server running on au.h4bbo.net:30101 and au.h4bbo.net:38101?");
            player.shutdown();
            return;
        }

        // 6. Capture before-click state
        FrameSnapshot beforeSnap = player.getFrameSnapshot();
        Bitmap beforeFrame = beforeSnap.renderFrame();
        BufferedImage beforeImage = beforeFrame.toBufferedImage();
        savePng(beforeFrame, OUTPUT_DIR.resolve("01_before_click.png"));
        dumpSpriteInfo(beforeSnap, OUTPUT_DIR.resolve("01_sprite_info.txt"));

        // Save navigator region crop
        BufferedImage navBefore = cropRegion(beforeImage, NAV_X, NAV_Y, NAV_W, NAV_H);
        ImageIO.write(navBefore, "PNG", OUTPUT_DIR.resolve("01a_nav_before.png").toFile());

        // 7. Find and click the first category row
        System.out.println("\n--- Probing category row click ---");
        RowClickResult result = probeRowClick(player, beforeSnap);

        if (result == null) {
            System.out.println("FAIL: Could not find a category row to click");
            player.shutdown();
            return;
        }

        System.out.printf(Locale.ROOT,
                "Row click result: target=(%d,%d) channel=%d label='%s' hitChannel=%d " +
                        "navChange=%.2f%% changed=%s%n",
                result.clickX, result.clickY, result.targetChannel, result.label,
                result.hitChannel, result.navChangeFraction * 100.0,
                result.navigatorChanged ? "YES" : "NO");

        // 8. Capture after-click state
        BufferedImage afterImage = result.afterSnapshot.renderFrame().toBufferedImage();
        ImageIO.write(afterImage, "PNG", OUTPUT_DIR.resolve("02_after_click.png").toFile());
        dumpSpriteInfo(result.afterSnapshot, OUTPUT_DIR.resolve("02_sprite_info.txt"));

        BufferedImage navAfter = cropRegion(afterImage, NAV_X, NAV_Y, NAV_W, NAV_H);
        ImageIO.write(navAfter, "PNG", OUTPUT_DIR.resolve("02a_nav_after.png").toFile());

        // 9. Create diff images
        BufferedImage navDiff = createPixelDiff(navBefore, navAfter);
        ImageIO.write(navDiff, "PNG", OUTPUT_DIR.resolve("03_nav_diff.png").toFile());

        BufferedImage sbs = createSideBySide(navBefore, navAfter, navDiff);
        ImageIO.write(sbs, "PNG", OUTPUT_DIR.resolve("04_side_by_side.png").toFile());

        // 10. Compare against reference images
        BufferedImage refBefore = loadImage("docs/navigator-before-click.png");
        BufferedImage refAfter = loadImage("docs/navigator-before-after-restaurant-category.png");

        if (refBefore != null) {
            BufferedImage refNavBefore = cropRegion(refBefore, NAV_X, NAV_Y, NAV_W, NAV_H);
            ImageIO.write(refNavBefore, "PNG", OUTPUT_DIR.resolve("05a_ref_nav_before.png").toFile());
            analyzePixelDiffs(navBefore, refNavBefore, "Our before vs Reference before");
        }
        if (refAfter != null) {
            BufferedImage refNavAfter = cropRegion(refAfter, NAV_X, NAV_Y, NAV_W, NAV_H);
            ImageIO.write(refNavAfter, "PNG", OUTPUT_DIR.resolve("05b_ref_nav_after.png").toFile());
            analyzePixelDiffs(navAfter, refNavAfter, "Our after vs Reference after");
        }

        // 11. Final verdict
        System.out.println("\n=== VERDICT ===");
        if (result.navigatorChanged) {
            System.out.println("PASS: Category row click was registered — navigator content changed");
        } else {
            System.out.println("FAIL: Category row click was NOT registered — navigator did not change");
            System.out.printf("  Hit test returned channel %d (expected %d)%n",
                    result.hitChannel, result.targetChannel);
            System.out.printf("  Nav region change: %.2f%% (threshold: %.2f%%)%n",
                    result.navChangeFraction * 100.0, CLICK_CHANGE_THRESHOLD * 100.0);
        }

        if (!errors.isEmpty()) System.out.println("\nTotal errors: " + errors.size());

        player.shutdown();
        System.out.println("\n=== Test complete ===");
        System.out.println("Output: " + OUTPUT_DIR.toAbsolutePath());
    }

    // ---- Row click probing ----

    /**
     * Find the first category row in the navigator and click it.
     * Category rows are the grey bars like "Restaurant, Bar & Night Club Rooms".
     * They are typically large MATTE-ink bitmap sprites in the navigator list area.
     */
    static RowClickResult probeRowClick(Player player, FrameSnapshot beforeSnap) throws Exception {
        // Find the category row target — look for the first clickable row in the navigator list
        RowTarget target = findCategoryRowTarget(player, beforeSnap);
        if (target == null) {
            System.out.println("Could not find a category row target");
            return null;
        }

        System.out.printf("Found category row: label='%s' channel=%d click=(%d,%d)%n",
                target.label, target.channel, target.clickX, target.clickY);

        Bitmap beforeFrame = beforeSnap.renderFrame();
        BufferedImage beforeImage = beforeFrame.toBufferedImage();

        // Hit test at the click point to verify what channel we'll hit
        int hitChannel = HitTester.hitTest(player.getStageRenderer(), beforeSnap.frameNumber(),
                target.clickX, target.clickY,
                channel -> player.getEventDispatcher().isSpriteMouseInteractive(channel));

        System.out.printf("Hit test at (%d,%d): channel=%d (target=%d) match=%s%n",
                target.clickX, target.clickY, hitChannel, target.channel,
                hitChannel == target.channel ? "YES" : "NO");

        // Perform the click: mouseMove -> mouseDown -> mouseUp
        player.getInputHandler().onMouseMove(target.clickX, target.clickY);
        tickAndSleep(player, 2);
        player.getInputHandler().onMouseDown(target.clickX, target.clickY, false);
        tickAndSleep(player, 2);
        player.getInputHandler().onMouseUp(target.clickX, target.clickY, false);
        tickAndSleep(player, 2);

        // Wait for the navigator to update
        double bestChange = 0.0;
        FrameSnapshot bestSnap = player.getFrameSnapshot();
        for (int i = 0; i < CLICK_SETTLE_TICKS; i++) {
            tickAndSleep(player, 1);
            FrameSnapshot snap = player.getFrameSnapshot();
            double change = computeRegionChangeFraction(beforeImage, snap.renderFrame().toBufferedImage(),
                    NAV_X, NAV_Y, NAV_W, NAV_H);
            if (change > bestChange) {
                bestChange = change;
                bestSnap = snap;
            }
            if (change >= CLICK_CHANGE_THRESHOLD) {
                System.out.printf("  Navigator changed at tick +%d (%.2f%%)%n", i, change * 100.0);
                break;
            }
        }

        return new RowClickResult(target.clickX, target.clickY, target.channel, target.label,
                hitChannel, bestChange, bestSnap, bestChange >= CLICK_CHANGE_THRESHOLD);
    }

    /**
     * Find the first category row in the navigator.
     * Category rows are the grey bars in the Guest Rooms list. They are typically:
     * - Bitmap sprites with MATTE ink in the navigator region
     * - Positioned below the tab buttons (y > NAV_Y + 100)
     * - Wide (spanning most of the navigator width)
     * - About 16px tall (one row)
     *
     * We look for sprites with attached behaviors (mouseDown handlers) that are
     * positioned in the navigator list area.
     */
    static RowTarget findCategoryRowTarget(Player player, FrameSnapshot snap) {
        List<RenderSprite> sprites = snap.sprites();

        // First pass: find the list body container (large bitmap in navigator area)
        RenderSprite listBody = null;
        for (int i = sprites.size() - 1; i >= 0; i--) {
            RenderSprite sprite = sprites.get(i);
            if (!sprite.isVisible() || sprite.getChannel() <= 0) continue;
            if (!intersectsNavigator(sprite)) continue;

            if (sprite.getType() == RenderSprite.SpriteType.BITMAP
                    && sprite.getWidth() >= 280 && sprite.getHeight() >= 100
                    && sprite.getX() >= NAV_X && sprite.getY() >= NAV_Y + 70) {
                listBody = sprite;
                break;
            }
        }

        if (listBody != null) {
            System.out.printf("List body: channel=%d pos=(%d,%d) %dx%d ink=%s%n",
                    listBody.getChannel(), listBody.getX(), listBody.getY(),
                    listBody.getWidth(), listBody.getHeight(), listBody.getInkMode());

            // Click the center of the second row in the list body.
            // Each row is ~16px tall. The second row ("Restaurant, Bar & Night Club Rooms")
            // starts about 16px below the list body top.
            int clickX = listBody.getX() + listBody.getWidth() / 2;
            int clickY = listBody.getY() + 28; // second row, vertically centered

            // Try to resolve a label near this position
            String label = resolveNearestLabel(player, snap, clickX, clickY);

            return new RowTarget(clickX, clickY, listBody.getChannel(),
                    label != null ? label : "first-row-heuristic");
        }

        // Fallback: find any interactive bitmap sprite in the navigator list area
        for (int i = sprites.size() - 1; i >= 0; i--) {
            RenderSprite sprite = sprites.get(i);
            if (!sprite.isVisible() || sprite.getChannel() <= 0) continue;
            if (!intersectsNavigator(sprite)) continue;
            if (sprite.getY() < NAV_Y + 100) continue; // skip header area

            if (sprite.getType() == RenderSprite.SpriteType.BITMAP
                    && sprite.getWidth() >= 200 && sprite.getHeight() >= 10) {
                int clickX = sprite.getX() + sprite.getWidth() / 2;
                int clickY = sprite.getY() + sprite.getHeight() / 2;
                String label = resolveNearestLabel(player, snap, clickX, clickY);
                return new RowTarget(clickX, clickY, sprite.getChannel(),
                        label != null ? label : "fallback-bitmap");
            }
        }

        return null;
    }

    /**
     * Try to find the text label nearest to the given stage coordinate.
     * Searches text sprites in the navigator that overlap this position.
     */
    static String resolveNearestLabel(Player player, FrameSnapshot snap, int stageX, int stageY) {
        for (RenderSprite sprite : snap.sprites()) {
            if (!sprite.isVisible()) continue;
            if (sprite.getType() != RenderSprite.SpriteType.TEXT
                    && sprite.getType() != RenderSprite.SpriteType.BUTTON) continue;

            int left = sprite.getX();
            int top = sprite.getY();
            int right = left + sprite.getWidth();
            int bottom = top + sprite.getHeight();

            // Check if this text sprite is on the same row (overlapping Y range)
            if (stageY >= top - 4 && stageY <= bottom + 4 && stageX >= left - 20 && stageX <= right + 20) {
                String label = resolveSpriteLabel(player, sprite);
                if (label != null && !label.isBlank()) {
                    return label;
                }
            }
        }
        return null;
    }

    static String resolveSpriteLabel(Player player, RenderSprite sprite) {
        CastMember member = sprite.getDynamicMember();
        if (member == null && sprite.getChannel() > 0) {
            var state = player.getStageRenderer().getSpriteRegistry().get(sprite.getChannel());
            if (state != null) {
                member = player.getCastLibManager().getDynamicMember(
                        state.getEffectiveCastLib(), state.getEffectiveCastMember());
            }
        }
        if (member == null) return sprite.getMemberName();
        String text = member.getTextContent();
        if (text == null || text.isBlank()) return member.getName();
        return text.strip();
    }

    // ---- HTTP ----

    static byte[] httpGet(String url) throws Exception {
        HttpClient client = HttpClient.newBuilder()
                .connectTimeout(Duration.ofSeconds(10))
                .followRedirects(HttpClient.Redirect.NORMAL)
                .build();
        HttpRequest req = HttpRequest.newBuilder()
                .uri(URI.create(url))
                .timeout(Duration.ofSeconds(30))
                .build();
        HttpResponse<byte[]> resp = client.send(req, HttpResponse.BodyHandlers.ofByteArray());
        if (resp.statusCode() != 200)
            throw new IOException("HTTP " + resp.statusCode() + " for " + url);
        return resp.body();
    }

    // ---- Helpers ----

    static boolean intersectsNavigator(RenderSprite sprite) {
        int left = sprite.getX();
        int top = sprite.getY();
        int right = left + sprite.getWidth();
        int bottom = top + sprite.getHeight();
        return right > NAV_X && left < NAV_X + NAV_W
                && bottom > NAV_Y && top < NAV_Y + NAV_H;
    }

    static void tickAndSleep(Player player, int steps) throws InterruptedException {
        for (int i = 0; i < steps; i++) {
            try { player.tick(); } catch (Exception ignored) { }
            Thread.sleep(TICK_DELAY_MS);
        }
    }

    static void savePng(Bitmap bmp, Path path) throws IOException {
        ImageIO.write(bmp.toBufferedImage(), "PNG", path.toFile());
    }

    static BufferedImage cropRegion(BufferedImage img, int x, int y, int w, int h) {
        int cx = Math.min(x, img.getWidth()), cy = Math.min(y, img.getHeight());
        int cw = Math.min(w, img.getWidth() - cx), ch = Math.min(h, img.getHeight() - cy);
        return (cw > 0 && ch > 0) ? img.getSubimage(cx, cy, cw, ch)
                : new BufferedImage(1, 1, BufferedImage.TYPE_INT_ARGB);
    }

    static BufferedImage createPixelDiff(BufferedImage a, BufferedImage b) {
        int w = Math.min(a.getWidth(), b.getWidth()), h = Math.min(a.getHeight(), b.getHeight());
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

    static double computeRegionChangeFraction(BufferedImage before, BufferedImage after,
                                              int rx, int ry, int rw, int rh) {
        BufferedImage a = cropRegion(before, rx, ry, rw, rh);
        BufferedImage b = cropRegion(after, rx, ry, rw, rh);
        int w = Math.min(a.getWidth(), b.getWidth());
        int h = Math.min(a.getHeight(), b.getHeight());
        int total = 0, changed = 0;
        for (int y = 0; y < h; y += 2) {
            for (int x = 0; x < w; x += 2) {
                total++;
                int pa = a.getRGB(x, y), pb = b.getRGB(x, y);
                int dr = Math.abs(((pa >> 16) & 0xFF) - ((pb >> 16) & 0xFF));
                int dg = Math.abs(((pa >> 8) & 0xFF) - ((pb >> 8) & 0xFF));
                int db = Math.abs((pa & 0xFF) - (pb & 0xFF));
                if (dr + dg + db > 30) changed++;
            }
        }
        return total == 0 ? 0.0 : (double) changed / total;
    }

    static void analyzePixelDiffs(BufferedImage a, BufferedImage b, String label) {
        int w = Math.min(a.getWidth(), b.getWidth()), h = Math.min(a.getHeight(), b.getHeight());
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

    static BufferedImage loadImage(String path) {
        for (String pfx : new String[]{"", "../", "../../"}) {
            File f = new File(pfx + path);
            if (f.exists()) {
                try { return ImageIO.read(f); } catch (IOException e) { /* next */ }
            }
        }
        System.out.println("Image not found: " + path);
        return null;
    }

    static void dumpSpriteInfo(FrameSnapshot snap, Path outFile) throws IOException {
        StringBuilder sb = new StringBuilder();
        sb.append("Frame ").append(snap.frameNumber()).append("\n");
        sb.append("Stage: ").append(snap.stageWidth()).append("x").append(snap.stageHeight()).append("\n\n");
        for (RenderSprite s : snap.sprites()) {
            if (!s.isVisible()) continue;
            sb.append(String.format("Ch%-3d %-8s pos=(%d,%d) %dx%d ink=%-3d(%-20s) blend=%d fg=0x%06X bg=0x%06X hasFg=%s hasBg=%s baked=%s dyn=%s%n",
                    s.getChannel(), s.getType(), s.getX(), s.getY(), s.getWidth(), s.getHeight(),
                    s.getInk(), s.getInkMode(), s.getBlend(), s.getForeColor() & 0xFFFFFF, s.getBackColor() & 0xFFFFFF,
                    s.hasForeColor(), s.hasBackColor(),
                    s.getBakedBitmap() != null ? s.getBakedBitmap().getWidth() + "x" + s.getBakedBitmap().getHeight() : "null",
                    s.getDynamicMember() != null));
        }
        Files.writeString(outFile, sb.toString());
    }

    // ---- Result records ----

    private record RowTarget(int clickX, int clickY, int channel, String label) {}

    private record RowClickResult(int clickX, int clickY, int targetChannel, String label,
                                  int hitChannel, double navChangeFraction,
                                  FrameSnapshot afterSnapshot, boolean navigatorChanged) {}
}
