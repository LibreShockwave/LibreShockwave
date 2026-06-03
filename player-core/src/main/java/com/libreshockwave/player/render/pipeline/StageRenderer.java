package com.libreshockwave.player.render.pipeline;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.cast.ShapeInfo;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.chunks.ScoreChunk;
import com.libreshockwave.player.cast.CastLib;
import com.libreshockwave.player.cast.CastLibManager;
import com.libreshockwave.player.cast.CastMember;
import com.libreshockwave.player.render.SpriteRegistry;
import com.libreshockwave.player.sprite.SpriteState;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Computes what sprites to render for the current frame.
 * Supports both Score-based sprites and dynamically puppeted sprites.
 */
public class StageRenderer {

    /**
     * Director's stage sprite rasterization lands one pixel below the raw locV
     * coordinate used by the score/runtime state.
     */
    private static final int DIRECTOR_SPRITE_RASTER_Y_ORIGIN = 1;

    private final DirectorFile file;
    private final SpriteRegistry spriteRegistry;
    private CastLibManager castLibManager;

    private int backgroundColor = 0xFFFFFF;  // White default
    private int defaultBackgroundColor = 0xFFFFFF;

    // Stage image buffer - used by (the stage).image for direct pixel drawing
    private Bitmap stageImage;

    // Last baked sprites from FrameSnapshot — used for ink-aware hit testing
    private List<RenderSprite> lastBakedSprites = List.of();

    public StageRenderer(DirectorFile file) {
        this.file = file;
        this.spriteRegistry = new SpriteRegistry();
        if (file != null && file.getConfig() != null) {
            int stageColor = file.getConfig().stageColorRGB();
            this.backgroundColor = stageColor;
            this.defaultBackgroundColor = stageColor;
        }
    }

    public void setCastLibManager(CastLibManager castLibManager) {
        this.castLibManager = castLibManager;
    }

    public SpriteRegistry getSpriteRegistry() {
        return spriteRegistry;
    }

    public int getStageWidth() {
        return file != null ? file.getStageWidth() : 640;
    }

    public int getStageHeight() {
        return file != null ? file.getStageHeight() : 480;
    }

    public int getBackgroundColor() {
        return backgroundColor;
    }

    public void setBackgroundColor(int color) {
        this.backgroundColor = color;
        if (stageImage != null && !stageImage.isScriptModified()) {
            stageImage.fill(0xFF000000 | (backgroundColor & 0xFFFFFF));
        }
    }

    public void setDefaultBackgroundColor(int color) {
        this.defaultBackgroundColor = color;
        setBackgroundColor(color);
    }

    /**
     * Get the stage image buffer for direct pixel drawing.
     * Creates the buffer on first access, sized to the stage dimensions.
     * Filled with background color - Lingo scripts expect an opaque stage image.
     */
    public Bitmap getStageImage() {
        if (stageImage == null) {
            int w = getStageWidth();
            int h = getStageHeight();
            stageImage = new Bitmap(w, h, 32);
            // Fill with background color (opaque) - Director's stage image is opaque
            stageImage.fill(0xFF000000 | (backgroundColor & 0xFFFFFF));
        }
        return stageImage;
    }

    /**
     * Check whether the stage image buffer has been created (i.e., scripts have used it).
     */
    public boolean hasStageImage() {
        return stageImage != null;
    }

    public Bitmap getRenderableStageImage() {
        if (stageImage == null || !stageImage.isScriptModified()) {
            return null;
        }
        return stageImage;
    }

    /**
     * Discard any script-owned stage image buffer and fall back to the stage
     * background color on the next render.
     */
    public void discardStageImage() {
        stageImage = null;
    }

    /**
     * Reset transient stage visuals back to the movie baseline.
     * Used when room/program/error transitions should not retain a script-drawn
     * stage buffer or a room-specific background color.
     */
    public void resetVisualState() {
        backgroundColor = defaultBackgroundColor;
        stageImage = null;
    }

    /** Store baked sprites from last rendered frame for hit testing. */
    public void setLastBakedSprites(List<RenderSprite> sprites) {
        this.lastBakedSprites = sprites;
    }

    /** Get baked sprites from last rendered frame (for ink-aware hit testing). */
    public List<RenderSprite> getLastBakedSprites() {
        return lastBakedSprites;
    }

