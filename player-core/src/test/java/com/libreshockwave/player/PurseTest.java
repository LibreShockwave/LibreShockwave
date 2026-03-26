package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.player.cast.CastMember;
import com.libreshockwave.player.render.pipeline.FrameSnapshot;
import com.libreshockwave.player.render.pipeline.RenderSprite;
import com.libreshockwave.player.input.HitTester;

import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Purse open test: load Habbo over real HTTP, click the purse widget,
 * save before/after frames, and create pixel diffs against reference.
 *
 * Usage:
 *   ./gradlew :player-core:runPurseTest
 */
public class PurseTest {

    private static final String DCR_URL = "https://sandbox.h4bbo.net/dcr/14.1_b8/habbo.dcr";

    private static final Path OUTPUT_DIR = Path.of("build/purse-test");

    // ROI that bounds the purse window in the reference screenshot
    private static final int PURSE_X = 150, PURSE_Y = 150, PURSE_W = 420, PURSE_H = 260;

    // Timing
    private static final int TICK_DELAY_MS = 67;        // ~15fps
    private static final int CAST_LOAD_WAIT_MS = 8000;
    private static final int MAX_WAIT_MS   = 120_000;
    private static final int CLICK_SETTLE_TICKS = 75;
    private static final double CLICK_CHANGE_THRESHOLD = 0.010;

    public static void main(String[] args) throws Exception {
        Files.createDirectories(OUTPUT_DIR);
        System.out.println("=== Purse Test ===");
        System.out.println("Loading DCR from " + DCR_URL);

        byte[] dcrBytes = httpGet(DCR_URL);
        System.out.printf("Downloaded DCR: %d bytes%n", dcrBytes.length);

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

        player.setErrorListener((msg, ex) -> {
            System.out.println("[Error] " + msg);
        });

        // Start playback and wait for external casts
        player.play();
        player.preloadAllCasts();
        System.out.println("Waiting " + CAST_LOAD_WAIT_MS + "ms for casts to load...");
        Thread.sleep(CAST_LOAD_WAIT_MS);

        // Tick until hotel view is populated
        long startLoad = System.currentTimeMillis();
        for (int i = 0; i < 3000 && (System.currentTimeMillis() - startLoad) < 60_000; i++) {
            try { player.tick(); } catch (Exception ignored) { }
            Thread.sleep(10);
            if (i % 50 == 0) {
                int sc = player.getFrameSnapshot().sprites().size();
                System.out.printf("  startup tick %d, sprites=%d%n", i, sc);
                if (sc > 20 && i > 100) {
                    for (int j = 0; j < 200; j++) {
                        try { player.tick(); } catch (Exception ignored) {}
                        Thread.sleep(10);
                    }
                    break;
                }
            }
        }

        FrameSnapshot beforeSnap = player.getFrameSnapshot();
        Bitmap beforeFrame = beforeSnap.renderFrame();
        NavigatorSSOTest.savePng(beforeFrame, OUTPUT_DIR.resolve("01_before_click.png"));
        System.out.printf("Baseline: frame %d, sprites=%d%n",
                beforeSnap.frameNumber(), beforeSnap.sprites().size());
        if (beforeSnap.sprites().size() < 20) {
            player.shutdown();
            throw new IllegalStateException("Hotel view did not load; baseline only had "
                    + beforeSnap.sprites().size() + " sprites");
        }

        ClickTarget target = findPurseClickTarget(player, beforeSnap);
        if (target == null) {
            System.out.println("Purse click target not found (label/heuristic failed).");
        } else {
            System.out.printf(Locale.ROOT, "Clicking purse at (%d,%d) channel=%d source=%s%n",
                    target.stageX(), target.stageY(), target.channel(), target.source());

            int hitChannel = HitTester.hitTest(player.getStageRenderer(), beforeSnap.frameNumber(),
                    target.stageX(), target.stageY(),
                    channel -> player.getEventDispatcher().isSpriteMouseInteractive(channel));
            System.out.println("Hit test channel: " + hitChannel);

            player.getInputHandler().onMouseMove(target.stageX(), target.stageY());
            tickAndSleep(player, 1);
            player.getInputHandler().onMouseDown(target.stageX(), target.stageY(), false);
            tickAndSleep(player, 1);
            player.getInputHandler().onMouseUp(target.stageX(), target.stageY(), false);
        }

        // Allow UI to update
        double bestChange = 0.0;
        FrameSnapshot bestSnap = player.getFrameSnapshot();
        for (int i = 0; i < CLICK_SETTLE_TICKS; i++) {
            tickAndSleep(player, 1);
            FrameSnapshot snap = player.getFrameSnapshot();
            double change = computeRegionChangeFraction(
                    beforeFrame.toBufferedImage(), snap.renderFrame().toBufferedImage(),
                    PURSE_X, PURSE_Y, PURSE_W, PURSE_H);
            if (change > bestChange) {
                bestChange = change;
                bestSnap = snap;
            }
            if (change >= CLICK_CHANGE_THRESHOLD) break;
        }

        Bitmap afterFrame = bestSnap.renderFrame();
        NavigatorSSOTest.savePng(afterFrame, OUTPUT_DIR.resolve("02_after_click.png"));
        System.out.printf(Locale.ROOT, "Best purse change: %.2f%%%n", bestChange * 100.0);

        // Sprite dump for debugging
        NavigatorSSOTest.dumpSpriteInfo(bestSnap, OUTPUT_DIR.resolve("sprite_info.txt"));
        dumpPurseSprites(bestSnap);

        // Diff vs reference
        BufferedImage ourImage = afterFrame.toBufferedImage();
        BufferedImage refImage = NavigatorSSOTest.loadImage("docs/purse-reference.png");
        if (refImage != null) {
            createDiffOutputs(ourImage, refImage);
            NavigatorSSOTest.analyzePixelDiffs(ourImage, refImage, "Purse: Ours vs Reference");
        } else {
            System.out.println("Reference image not found: docs/purse-reference.png");
        }

        player.shutdown();
        System.out.println("=== Purse Test complete ===");
        System.out.println("Output: " + OUTPUT_DIR.toAbsolutePath());
    }

