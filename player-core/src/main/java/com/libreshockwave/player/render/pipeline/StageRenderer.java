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

    private final DirectorFile file;
    private final SpriteRegistry spriteRegistry;
    private CastLibManager castLibManager;

    private int backgroundColor = 0xFFFFFF;  // White default

    // Stage image buffer - used by (the stage).image for direct pixel drawing
    private Bitmap stageImage;

    // Last baked sprites from FrameSnapshot — used for ink-aware hit testing
    private List<RenderSprite> lastBakedSprites = List.of();

    public StageRenderer(DirectorFile file) {
        this.file = file;
        this.spriteRegistry = new SpriteRegistry();
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
            RegPoint reg = scaledRegPoint(member, width, height, x, y, state.isFlipH(), state.isFlipV());
            x -= reg.x();
            y -= reg.y();
        }

        RenderSprite.SpriteType type = member != null ? determineSpriteTypeFromMember(member) : RenderSprite.SpriteType.UNKNOWN;

        // Score spriteType 2-8 are tool-palette shapes (rect, oval, line).
        // Only promote to SHAPE if the cast member is actually a Shape type.
        // Flash (SWF) members, scripts, and other non-shape types that happen to
        // be on shape sprite channels must NOT be rendered as solid fills.
        if (type == RenderSprite.SpriteType.UNKNOWN && data.spriteType() >= 2 && data.spriteType() <= 8
                && member != null && member.memberType() == com.libreshockwave.cast.MemberType.SHAPE) {
            type = RenderSprite.SpriteType.SHAPE;
        }

        return new RenderSprite(
            channel, x, y, width, height, locZ, visible, type, member, null,
            state.hasForeColor() ? state.getForeColor() : data.resolvedForeColor(),
            state.hasBackColor() ? state.getBackColor() : data.resolvedBackColor(),
            state.hasForeColor(), state.hasBackColor(),
            state.getInk(), state.getBlend(),
            state.isFlipH(), state.isFlipV(), null,
            state.hasScriptBehaviors()
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
                        pos.locH(), pos.locV(), w, h,
                        pos.locZ(), true,
                        RenderSprite.SpriteType.SHAPE,
                        null, null,
                        fillColor, state.getBackColor(),
                        true, state.hasBackColor(),
                        0, state.getBlend(), // COPY ink for solid fill
                        state.isFlipH(), state.isFlipV(),
                        null, state.hasScriptBehaviors());
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
            RegPoint reg = scaledRegPoint(member, width, height, x, y, state.isFlipH(), state.isFlipV());
            x -= reg.x();
            y -= reg.y();
            // Fallback auto-size: if sprite still has 0x0 dimensions, derive from member
            if (width == 0 && height == 0 && member.isBitmap()
                    && member.specificData() != null && member.specificData().length >= 10) {
                var bi = com.libreshockwave.cast.BitmapInfo.parse(member.specificData());
                width = bi.width();
                height = bi.height();
            }
        } else if (dynamicMember != null) {
            type = determineSpriteTypeFromDynamic(dynamicMember);
            // Apply registration point offset from dynamic member
            RegPoint reg = mirroredDynamicRegPoint(dynamicMember, width, height,
                    state.isFlipH(), state.isFlipV());
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
            state.hasScriptBehaviors()
        );
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
            var bi = com.libreshockwave.cast.BitmapInfo.parse(member.specificData());
            // ScummVM's getRegistrationOffset() uses bitmap-local coordinates
            // (_regX - _initialRect.left, _regY - _initialRect.top) for sprite rendering.
            int regX = bi.regXLocal();
            int regY = bi.regYLocal();
            int bmpW = bi.width();
            int bmpH = bi.height();
            if (spriteWidth > 0 && bmpW > 0 && bmpW != spriteWidth) {
                regX = regX * spriteWidth / bmpW;
            }
            if (spriteHeight > 0 && bmpH > 0 && bmpH != spriteHeight) {
                regY = regY * spriteHeight / bmpH;
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

    /**
     * Resolve a score color value to RGB.
     * If the color is already RGB (colorFlag set), return it directly.
     * Otherwise, treat it as a Director color number and look up through the default palette.
     *
     * Director's score foreColor/backColor bytes use inverted palette indexing:
     * foreColor 0 = black (palette index 255), foreColor 255 = white (palette index 0).
     * This is the standard Director color model for D5+ movies.
     */
    private int resolveScoreColor(int color, boolean isRGB) {
        if (isRGB) {
            return color;
        }
        // Director color number → palette index (inverted mapping)
        if (color >= 0 && color <= 255 && file != null) {
            Palette palette = file.resolvePalette(-1); // Default palette
            if (palette != null) {
                int paletteIndex = 255 - color;
                return palette.getColor(paletteIndex);
            }
        }
        return color;
    }

    public void reset() {
        spriteRegistry.clear();
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