    /**
     * Get all sprites to render for the given frame.
     * Includes both Score-based sprites and dynamically created/puppeted sprites.
     */
    public List<RenderSprite> getSpritesForFrame(int frame) {
        List<RenderSprite> sprites = new ArrayList<>();
        Set<Integer> renderedChannels = new HashSet<>();
        collectScoreSprites(frame, sprites, renderedChannels);
        collectDynamicSprites(sprites, renderedChannels);
        sortSprites(sprites);

        return sprites;
    }

    public void collectScoreSprites(int frame, List<RenderSprite> sprites, Set<Integer> renderedChannels) {
        if (file == null) {
            return;
        }

        ScoreChunk score = file.getScoreChunk();
        if (score == null) {
            return;
        }

        int frameIndex = frame - 1;
        for (ScoreChunk.FrameChannelEntry entry : score.frameData().frameChannelData()) {
            if (entry.frameIndex().value() != frameIndex) {
                continue;
            }

            int channel = entry.channelIndex().value();
            SpriteState state = spriteRegistry.get(channel);

            if (state != null && state.hasDynamicMember()) {
                if (state.isDynamic()) {
                    state.applyScoreDefaults(entry.data());
                }
                RenderSprite sprite = createDynamicRenderSprite(state);
                if (sprite != null) {
                    sprites.add(sprite);
                    renderedChannels.add(channel);
                }
                continue;
            }

            RenderSprite sprite = createRenderSprite(channel, entry.data());
            if (sprite != null) {
                sprites.add(sprite);
                renderedChannels.add(channel);
            }
        }
    }

    public void collectDynamicSprites(List<RenderSprite> sprites, Set<Integer> renderedChannels) {
        List<SpriteState> dynSprites = spriteRegistry.getDynamicSprites();
        for (SpriteState state : dynSprites) {
            int channel = state.getChannel();
            if (!renderedChannels.contains(channel)
                    && (state.hasDynamicMember() || state.isPuppet())) {
                RenderSprite sprite = createDynamicRenderSprite(state);
                if (sprite != null) {
                    sprites.add(sprite);
                }
            }
        }
    }

    public void sortSprites(List<RenderSprite> sprites) {
        sprites.sort((a, b) -> {
            int cmp = Integer.compare(a.getLocZ(), b.getLocZ());
            return cmp != 0 ? cmp : Integer.compare(a.getChannel(), b.getChannel());
        });
    }

    /**
     * Create a RenderSprite from Score channel data.
     */
    private RenderSprite createRenderSprite(int channel, ScoreChunk.ChannelData data) {
        // Skip empty sprites
        if (data.isEmpty() || data.spriteType() == 0) {
            return null;
        }

        // Get or create runtime state
        SpriteState state = spriteRegistry.getOrCreate(channel, data);

        // Snapshot position atomically to prevent torn reads from VM thread
        var pos = state.snapshotPosition();
        int x = pos.locH();
        int y = pos.locV();
        int locZ = pos.locZ();
        int width = pos.width();
        int height = pos.height();
        boolean visible = state.isVisible();

        // Get cast member — use CASp-based lookup for internal casts,
        // fall back to CastLibManager for external casts
        CastMemberChunk member = file.getCastMemberByNumber(data.castLib(), data.castMember());
        if (member == null && castLibManager != null) {
            member = castLibManager.getCastMember(data.castLib(), data.castMember());
        }

        // Apply registration point offset (scaled for stretched sprites per ScummVM behavior)
        if (member != null) {
            RegPoint reg = scaledRegPoint(
                    member,
                    width,
                    height,
                    x,
                    y,
                    effectiveFlipH(state),
                    state.isFlipV()
            );
            x -= reg.x();
            y -= reg.y();
        }
        y = rasterY(y);

        RenderSprite.SpriteType type = member != null ? determineSpriteTypeFromMember(member) : RenderSprite.SpriteType.UNKNOWN;

        // Score spriteType 2-8 are tool-palette shapes (rect, oval, line).
        // Only promote to SHAPE if the cast member is actually a Shape type.
        // Flash (SWF) members, scripts, and other non-shape types that happen to
        // be on shape sprite channels must NOT be rendered as solid fills.
        if (type == RenderSprite.SpriteType.UNKNOWN && data.spriteType() >= 2 && data.spriteType() <= 8
                && member != null && member.memberType() == com.libreshockwave.cast.MemberType.SHAPE) {
            type = RenderSprite.SpriteType.SHAPE;
        }

        int foreColor = state.hasForeColor()
                ? state.getForeColor()
                : resolveScoreColor(data.resolvedForeColor(), data.isForeColorRGB());
        int backColor = state.hasBackColor()
                ? state.getBackColor()
                : resolveScoreColor(data.resolvedBackColor(), data.isBackColorRGB());

        return new RenderSprite(
            channel, x, y, width, height, locZ, visible, type, member, null,
            foreColor,
            backColor,
            state.hasForeColor(), state.hasBackColor(),
            state.getInk(), state.getBlend(),
            state.isFlipH(), state.isFlipV(), null,
            state.hasScriptBehaviors() || spriteRegistry.hasScoreBehaviorChannel(channel)
        );
    }

