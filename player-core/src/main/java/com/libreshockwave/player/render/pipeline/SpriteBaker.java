package com.libreshockwave.player.render.pipeline;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.player.Player;
import com.libreshockwave.player.cast.CastLib;
import com.libreshockwave.player.cast.CastLibManager;
import com.libreshockwave.player.cast.CastMember;
import com.libreshockwave.chunks.ScoreChunk;

import java.util.ArrayList;
import java.util.List;

/**
 * Centralized sprite-to-bitmap baking pipeline.
 * Converts all sprite types (BITMAP, TEXT, SHAPE) into pre-rendered bitmaps
 * so renderers only need to blit pixels — no type-specific draw logic needed.
 */
public class SpriteBaker {

    private final BitmapCache bitmapCache;
    private final CastLibManager castLibManager;
    private final Player player;
    private final List<SpriteBakeStep> bakeSteps = new ArrayList<>();
    private int tickCounter;

    public SpriteBaker(BitmapCache bitmapCache, CastLibManager castLibManager, Player player) {
        this.bitmapCache = bitmapCache;
        this.castLibManager = castLibManager;
        this.player = player;
        registerDefaultSteps();
    }

    public int getTickCounter() {
        return tickCounter;
    }

    /**
     * Bake all sprites in the list, returning a new list with baked bitmaps attached.
     */
    public List<RenderSprite> bakeSprites(List<RenderSprite> sprites) {
        tickCounter++;
        List<RenderSprite> result = new ArrayList<>(sprites.size());
        for (RenderSprite sprite : sprites) {
            result.add(bake(sprite));
        }
        return result;
    }

    /**
     * Bake a single sprite: dispatch by type, apply colorization, attach bitmap.
     */
    public RenderSprite bake(RenderSprite sprite) {
        Bitmap baked = null;
        for (SpriteBakeStep step : bakeSteps) {
            if (step.supports(sprite)) {
                baked = step.bake(sprite);
                break;
            }
        }

        // Apply Director's sprite-level foreColor/backColor colorization.
        int ch = sprite.getChannel();
        boolean hasColor = sprite.hasForeColor() || sprite.hasBackColor();
        boolean colorizeOk = InkProcessor.allowsColorize(sprite.getInk());
        boolean scriptMod = isScriptModifiedSprite(sprite);
        if (baked != null && sprite.getType() != RenderSprite.SpriteType.SHAPE && hasColor && colorizeOk) {
            if (scriptMod) {
                // Script-modified bitmaps (window system buffers, copyPixels results):
                // Apply simple bgColor replacement — Director replaces white pixels with
                // backColor for COPY ink sprites. This is safe for colored bitmaps because
                // only exact white (0xFFFFFF) pixels are replaced, preserving existing content.
                int bgColor = sprite.getBackColor() & 0xFFFFFF;
                if (sprite.hasBackColor() && bgColor != 0xFFFFFF) {
                    baked = InkProcessor.remapExactColor(baked, 0xFFFFFF, bgColor);
                }
            } else {
                // File-loaded member bitmaps (1-bit icons, etc.): apply full grayscale remap
                // where white→backColor, black→foreColor, gray→interpolated
                baked = InkProcessor.applyForeColorRemap(baked, sprite.getForeColor(), sprite.getBackColor());
            }
        }

        // For text sprites, the baked bitmap may have different dimensions than the
        // score sprite (member specificData dimensions vs score stored dimensions).
        // Update the sprite dimensions to match the baked bitmap so the renderer
        // doesn't scale or clip the text.
        if (baked != null && (sprite.getType() == RenderSprite.SpriteType.TEXT
                || sprite.getType() == RenderSprite.SpriteType.BUTTON)
                && (baked.getWidth() != sprite.getWidth() || baked.getHeight() != sprite.getHeight())) {
            return sprite.withBakedBitmapAndSize(baked, baked.getWidth(), baked.getHeight());
        }

        return sprite.withBakedBitmap(baked);
    }

    private boolean isScriptModifiedSprite(RenderSprite sprite) {
        if (sprite.getDynamicMember() == null) return false;
        Bitmap bmp = sprite.getDynamicMember().getBitmap();
        return bmp != null && bmp.isScriptModified();
    }

    private void registerDefaultSteps() {
        registerBakeStep(new BitmapSpriteBakeStep());
        registerBakeStep(new TextSpriteBakeStep());
        registerBakeStep(new ShapeSpriteBakeStep());
        registerBakeStep(new FilmLoopSpriteBakeStep());
        registerBakeStep(new UnsupportedSpriteBakeStep());
    }