    // ---- HTTP ----

    private static byte[] httpGet(String url) throws Exception {
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

    private static void createDiffOutputs(BufferedImage ours, BufferedImage ref) throws IOException {
        BufferedImage fullDiff = NavigatorSSOTest.createPixelDiff(ours, ref);
        ImageIO.write(fullDiff, "PNG", OUTPUT_DIR.resolve("04_full_diff.png").toFile());

        BufferedImage purseOurs = NavigatorSSOTest.cropRegion(ours, PURSE_X, PURSE_Y, PURSE_W, PURSE_H);
        BufferedImage purseRef  = NavigatorSSOTest.cropRegion(ref,  PURSE_X, PURSE_Y, PURSE_W, PURSE_H);
        BufferedImage purseDiff = NavigatorSSOTest.createPixelDiff(purseOurs, purseRef);

        ImageIO.write(purseOurs, "PNG", OUTPUT_DIR.resolve("05a_purse_region_ours.png").toFile());
        ImageIO.write(purseRef,  "PNG", OUTPUT_DIR.resolve("05b_purse_region_ref.png").toFile());
        ImageIO.write(purseDiff, "PNG", OUTPUT_DIR.resolve("05_purse_region_diff.png").toFile());

        BufferedImage sbs = NavigatorSSOTest.createSideBySide(purseOurs, purseRef, purseDiff);
        ImageIO.write(sbs, "PNG", OUTPUT_DIR.resolve("06_purse_side_by_side.png").toFile());
        System.out.println("Created purse diff images");
    }

    private static void tickAndSleep(Player player, int steps) throws InterruptedException {
        for (int i = 0; i < steps; i++) {
            try { player.tick(); } catch (Exception ignored) { }
            Thread.sleep(TICK_DELAY_MS);
        }
    }

    private static ClickTarget findPurseClickTarget(Player player, FrameSnapshot snap) {
        ClickTarget heuristic = null;
        RenderSprite creditsAnchor = null;
        List<RenderSprite> sprites = snap.sprites();
        for (int i = sprites.size() - 1; i >= 0; i--) {
            RenderSprite sprite = sprites.get(i);
            if (!sprite.isVisible() || sprite.getChannel() <= 0) continue;

            String label = resolveSpriteLabel(player, sprite);
            String memberName = sprite.getMemberName() == null ? "" : sprite.getMemberName();
            String labelLc = label == null ? "" : label.toLowerCase(Locale.ROOT);
            String nameLc  = memberName.toLowerCase(Locale.ROOT);

            boolean looksInteractive = sprite.getType() == RenderSprite.SpriteType.BUTTON
                    || sprite.getType() == RenderSprite.SpriteType.TEXT
                    || sprite.getType() == RenderSprite.SpriteType.BITMAP;

            int cx = sprite.getX() + Math.max(1, sprite.getWidth() / 2);
            int cy = sprite.getY() + Math.max(1, sprite.getHeight() / 2);

            if (labelLc.contains("purse") || nameLc.contains("purse")
                    || labelLc.contains("wallet") || nameLc.contains("wallet")) {
                return new ClickTarget(cx, cy, sprite.getChannel(), "label:" + labelLc);
            }
            if ((labelLc.contains("credits") || nameLc.contains("credits")) && creditsAnchor == null) {
                creditsAnchor = sprite;
            }

            // Heuristic: top-right toolbar buttons
            if (heuristic == null
                    && looksInteractive
                    && sprite.getX() > snap.stageWidth() - 220
                    && sprite.getY() < 140
                    && sprite.getWidth() >= 24 && sprite.getWidth() <= 140
                    && sprite.getHeight() >= 12 && sprite.getHeight() <= 120) {
                heuristic = new ClickTarget(cx, cy, sprite.getChannel(), "toolbar-heuristic");
            }
        }
        if (creditsAnchor != null) {
            ClickTarget iconCandidate = findCreditsIconCandidate(snap, creditsAnchor);
            if (iconCandidate != null) {
                return iconCandidate;
            }
            return new ClickTarget(
                    creditsAnchor.getX() + creditsAnchor.getWidth() - 6,
                    creditsAnchor.getY() + Math.max(1, creditsAnchor.getHeight() / 2),
                    creditsAnchor.getChannel(),
                    "credits-text-edge");
        }
        return heuristic;
    }

    private static ClickTarget findCreditsIconCandidate(FrameSnapshot snap, RenderSprite anchor) {
        RenderSprite best = null;
        int anchorRight = anchor.getX() + anchor.getWidth();
        for (RenderSprite sprite : snap.sprites()) {
            if (!sprite.isVisible() || sprite.getChannel() <= 0) {
                continue;
            }
            if (sprite.getX() <= anchorRight) {
                continue;
            }
            if (sprite.getX() > anchorRight + 120) {
                continue;
            }
            if (Math.abs(sprite.getY() - anchor.getY()) > 24) {
                continue;
            }
            if (sprite.getWidth() < 20 || sprite.getWidth() > 60
                    || sprite.getHeight() < 20 || sprite.getHeight() > 60) {
                continue;
            }
            if (best == null || sprite.getWidth() * sprite.getHeight() > best.getWidth() * best.getHeight()) {
                best = sprite;
            }
        }
        if (best == null) {
            return null;
        }
        return new ClickTarget(
                best.getX() + Math.max(1, best.getWidth() / 2),
                best.getY() + Math.max(1, best.getHeight() / 2),
                best.getChannel(),
                "credits-icon-heuristic");
    }

    private static void dumpPurseSprites(FrameSnapshot snap) throws IOException {
        Path dir = OUTPUT_DIR.resolve("sprites");
        Files.createDirectories(dir);
        for (RenderSprite sprite : snap.sprites()) {
            if (!sprite.isVisible() || sprite.getBakedBitmap() == null) {
                continue;
            }
            if (!intersectsPurse(sprite)) {
                continue;
            }
            String fileName = String.format(Locale.ROOT,
                    "ch%02d_%s_%dx%d_%d_%d.png",
                    sprite.getChannel(),
                    sprite.getInkMode().name().toLowerCase(Locale.ROOT),
                    sprite.getWidth(),
                    sprite.getHeight(),
                    sprite.getX(),
                    sprite.getY());
            NavigatorSSOTest.savePng(sprite.getBakedBitmap(), dir.resolve(fileName));
            if (sprite.getDynamicMember() != null && sprite.getDynamicMember().getBitmap() != null) {
                String rawFileName = String.format(Locale.ROOT,
                        "ch%02d_raw_%dx%d_%d_%d.png",
                        sprite.getChannel(),
                        sprite.getDynamicMember().getBitmap().getWidth(),
                        sprite.getDynamicMember().getBitmap().getHeight(),
                        sprite.getX(),
                        sprite.getY());
                NavigatorSSOTest.savePng(sprite.getDynamicMember().getBitmap(), dir.resolve(rawFileName));
            }
        }
    }

    private static boolean intersectsPurse(RenderSprite sprite) {
        int left = sprite.getX();
        int top = sprite.getY();
        int right = left + sprite.getWidth();
        int bottom = top + sprite.getHeight();
        return right > PURSE_X && left < PURSE_X + PURSE_W
                && bottom > PURSE_Y && top < PURSE_Y + PURSE_H;
    }

    private static String resolveSpriteLabel(Player player, RenderSprite sprite) {
        CastMember member = sprite.getDynamicMember();
        if (member == null && sprite.getChannel() > 0) {
            var state = player.getStageRenderer().getSpriteRegistry().get(sprite.getChannel());
            if (state != null) {
                member = player.getCastLibManager().getDynamicMember(
                        state.getEffectiveCastLib(), state.getEffectiveCastMember());
            }
        }
        if (member == null) {
            return sprite.getMemberName();
        }
        String text = member.getTextContent();
        if (text == null || text.isBlank()) {
            return member.getName();
        }
        return text.strip();
    }

    private static double computeRegionChangeFraction(BufferedImage before, BufferedImage after,
                                                      int rx, int ry, int rw, int rh) {
        BufferedImage a = NavigatorSSOTest.cropRegion(before, rx, ry, rw, rh);
        BufferedImage b = NavigatorSSOTest.cropRegion(after, rx, ry, rw, rh);
        int w = Math.min(a.getWidth(), b.getWidth());
        int h = Math.min(a.getHeight(), b.getHeight());
        int total = 0;
        int changed = 0;
        for (int y = 0; y < h; y += 2) {
            for (int x = 0; x < w; x += 2) {
                total++;
                int pa = a.getRGB(x, y);
                int pb = b.getRGB(x, y);
                int dr = Math.abs(((pa >> 16) & 0xFF) - ((pb >> 16) & 0xFF));
                int dg = Math.abs(((pa >> 8) & 0xFF) - ((pb >> 8) & 0xFF));
                int db = Math.abs((pa & 0xFF) - (pb & 0xFF));
                if (dr + dg + db > 30) {
                    changed++;
                }
            }
        }
        return total == 0 ? 0.0 : (double) changed / total;
    }

    private record ClickTarget(int stageX, int stageY, int channel, String source) {}
}