    /**
     * Create a RenderSprite from a dynamically modified sprite state.
     */
    private RenderSprite createDynamicRenderSprite(SpriteState state) {
        if (!state.isVisible()) {
            return null;
        }

        int castLib = state.getEffectiveCastLib();
        int castMember = state.getEffectiveCastMember();

        if (castMember <= 0) {
            // Puppeted sprites without a member can still render if they have
            // bgColor set (e.g., window system color swatches). In Director,
            // a visible sprite with bgColor fills its rect with that color.
            if (state.isPuppet() && state.hasBackColor()) {
                var pos = state.snapshotPosition();
                int w = pos.width(), h = pos.height();
                if (w > 0 && h > 0) {
                    // Use bgColor as fill color (Director fills puppet sprite rects
                    // with bgColor when no member bitmap is set)
                    int fillColor = state.getBackColor();
                    return new RenderSprite(
                        state.getChannel(),
                        pos.locH(), rasterY(pos.locV()), w, h,
                        pos.locZ(), true,
                        RenderSprite.SpriteType.SHAPE,
                        null, null,
                        fillColor, state.getBackColor(),
                        true, state.hasBackColor(),
                        0, state.getBlend(), // COPY ink for solid fill
                        state.isFlipH(), state.isFlipV(),
                        null, hasAnyBehavior(state));
                }
            }
            return null;
        }

        // Snapshot position atomically to prevent torn reads from VM thread
        var pos = state.snapshotPosition();
        int x = pos.locH();
        int y = pos.locV();
        int locZ = pos.locZ();
        int width = pos.width();
        int height = pos.height();

        // Look up the cast member — try CastLibManager first for dynamic sprites,
        // since the castLib/castMember values are runtime numbers from the VM
        // (which may differ from the DCR file's internal cast numbering).
        CastMemberChunk member = null;
        if (castLibManager != null) {
            member = castLibManager.getCastMember(castLib, castMember);
        }
        if (member == null && file != null) {
            member = file.getCastMemberByIndex(castLib, castMember);
        }

        // Also resolve the runtime CastMember — needed to detect Lingo-modified bitmaps
        CastMember dynamicMember = null;
        if (castLibManager != null) {
            dynamicMember = castLibManager.getDynamicMember(castLib, castMember);
        }

        RenderSprite.SpriteType type = RenderSprite.SpriteType.UNKNOWN;
        if (member != null) {
            type = determineSpriteTypeFromMember(member);
            // Apply registration point offset (scaled for stretched sprites)
            RegPoint reg = scaledRegPoint(
                    member,
                    width,
                    height,
                    x,
                    y,
                    effectiveFlipH(state),
                    state.isFlipV()
            );
            x -= reg.x();
            y -= reg.y();
            // Fallback auto-size: if sprite still has 0x0 dimensions, derive from member
            if (width == 0 && height == 0 && member.isBitmap()
                    && member.specificData() != null && member.specificData().length >= 10) {
                var bi = com.libreshockwave.cast.BitmapInfo.parse(member);
                width = bi.width();
                height = bi.height();
            }
        } else if (dynamicMember != null) {
            type = determineSpriteTypeFromDynamic(dynamicMember);
            // Apply registration point offset from dynamic member
            RegPoint reg = mirroredDynamicRegPoint(
                    dynamicMember,
                    width,
                    height,
                    effectiveFlipH(state),
                    state.isFlipV()
            );
            x -= reg.x();
            y -= reg.y();
            // Fallback auto-size for dynamic members
            if (width == 0 && height == 0) {
                int dw = dynamicMember.getProp("width").toInt();
                int dh = dynamicMember.getProp("height").toInt();
                if (dw > 0 && dh > 0) {
                    width = dw;
                    height = dh;
                }
            }
        }
        y = rasterY(y);

        return new RenderSprite(
            state.getChannel(),
            x, y,
            width, height,
            locZ,
            state.isVisible(),
            type, member, dynamicMember,
            state.getForeColor(), state.getBackColor(),
            state.hasForeColor(), state.hasBackColor(),
            state.getInk(), state.getBlend(),
            state.isFlipH(), state.isFlipV(),
            state.getRotation(), state.getSkew(),
            null,
            hasAnyBehavior(state)
        );
    }