    public void registerBakeStep(SpriteBakeStep step) {
        bakeSteps.add(step);
    }

    public interface SpriteBakeStep {
        boolean supports(RenderSprite sprite);
        Bitmap bake(RenderSprite sprite);
    }

    private final class BitmapSpriteBakeStep implements SpriteBakeStep {
        @Override
        public boolean supports(RenderSprite sprite) {
            return sprite.getType() == RenderSprite.SpriteType.BITMAP;
        }

        @Override
        public Bitmap bake(RenderSprite sprite) {
            return bakeBitmap(sprite);
        }
    }

    private final class TextSpriteBakeStep implements SpriteBakeStep {
        @Override
        public boolean supports(RenderSprite sprite) {
            return sprite.getType() == RenderSprite.SpriteType.TEXT
                    || sprite.getType() == RenderSprite.SpriteType.BUTTON;
        }

        @Override
        public Bitmap bake(RenderSprite sprite) {
            return bakeText(sprite);
        }
    }

    private final class ShapeSpriteBakeStep implements SpriteBakeStep {
        @Override
        public boolean supports(RenderSprite sprite) {
            return sprite.getType() == RenderSprite.SpriteType.SHAPE;
        }

        @Override
        public Bitmap bake(RenderSprite sprite) {
            return bakeShape(sprite);
        }
    }

    private final class FilmLoopSpriteBakeStep implements SpriteBakeStep {
        @Override
        public boolean supports(RenderSprite sprite) {
            return sprite.getType() == RenderSprite.SpriteType.FILM_LOOP;
        }

        @Override
        public Bitmap bake(RenderSprite sprite) {
            return bakeFilmLoop(sprite);
        }
    }

    private static final class UnsupportedSpriteBakeStep implements SpriteBakeStep {
        @Override
        public boolean supports(RenderSprite sprite) {
            return true;
        }

        @Override
        public Bitmap bake(RenderSprite sprite) {
            return null;
        }
    }

    /**
     * Bake a BITMAP sprite: decode + ink-process via BitmapCache.
     */
    private Bitmap bakeBitmap(RenderSprite sprite) {
        Bitmap b = null;

        // Check if the runtime CastMember's bitmap was modified by Lingo (fill, copyPixels, etc.)
        // If so, use the live bitmap directly instead of the stale BitmapCache entry.
        if (sprite.getDynamicMember() != null) {
            Bitmap liveBmp = sprite.getDynamicMember().getBitmap();
            if (liveBmp != null && liveBmp.isScriptModified()) {
                boolean skipBgTransparent = sprite.getInkMode() == com.libreshockwave.id.InkMode.BACKGROUND_TRANSPARENT
                        && liveBmp.getBitDepth() == 32
                        && !hasBorderColor(liveBmp, sprite.getBackColor() & 0xFFFFFF);
                if (skipBgTransparent) {
                    return liveBmp;
                }
                if (InkProcessor.shouldProcessInk(sprite.getInk())) {
                    // Script-modified bitmaps (avatar canvases, window buffers) use
                    // simple exact-match color-key transparency. The graduated alpha
                    // unblending in applyBackgroundTransparent is designed for text
                    // anti-aliasing, but destroys intentionally grayscale body parts
                    // (e.g., Habbo avatar sprites that use grayscale-to-color remapping).
                    return InkProcessor.applyInk(liveBmp, sprite.getInk(),
                            sprite.getBackColor(), false, null, true);
                }
                return liveBmp;
            }
        }

        if (sprite.getCastMember() != null) {
            // Check for runtime palette override (palette swap animation)
            PaletteOverrideInfo palInfo = resolvePaletteOverride(sprite);
            Palette paletteOverride = null;
            if (palInfo != null) {
                // Only invalidate cache when palette actually changed
                bitmapCache.invalidateIfPaletteChanged(
                        sprite.getCastMember().id().value(), palInfo.version);
                paletteOverride = palInfo.palette;
            }
            b = bitmapCache.getProcessed(sprite.getCastMember(), sprite.getInk(),
                    sprite.getBackColor(), player, paletteOverride);
        }
        if (b == null && sprite.getDynamicMember() != null) {
            b = bitmapCache.getProcessedDynamic(sprite.getDynamicMember(),
                    sprite.getInk(), sprite.getBackColor());
        }
        return b;
    }

