package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
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
 * Navigator SSO test: downloads habbo.dcr from sandbox.h4bbo.net,
 * connects to the real Habbo proxy at au.h4bbo.net:30001, ticks frames until the
 * navigator window appears, renders to PNG, and creates pixel diff against reference.
 *
 * Everything loads over HTTP — no local disk paths for DCR or casts.
 *
 * Requires: Habbo proxy running on au.h4bbo.net:30001 (TCP) and au.h4bbo.net:38101 (MUS).
 *
 * Usage:
 *   ./gradlew :player-core:runNavigatorSSOTest
 */
public class NavigatorSSOTest {

    private static final String DCR_URL = "https://sandbox.h4bbo.net/dcr/14.1_b8/habbo.dcr";

    // Navigator region (right side of stage)
    private static final int NAV_X = 350, NAV_Y = 60, NAV_W = 370, NAV_H = 440;

    private static final Path OUTPUT_DIR = Path.of("build/navigator-sso");

    // Timing
    private static final int TICK_DELAY_MS = 67;        // ~15fps
    private static final int MAX_WAIT_MS   = 120_000;   // 2 minutes max
    private static final int CAST_LOAD_WAIT_MS = 8000;  // wait for casts

    public static void main(String[] args) throws Exception {
        Files.createDirectories(OUTPUT_DIR);
        System.out.println("=== Navigator SSO Test ===");
        System.out.println("Loading DCR from " + DCR_URL);
        System.out.println("Proxy: au.h4bbo.net:30001 (TCP) / au.h4bbo.net:38101 (MUS)");

        // 1. Download DCR over HTTP
        byte[] dcrBytes = httpGet(DCR_URL);
        System.out.printf("Downloaded DCR: %d bytes%n", dcrBytes.length);

        DirectorFile dirFile = DirectorFile.load(dcrBytes);
        // Set base path so NetManager resolves cast downloads from the same origin
        dirFile.setBasePath("https://sandbox.h4bbo.net/dcr/14.1_b8/");
        Player player = new Player(dirFile);

        // 2. External params — real proxy ports, SSO ticket, all URLs from h4bbo.net
        Map<String, String> params = new LinkedHashMap<>();
        params.put("sw1", "site.url=http://www.habbo.co.uk;url.prefix=http://www.habbo.co.uk");
        params.put("sw2", "connection.info.host=au.h4bbo.net;connection.info.port=30087");
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

        // 3. Start playback and wait for external casts to load over HTTP
        player.play();
        player.preloadAllCasts();
        System.out.println("Waiting " + CAST_LOAD_WAIT_MS + "ms for casts to load from h4bbo.net...");
        Thread.sleep(CAST_LOAD_WAIT_MS);

        // 4. Tick at real speed until hotel view loads (>20 sprites)
        //    Needs real-time delays so HTTP cast downloads can complete between ticks
        System.out.println("Loading hotel view...");
        long loadStart = System.currentTimeMillis();
        for (int i = 0; i < 3000 && (System.currentTimeMillis() - loadStart) < 60_000; i++) {
            try { player.tick(); } catch (Exception e) { }
            Thread.sleep(10); // give HTTP threads time to download
            if (i % 50 == 0) {
                int sc = player.getFrameSnapshot().sprites().size();
                System.out.printf("  startup tick %d, sprites=%d, elapsed=%ds%n",
                        i, sc, (System.currentTimeMillis() - loadStart) / 1000);
                if (sc > 20 && i > 100) {
                    System.out.printf("  Hotel view loaded at tick %d (%d sprites)%n", i, sc);
                    // Tick more to settle
                    for (int j = 0; j < 200; j++) {
                        try { player.tick(); } catch (Exception ignored) {}
                        Thread.sleep(10);
                    }
                    break;
                }
            }
        }

        // Capture baseline hotel view
        FrameSnapshot baseSnap = player.getFrameSnapshot();
        Bitmap hotelViewBitmap = baseSnap.renderFrame();
        savePng(hotelViewBitmap, OUTPUT_DIR.resolve("01_hotel_view.png"));
        int baselineSpriteCount = baseSnap.sprites().size();
        System.out.printf("Baseline: frame %d, %d sprites%n",
                baseSnap.frameNumber(), baselineSpriteCount);

        // 5. Tick at real speed, waiting for navigator window to appear.
        //    Detect by checking for navigator-region sprites (channels 60+).
        Bitmap navigatorBitmap = null;
        int navigatorTick = -1;
        int tick = 0;
        long startMs = System.currentTimeMillis();

        System.out.println("Ticking frames (max " + MAX_WAIT_MS / 1000 + "s), waiting for navigator...");
        while (System.currentTimeMillis() - startMs < MAX_WAIT_MS) {
            try { player.tick(); } catch (Exception e) {
                if (tick < 5) System.out.println("[Tick " + tick + "] " + e.getMessage());
            }

            if (tick % 30 == 0) {
                FrameSnapshot snap = player.getFrameSnapshot();
                // Detect navigator by checking for sprites in the navigator region (right side)
                boolean hasNavigator = snap.sprites().stream().anyMatch(s ->
                        s.isVisible() && s.getX() >= NAV_X && s.getChannel() >= 60);

                if (hasNavigator && navigatorBitmap == null) {
                    System.out.printf("Navigator appeared at tick %d (%d sprites), capturing%n",
                            tick, snap.sprites().size());
                    navigatorBitmap = snap.renderFrame();
                    navigatorTick = tick;
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

        // 6. Final capture
        FrameSnapshot finalSnap = player.getFrameSnapshot();
        Bitmap finalFrame = finalSnap.renderFrame();
        savePng(finalFrame, OUTPUT_DIR.resolve("02_final_frame.png"));
        System.out.printf("Final frame (tick %d, frame %d, %d sprites)%n",
                tick, finalSnap.frameNumber(), finalSnap.sprites().size());

        if (navigatorBitmap != null) {
            savePng(navigatorBitmap, OUTPUT_DIR.resolve("03_navigator.png"));
            System.out.println("Navigator saved from tick " + navigatorTick);
        } else {
            System.out.println("Navigator did NOT appear within " + MAX_WAIT_MS / 1000 + "s");
            System.out.println("Is the Habbo proxy running on au.h4bbo.net:30001?");
        }

        // 7. Sprite info + ink analysis
        dumpSpriteInfo(finalSnap, OUTPUT_DIR.resolve("sprite_info.txt"));

        // 8. Pixel diffs against reference
        Bitmap compareFrame = navigatorBitmap != null ? navigatorBitmap : finalFrame;
        BufferedImage ourImage = compareFrame.toBufferedImage();
        savePng(compareFrame, OUTPUT_DIR.resolve("02_our_output.png"));

        BufferedImage refImage = loadImage("docs/habbo-reference.png");
        BufferedImage wasmNavImage = loadImage("player-wasm/frames_navigator/03_navigator_final.png");

        if (refImage != null) createDiffOutputs(ourImage, refImage, "Ours vs Reference");
        if (wasmNavImage != null && refImage != null)
            analyzePixelDiffs(wasmNavImage, refImage, "WASM vs Reference");
        if (wasmNavImage != null) {
            BufferedImage d = createPixelDiff(ourImage, wasmNavImage);
            ImageIO.write(d, "PNG", OUTPUT_DIR.resolve("07_ours_vs_wasm_diff.png").toFile());
            analyzePixelDiffs(ourImage, wasmNavImage, "Ours vs WASM");
        }

        analyzeInkColors(finalSnap);
        if (!errors.isEmpty()) System.out.println("\nTotal errors: " + errors.size());

        player.shutdown();
        System.out.println("\n=== Test complete ===");
        System.out.println("Output: " + OUTPUT_DIR.toAbsolutePath());
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

    static void createDiffOutputs(BufferedImage ours, BufferedImage ref, String label) throws IOException {
        BufferedImage fullDiff = createPixelDiff(ours, ref);
        ImageIO.write(fullDiff, "PNG", OUTPUT_DIR.resolve("04_full_diff.png").toFile());

        BufferedImage navOurs = cropRegion(ours, NAV_X, NAV_Y, NAV_W, NAV_H);
        BufferedImage navRef = cropRegion(ref, NAV_X, NAV_Y, NAV_W, NAV_H);
        BufferedImage navDiff = createPixelDiff(navOurs, navRef);
        ImageIO.write(navDiff, "PNG", OUTPUT_DIR.resolve("05_nav_region_diff.png").toFile());
        ImageIO.write(navOurs, "PNG", OUTPUT_DIR.resolve("05a_nav_region_ours.png").toFile());
        ImageIO.write(navRef, "PNG", OUTPUT_DIR.resolve("05b_nav_region_ref.png").toFile());

        BufferedImage sbs = createSideBySide(navOurs, navRef, navDiff);
        ImageIO.write(sbs, "PNG", OUTPUT_DIR.resolve("06_nav_side_by_side.png").toFile());
        System.out.println("Created diff images");
        analyzePixelDiffs(ours, ref, label);
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

    // ---- Image utilities ----

    static void savePng(Bitmap bmp, Path path) throws IOException {
        ImageIO.write(bmp.toBufferedImage(), "PNG", path.toFile());
    }

    static BufferedImage cropRegion(BufferedImage img, int x, int y, int w, int h) {
        int cx = Math.min(x, img.getWidth()), cy = Math.min(y, img.getHeight());
        int cw = Math.min(w, img.getWidth()-cx), ch = Math.min(h, img.getHeight()-cy);
        return (cw>0&&ch>0) ? img.getSubimage(cx,cy,cw,ch) : new BufferedImage(1,1,BufferedImage.TYPE_INT_ARGB);
    }

    static BufferedImage createPixelDiff(BufferedImage a, BufferedImage b) {
        int w = Math.min(a.getWidth(),b.getWidth()), h = Math.min(a.getHeight(),b.getHeight());
        BufferedImage d = new BufferedImage(w,h,BufferedImage.TYPE_INT_ARGB);
        for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
            int pa=a.getRGB(x,y), pb=b.getRGB(x,y);
            if (pa==pb) d.setRGB(x,y,0xFF000000); else {
                int dr=Math.min(255,Math.abs(((pa>>16)&0xFF)-((pb>>16)&0xFF))*4);
                int dg=Math.min(255,Math.abs(((pa>>8)&0xFF)-((pb>>8)&0xFF))*4);
                int db=Math.min(255,Math.abs((pa&0xFF)-(pb&0xFF))*4);
                d.setRGB(x,y,0xFF000000|(dr<<16)|(dg<<8)|db);
            }
        }
        return d;
    }

    static BufferedImage createSideBySide(BufferedImage a, BufferedImage b, BufferedImage diff) {
        int h = Math.max(a.getHeight(),Math.max(b.getHeight(),diff.getHeight()));
        BufferedImage r = new BufferedImage(a.getWidth()+b.getWidth()+diff.getWidth()+4,h,BufferedImage.TYPE_INT_ARGB);
        var g = r.createGraphics();
        g.drawImage(a,0,0,null); g.drawImage(b,a.getWidth()+2,0,null);
        g.drawImage(diff,a.getWidth()+b.getWidth()+4,0,null); g.dispose();
        return r;
    }

    static long regionPixelHash(Bitmap frame, int rx, int ry, int rw, int rh) {
        int[] px=frame.getPixels(); int fw=frame.getWidth(), fh=frame.getHeight();
        long hash=0;
        for (int y=ry;y<Math.min(ry+rh,fh);y+=2)
            for (int x=rx;x<Math.min(rx+rw,fw);x+=2) hash=hash*31+px[y*fw+x];
        return hash;
    }

    static void analyzePixelDiffs(BufferedImage a, BufferedImage b, String label) {
        int w=Math.min(a.getWidth(),b.getWidth()), h=Math.min(a.getHeight(),b.getHeight());
        int total=w*h, identical=0, close=0, different=0; long tR=0,tG=0,tB=0;
        for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
            int pa=a.getRGB(x,y), pb=b.getRGB(x,y);
            if (pa==pb) identical++; else {
                int dr=Math.abs(((pa>>16)&0xFF)-((pb>>16)&0xFF));
                int dg=Math.abs(((pa>>8)&0xFF)-((pb>>8)&0xFF));
                int db=Math.abs((pa&0xFF)-(pb&0xFF));
                tR+=dr;tG+=dg;tB+=db; if(dr<=5&&dg<=5&&db<=5)close++;else different++;
            }
        }
        System.out.printf("\n--- %s ---\n",label);
        System.out.printf("  Total: %d | Identical: %d (%.1f%%) | Close: %d (%.1f%%) | Different: %d (%.1f%%)%n",
                total,identical,100.0*identical/total,close,100.0*close/total,different,100.0*different/total);
        if(different+close>0){long dp=different+close;
            System.out.printf("  Avg diff: R=%.1f G=%.1f B=%.1f%n",(double)tR/dp,(double)tG/dp,(double)tB/dp);}
    }

    // ---- Sprite/Ink analysis ----

    static void dumpSpriteInfo(FrameSnapshot snap, Path outFile) throws IOException {
        StringBuilder sb = new StringBuilder();
        sb.append("Frame ").append(snap.frameNumber()).append("\n");
        sb.append("Stage: ").append(snap.stageWidth()).append("x").append(snap.stageHeight()).append("\n\n");
        for (RenderSprite s : snap.sprites()) { if (!s.isVisible()) continue;
            sb.append(String.format("Ch%-3d %-8s pos=(%d,%d) %dx%d ink=%-3d(%-20s) blend=%d fg=0x%06X bg=0x%06X hasFg=%s hasBg=%s baked=%s dyn=%s%n",
                s.getChannel(),s.getType(),s.getX(),s.getY(),s.getWidth(),s.getHeight(),
                s.getInk(),s.getInkMode(),s.getBlend(),s.getForeColor()&0xFFFFFF,s.getBackColor()&0xFFFFFF,
                s.hasForeColor(),s.hasBackColor(),
                s.getBakedBitmap()!=null?s.getBakedBitmap().getWidth()+"x"+s.getBakedBitmap().getHeight():"null",
                s.getDynamicMember()!=null));}
        Files.writeString(outFile, sb.toString());
    }

    static void analyzeInkColors(FrameSnapshot snap) {
        System.out.println("\n--- Ink/Color Analysis ---");
        Map<String,Integer> inks = new TreeMap<>(); int colorized=0;
        for (RenderSprite s : snap.sprites()) { if(!s.isVisible()||s.getBakedBitmap()==null) continue;
            inks.merge(s.getInkMode().toString(),1,Integer::sum);
            if((s.hasForeColor()||s.hasBackColor())&&
                com.libreshockwave.player.render.pipeline.InkProcessor.allowsColorize(s.getInk())) {
                colorized++;
                System.out.printf("  COLORIZED: ch%d ink=%s fg=0x%06X bg=0x%06X dyn=%s%n",
                    s.getChannel(),s.getInkMode(),s.getForeColor()&0xFFFFFF,s.getBackColor()&0xFFFFFF,
                    s.getDynamicMember()!=null);
            }
        }
        System.out.println("\nInk distribution:");
        inks.forEach((k,v)->System.out.printf("  %-25s : %d sprites%n",k,v));
        System.out.println("Colorized sprites: "+colorized);
    }
}