    private boolean hasAnyBehavior(SpriteState state) {
        return state != null && (state.hasScriptBehaviors()
                || spriteRegistry.hasScoreBehaviorChannel(state.getChannel()));
    }

    private static int rasterY(int y) {
        return y + DIRECTOR_SPRITE_RASTER_Y_ORIGIN;
    }

    /**
     * Determine sprite type from a dynamic CastMember (runtime-created member).
     */
    private RenderSprite.SpriteType determineSpriteTypeFromDynamic(CastMember member) {
        MemberType memberType = member.getMemberType();
        if (memberType == null) {
            return RenderSprite.SpriteType.UNKNOWN;
        }
        return switch (memberType) {
            case BITMAP -> RenderSprite.SpriteType.BITMAP;
            case SHAPE -> RenderSprite.SpriteType.SHAPE;
            case TEXT -> RenderSprite.SpriteType.TEXT;
            case BUTTON -> RenderSprite.SpriteType.BUTTON;
            case FILM_LOOP -> RenderSprite.SpriteType.FILM_LOOP;
            default -> RenderSprite.SpriteType.UNKNOWN;
        };
    }

    private RenderSprite.SpriteType determineSpriteTypeFromMember(CastMemberChunk member) {
        if (member.isBitmap()) {
            return RenderSprite.SpriteType.BITMAP;
        }

        // Director 7+ "Text Asset" Xtras: XTRA type with "text" sub-type
        if (member.isTextXtra()) {
            return RenderSprite.SpriteType.TEXT;
        }

        MemberType memberType = member.memberType();
        if (memberType == null) {
            return RenderSprite.SpriteType.UNKNOWN;
        }

        return switch (memberType) {
            case SHAPE -> RenderSprite.SpriteType.SHAPE;
            case TEXT, RICH_TEXT -> RenderSprite.SpriteType.TEXT;
            case BUTTON -> RenderSprite.SpriteType.BUTTON;
            case FILM_LOOP -> RenderSprite.SpriteType.FILM_LOOP;
            default -> RenderSprite.SpriteType.UNKNOWN;
        };
    }

    /** Registration point result — avoids int[] arrays which TeaVM 0.13 reorders. */
    private record RegPoint(int x, int y) {}

    /**
     * Scale registration point proportionally when sprite dimensions differ from bitmap dimensions.
     * Director (confirmed via ScummVM) scales regPoint by spriteSize/bitmapSize for stretched sprites.
     */
    private RegPoint scaledRegPoint(CastMemberChunk member, int spriteWidth, int spriteHeight,
                                     int posX, int posY, boolean flipH, boolean flipV) {
        if (member.isBitmap() && member.specificData() != null && member.specificData().length >= 10) {
            var bi = com.libreshockwave.cast.BitmapInfo.parse(member);
            // ScummVM's getRegistrationOffset() uses bitmap-local coordinates
            // (_regX - _initialRect.left, _regY - _initialRect.top) for sprite rendering.
            int regX = bi.regXLocal();
            int regY = bi.regYLocal();
            int bmpW = bi.width();
            int bmpH = bi.height();
            if (spriteWidth > 0 && bmpW > 0 && bmpW != spriteWidth) {
                regX = scaleRegistrationOffset(regX, spriteWidth, bmpW);
            }
            if (spriteHeight > 0 && bmpH > 0 && bmpH != spriteHeight) {
                regY = scaleRegistrationOffset(regY, spriteHeight, bmpH);
            }
            regX = mirrorOffset(regX, spriteWidth > 0 ? spriteWidth : bmpW, flipH);
            regY = mirrorOffset(regY, spriteHeight > 0 ? spriteHeight : bmpH, flipV);
            return new RegPoint(regX, regY);
        }
        // Film loop: the specificData stores a bounding rect in stage coordinates.
        // The regPoint maps the sprite's locH/locV to the rect's origin so the
        // film loop renders at its original stage position.
        if (member.memberType() == MemberType.FILM_LOOP
                && member.specificData() != null && member.specificData().length >= 8) {
            var fi = com.libreshockwave.cast.FilmLoopInfo.parse(member.specificData());
            return new RegPoint(posX - fi.rectLeft(), posY - fi.rectTop());
        }
        int regX = mirrorOffset(member.regPointX(), spriteWidth, flipH);
        int regY = mirrorOffset(member.regPointY(), spriteHeight, flipV);
        return new RegPoint(regX, regY);
    }