    private boolean hasBorderColor(Bitmap bmp, int colorRgb) {
        int w = bmp.getWidth();
        int h = bmp.getHeight();
        for (int x = 0; x < w; x++) {
            if ((bmp.getPixel(x, 0) & 0xFFFFFF) == colorRgb) return true;
            if ((bmp.getPixel(x, h - 1) & 0xFFFFFF) == colorRgb) return true;
        }
        for (int y = 1; y < h - 1; y++) {
            if ((bmp.getPixel(0, y) & 0xFFFFFF) == colorRgb) return true;
            if ((bmp.getPixel(w - 1, y) & 0xFFFFFF) == colorRgb) return true;
        }
        return false;
    }

    private record PaletteOverrideInfo(Palette palette, int version) {}

    /**
     * Resolve a palette override for a sprite's bitmap member.
     * Returns null if no palette override is set.
     */
    private PaletteOverrideInfo resolvePaletteOverride(RenderSprite sprite) {
        if (castLibManager == null || sprite.getCastMember() == null) {
            return null;
        }

        // Find the runtime CastMember for this sprite's bitmap by name
        String name = sprite.getCastMember().name();
        if (name == null || name.isEmpty()) {
            return null;
        }

        CastMember member = castLibManager.findCastMemberByName(name);
        if (member == null || !member.hasPaletteOverride()) {
            return null;
        }

        // Resolve the palette member to a Palette object
        int palCastLib = member.getPaletteRefCastLib();
        int palMemberNum = member.getPaletteRefMemberNum();

        CastLib paletteCastLib = castLibManager.getCastLib(palCastLib);
        if (paletteCastLib == null) {
            return null;
        }

        DirectorFile palFile = paletteCastLib.getSourceFile();
        if (palFile == null) {
            return null;
        }

        Palette palette = palFile.resolvePaletteByMemberNumber(palMemberNum);
        return palette != null ? new PaletteOverrideInfo(palette, member.getPaletteVersion()) : null;
    }

    /**
     * Bake a TEXT or BUTTON sprite: resolve member, render text to bitmap.
     */
    private Bitmap bakeText(RenderSprite sprite) {
        // Try dynamic member first (runtime-created text/field members)
        CastMember member = sprite.getDynamicMember();

        // Fall back to file-loaded member lookup by name
        if (member == null && sprite.getCastMember() != null && castLibManager != null) {
            String memberName = sprite.getCastMember().name();
            if (memberName != null && !memberName.isEmpty()) {
                member = castLibManager.findCastMemberByName(memberName);
            }
        }

        // Fall back to runtime member lookup by chunk ID (for nameless members
        // like XTRA text members whose text may have been set by Lingo)
        if (member == null && sprite.getCastMember() != null && castLibManager != null) {
            member = castLibManager.findRuntimeMember(sprite.getCastMember());
        }

        Bitmap textImage = null;

        if (member != null && member.hasDynamicText()) {
            // Lingo set member.text — use the member's own text properties (color,
            // alignment, font style, etc.) so rendering matches caret positioning.
            int width = sprite.getWidth() > 0 ? sprite.getWidth() : 200;
            int height = sprite.getHeight() > 0 ? sprite.getHeight() : 20;
            int bgColor = (sprite.getInkMode() == com.libreshockwave.id.InkMode.BACKGROUND_TRANSPARENT)
                    ? 0x00000000 : member.getTextBgColor();
            textImage = member.renderTextToImage(width, height, bgColor);
        }
        // For file-loaded text members (no dynamic text), skip member.renderTextToImage()
        // and fall through to bakeTextFromFile() which applies the sprite's foreColor/backColor
        // from the score. member.renderTextToImage() uses the member's default color (black)
        // which doesn't reflect the score's foreColor for this sprite channel.

        // Fall back to rendering directly from the file's STXT/XMED chunk
        // (for score-placed text sprites that don't have a runtime CastMember,
        // or when renderTextToImage returned null)
        if (textImage == null && sprite.getCastMember() != null) {
            textImage = bakeTextFromFile(sprite);
        }

        if (textImage == null) {
            return null;
        }

        // For BACKGROUND_TRANSPARENT text: both XTRA and regular text members are already
        // rendered with transparent background (bgColor=0x00000000). No further ink processing
        // needed — the bitmap already has correct alpha (opaque text, transparent background).
        if (sprite.getInkMode() == com.libreshockwave.id.InkMode.BACKGROUND_TRANSPARENT) {
            return textImage;
        }

        // Apply ink processing
        if (InkProcessor.shouldProcessInk(sprite.getInk())) {
            textImage = InkProcessor.applyInk(textImage, sprite.getInk(),
                    sprite.getBackColor(), false, null);
        }

        return textImage;
    }