    private RegPoint mirroredDynamicRegPoint(CastMember dynamicMember, int spriteWidth, int spriteHeight,
                                             boolean flipH, boolean flipV) {
        Bitmap bmp = dynamicMember.getBitmap();
        int width = spriteWidth > 0 ? spriteWidth : (bmp != null ? bmp.getWidth() : 0);
        int height = spriteHeight > 0 ? spriteHeight : (bmp != null ? bmp.getHeight() : 0);
        int regX = dynamicMember.getRegPointX();
        int regY = dynamicMember.getRegPointY();
        regX = mirrorOffset(regX, width, flipH);
        regY = mirrorOffset(regY, height, flipV);
        return new RegPoint(regX, regY);
    }

    private int mirrorOffset(int reg, int span, boolean flipped) {
        if (!flipped || span <= 0) {
            return reg;
        }
        return span - reg;
    }

    private int scaleRegistrationOffset(int reg, int spriteSpan, int bitmapSpan) {
        if (usesLegacyRoundedRegistrationScale()) {
            return Math.round((float) reg * spriteSpan / bitmapSpan);
        }
        return reg * spriteSpan / bitmapSpan;
    }

    private boolean usesLegacyRoundedRegistrationScale() {
        return file != null
                && file.getConfig() != null
                && file.getConfig().directorVersion() <= 1600;
    }

    private boolean effectiveFlipH(SpriteState state) {
        return state.isFlipH() ^ hasDirectorHorizontalMirror(state.getRotation(), state.getSkew());
    }

    private static boolean hasDirectorHorizontalMirror(double rotation, double skew) {
        return normalizeTransformAngle(rotation) == 180 && normalizeTransformAngle(skew) == 180;
    }

    private static int normalizeTransformAngle(double angle) {
        int normalized = (int) Math.round(angle) % 360;
        if (normalized < 0) {
            normalized += 360;
        }
        return normalized;
    }

    /**
     * Resolve a score color value to RGB.
     * If the color is already RGB (colorFlag set), return it directly.
     * Otherwise, treat it as a Director color number and look up through the default palette.
     *
     * Director score color bytes are palette color numbers. In the default
     * Director palette used by Habbo v1, color number 255 resolves to black.
     */
    private int resolveScoreColor(int color, boolean isRGB) {
        if (isRGB) {
            return usesRgb555ScoreColors() ? expandScoreRgb555(color) : color;
        }
        // Director color number → palette index (inverted mapping)
        if (color >= 0 && color <= 255 && file != null) {
            Palette palette = file.resolvePalette(-1); // Default palette
            if (palette != null) {
                return palette.getColor(color);
            }
        }
        return color;
    }

    private boolean usesRgb555ScoreColors() {
        // Older Shockwave-era movies render score RGB colors through a 15-bit
        // display path; newer Habbo clients match their authored 24-bit values.
        return file != null
                && file.getConfig() != null
                && file.getConfig().directorVersion() <= 1600;
    }

    static int expandScoreRgb555(int color) {
        int r = expand5Bit((color >> 16) & 0xFF);
        int g = expand5Bit((color >> 8) & 0xFF);
        int b = expand5Bit(color & 0xFF);
        return (r << 16) | (g << 8) | b;
    }

    private static int expand5Bit(int value) {
        int fiveBit = value >> 3;
        return (fiveBit << 3) | (fiveBit >> 2);
    }

    public void reset() {
        spriteRegistry.clear();
        lastBakedSprites = List.of();
        resetVisualState();
    }

    public void onSpriteEnd(int channel) {
        spriteRegistry.remove(channel);
    }

    public void onFrameEnter(int frame) {
        if (file == null) return;

        ScoreChunk score = file.getScoreChunk();
        if (score == null) return;

        int frameIndex = frame - 1;

        for (ScoreChunk.FrameChannelEntry entry : score.frameData().frameChannelData()) {
            if (entry.frameIndex().value() == frameIndex) {
                int channel = entry.channelIndex().value();
                if (spriteRegistry.contains(channel)) {
                    spriteRegistry.updateFromScore(channel, entry.data());
                }
            }
        }
    }
}