    /**
     * Render text directly from file data (CASt specificData + STXT/XMED chunk).
     * Used for score-placed text sprites that don't have a runtime CastMember.
     * Handles both regular STXT text and Director 7+ Text Asset Xtras (XMED chunks).
     */
    private Bitmap bakeTextFromFile(RenderSprite sprite) {
        var castMember = sprite.getCastMember();
        var file = castMember.file();
        if (file == null) return null;
        // Try XMED text first (Director 7+ Text Asset Xtra)
        if (castMember.isTextXtra()) {
            return bakeTextFromXmed(sprite, file, castMember);
        }

        // Look up the STXT chunk via KEY* table
        var textChunk = file.getTextForMember(castMember);
        if (textChunk == null || textChunk.text() == null) return null;

        // Parse text formatting from specificData
        var textInfo = com.libreshockwave.cast.TextInfo.parse(castMember.specificData());

        // Get font info from STXT formatting runs (first run determines primary font)
        String fontName = "Arial";
        int fontSize = 12;
        int fontStyle = 0;
        int runColorR = -1, runColorG = -1, runColorB = -1;
        if (!textChunk.runs().isEmpty()) {
            var run = textChunk.runs().get(0);
            fontSize = run.fontSize();
            fontStyle = run.fontStyle();
            runColorR = run.colorR();
            runColorG = run.colorG();
            runColorB = run.colorB();
        }

        String styleStr = "";
        if ((fontStyle & 1) != 0) styleStr += "bold";
        if ((fontStyle & 2) != 0) styleStr += (styleStr.isEmpty() ? "" : ",") + "italic";
        if ((fontStyle & 4) != 0) styleStr += (styleStr.isEmpty() ? "" : ",") + "underline";

        String alignment = switch (textInfo.textAlign()) {
            case 1 -> "center";
            case -1 -> "right";
            default -> "left";
        };

        int width = textInfo.width() > 0 ? textInfo.width() : sprite.getWidth();
        int height = textInfo.height() > 0 ? textInfo.height() : sprite.getHeight();
        if (width <= 0) width = 200;
        if (height <= 0) height = 20;

        var renderer = CastMember.getTextRendererStatic();
        if (renderer == null) return null;

        // Text color: prefer STXT run color, fall back to palette-resolved sprite foreColor
        int textColor;
        if (runColorR >= 0) {
            textColor = 0xFF000000 | (runColorR << 16) | (runColorG << 8) | runColorB;
        } else {
            textColor = resolvePaletteColor(sprite.getForeColor());
        }
        // Use transparent background for BACKGROUND_TRANSPARENT ink
        int bgColor = (sprite.getInkMode() == com.libreshockwave.id.InkMode.BACKGROUND_TRANSPARENT)
                ? 0x00000000
                : 0xFF000000 | ((textInfo.bgRed() << 16) | (textInfo.bgGreen() << 8) | textInfo.bgBlue());

        return renderer.renderText(
                textChunk.text(), width, height,
                fontName, fontSize, styleStr,
                alignment, textColor, bgColor,
                textInfo.isWordWrap(), false,
                0, 0);
    }

    /**
     * Render text from XMED chunk data (Director 7+ Text Asset Xtra).
     * Uses XmedStyledText which contains all parsed properties from both
     * the XMED chunk and the CASt specificData — no raw byte reading needed here.
     */
    private Bitmap bakeTextFromXmed(RenderSprite sprite, com.libreshockwave.DirectorFile file,
                                     com.libreshockwave.chunks.CastMemberChunk castMember) {
        var styledText = file.getXmedStyledTextForMember(castMember);
        if (styledText == null || styledText.text() == null || styledText.text().isEmpty()) {
            return null;
        }

        // Use member's specificData dimensions (the text rect size) instead of score
        // sprite dimensions. The score stores different heights for OLE text members
        // than the Property Inspector / member intrinsic size. Director uses the member
        // dimensions for rendering text, not the score's stored height.
        int width = styledText.width() > 0 ? styledText.width() : (sprite.getWidth() > 0 ? sprite.getWidth() : 200);
        int height = styledText.height() > 0 ? styledText.height() : (sprite.getHeight() > 0 ? sprite.getHeight() : 20);

        // ARGB format — text color from XMED data if available, fall back to palette-resolved foreColor
        int textColor;
        if (styledText.colorR() >= 0) {
            textColor = styledText.textColorARGB();
        } else {
            textColor = resolvePaletteColor(sprite.getForeColor());
        }
        // Use transparent background for BACKGROUND_TRANSPARENT ink so the text
        // can be composited directly without ink processing removing the text pixels.
        int bgColor = (sprite.getInkMode() == com.libreshockwave.id.InkMode.BACKGROUND_TRANSPARENT)
                ? 0x00000000 : resolvePaletteColor(sprite.getBackColor());

        var renderer = CastMember.getTextRendererStatic();
        if (renderer == null) return null;

        String styleStr = styledText.fontStyleString();

        return renderer.renderXmedText(styledText, width, height, textColor, bgColor);
    }

    /**
     * Resolve a score color value (palette index 0-255) to packed ARGB for text rendering.
     * Values > 255 are already RGB (from script-set colors).
     * Values 0-255 are palette indices looked up through the default palette.
     */
    private int resolvePaletteColor(int color) {
        if (color > 255) {
            // Already packed RGB (script-set)
            return 0xFF000000 | (color & 0xFFFFFF);
        }
        // Palette index lookup
        Palette palette = player != null && player.getFile() != null
                ? player.getFile().resolvePalette(-1) : null;
        if (palette != null) {
            return 0xFF000000 | (palette.getColor(color) & 0xFFFFFF);
        }
        // Fallback: Director grayscale ramp (0=white, 255=black)
        int gray = 255 - color;
        return 0xFF000000 | (gray << 16) | (gray << 8) | gray;
    }

    /**
     * Bake a FILM_LOOP sprite: render the last frame of the film loop.
     * Film loops contain embedded score data with sub-sprites positioned in
     * stage coordinates. The specificData stores a bounding rect that defines
     * the film loop's coordinate space.
     */
    private Bitmap bakeFilmLoop(RenderSprite sprite) {
        var castMember = sprite.getCastMember();
        if (castMember == null || castMember.file() == null) return null;

        var file = castMember.file();

        // Look up the embedded score chunk via KEY* table
        ScoreChunk embeddedScore = file.getScoreForMember(castMember);
        if (embeddedScore == null || embeddedScore.frameData() == null) return null;

        var frameData = embeddedScore.frameData();
        if (frameData.frameChannelData().isEmpty()) return null;

        // Parse film loop rect from specificData
        var filmInfo = com.libreshockwave.cast.FilmLoopInfo.parse(castMember.specificData());
        int loopW = filmInfo.width() > 0 ? filmInfo.width() : sprite.getWidth();
        int loopH = filmInfo.height() > 0 ? filmInfo.height() : sprite.getHeight();
        if (loopW <= 0 || loopH <= 0) return null;

        // Rect origin — sub-sprites use absolute stage coords, offset by this
        int rectLeft = filmInfo.rectLeft();
        int rectTop = filmInfo.rectTop();

        // Cycle through film loop frames based on tick counter (animated marquee)
        int frameCount = frameData.header().frameCount();
        int targetFrame = (frameCount > 0) ? (tickCounter % frameCount) : 0;

        // Create output bitmap filled with transparent
        int[] outPixels = new int[loopW * loopH];

        // Collect sub-sprites for the target frame
        var subSprites = new ArrayList<ScoreChunk.FrameChannelEntry>();
        for (var entry : frameData.frameChannelData()) {
            if (entry.frameIndex().value() == targetFrame && !entry.data().isEmpty()) {
                subSprites.add(entry);
            }
        }
        subSprites.sort((a, b) -> Integer.compare(
                a.channelIndex().value(), b.channelIndex().value()));

        for (var entry : subSprites) {
            var data = entry.data();
            if (data.spriteType() == 0 || data.castMember() <= 0) continue;

            // Look up sub-sprite's cast member from the same file.
            // Film loop embedded scores use castLib=65535 (0xFFFF) or 0 as "internal cast" sentinel.
            var subMember = resolveFilmLoopMember(file, data.castLib(), data.castMember());
            if (subMember == null) continue;

            // Decode the sub-sprite's bitmap via the cache
            Bitmap subBitmap = bitmapCache.getProcessed(subMember, data.ink(),
                    data.resolvedBackColor(), player, null);
            if (subBitmap == null || subBitmap.getPixels() == null) continue;

            // Sub-sprite stage position minus its registration point
            int sx = data.posX();
            int sy = data.posY();
            if (subMember.isBitmap() && subMember.specificData() != null
                    && subMember.specificData().length >= 10) {
                var bi = com.libreshockwave.cast.BitmapInfo.parse(subMember.specificData());
                sx -= bi.regXLocal();
                sy -= bi.regYLocal();
            } else {
                sx -= subMember.regPointX();
                sy -= subMember.regPointY();
            }

            // Map from stage coordinates to film loop bitmap coordinates
            sx -= rectLeft;
            sy -= rectTop;

            // Blit sub-sprite onto the film loop output
            blitOnto(outPixels, loopW, loopH, subBitmap, sx, sy);
        }

        Bitmap result = new Bitmap(loopW, loopH, 32, outPixels);

        // Apply the film loop sprite's own ink to the composited bitmap.
        // Sub-sprites with COPY ink have opaque white backgrounds that need to be
        // made transparent so lower-channel sprites (e.g., the orange bar) show through.
        // useAlpha=false so 32-bit bitmaps get color-keyed (white→transparent).
        if (InkProcessor.shouldProcessInk(sprite.getInk())) {
            result = InkProcessor.applyInk(result, sprite.getInk(),
                    sprite.getBackColor(), false, null);
        }

        return result;
    }

    /** Resolve a cast member from a film loop's embedded score. */
    private com.libreshockwave.chunks.CastMemberChunk resolveFilmLoopMember(
            DirectorFile file, int castLib, int memberNum) {
        var member = file.getCastMemberByNumber(castLib, memberNum);
        if (member == null && (castLib == 0xFFFF || castLib == 0)) {
            member = file.getCastMemberByNumber(1, memberNum);
        }
        if (member == null) {
            member = file.getCastMemberByIndex(castLib, memberNum);
            if (member == null && (castLib == 0xFFFF || castLib == 0)) {
                member = file.getCastMemberByIndex(1, memberNum);
            }
        }
        return member;
    }

    /** Blit a source bitmap onto a destination pixel array with alpha compositing. */
    private static void blitOnto(int[] dst, int dstW, int dstH, Bitmap src, int ox, int oy) {
        int[] srcPixels = src.getPixels();
        int srcW = src.getWidth();
        int srcH = src.getHeight();
        for (int py = 0; py < srcH; py++) {
            int dy = oy + py;
            if (dy < 0 || dy >= dstH) continue;
            for (int px = 0; px < srcW; px++) {
                int dx = ox + px;
                if (dx < 0 || dx >= dstW) continue;
                int s = srcPixels[py * srcW + px];
                int sa = (s >> 24) & 0xFF;
                if (sa == 0) continue;
                int dstIdx = dy * dstW + dx;
                if (sa >= 255) {
                    dst[dstIdx] = s;
                } else {
                    int d = dst[dstIdx];
                    int da = (d >> 24) & 0xFF;
                    int inv = 255 - sa;
                    int oa = sa + (da * inv / 255);
                    if (oa > 0) {
                        int or_ = (((s >> 16) & 0xFF) * sa + ((d >> 16) & 0xFF) * da * inv / 255) / oa;
                        int og = (((s >> 8) & 0xFF) * sa + ((d >> 8) & 0xFF) * da * inv / 255) / oa;
                        int ob = ((s & 0xFF) * sa + (d & 0xFF) * da * inv / 255) / oa;
                        dst[dstIdx] = (oa << 24) | (or_ << 16) | (og << 8) | ob;
                    }
                }
            }
        }
    }

    /**
     * Bake a SHAPE sprite: create a solid-color bitmap filled with the sprite's foreColor.
     */
    private Bitmap bakeShape(RenderSprite sprite) {
        int w = sprite.getWidth() > 0 ? sprite.getWidth() : 50;
        int h = sprite.getHeight() > 0 ? sprite.getHeight() : 50;
        int fc = sprite.getForeColor();

        Bitmap shape = new Bitmap(w, h, 32);
        int argb = 0xFF000000 | (fc & 0xFFFFFF);
        int[] pixels = shape.getPixels();
        for (int i = 0; i < pixels.length; i++) {
            pixels[i] = argb;
        }

        return shape;
    }

}
