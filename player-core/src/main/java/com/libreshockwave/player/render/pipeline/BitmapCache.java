package com.libreshockwave.player.render.pipeline;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.cast.BitmapInfo;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.id.InkMode;

import com.libreshockwave.player.Player;
import com.libreshockwave.player.cast.CastMember;

import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Caches ink-processed bitmaps for rendering.
 * All decoding is synchronous — WASM is single-threaded and desktop bitmaps decode fast enough.
 */
public class BitmapCache {

    record IndexedMatteColorRemap(int foreColor, int backColor) {}

    private final Map<CacheKey, Bitmap> cache = new ConcurrentHashMap<>();
    private final Set<MemberCacheId> decodeFailed = Collections.newSetFromMap(new ConcurrentHashMap<>());
    /** Tracks the last known palette version per member ID to detect palette changes. */
    private final Map<MemberCacheId, Integer> paletteVersions = new ConcurrentHashMap<>();

    private record MemberCacheId(int fileIdentity, int memberId) {}

    private record CacheKey(MemberCacheId member, int ink, int foreColor, int backColor,
                            boolean hasForeColor, boolean hasBackColor) {}

    /**
     * Get an ink-processed bitmap for a file-loaded cast member.
     * Decodes synchronously on first call; returns cached bitmap on subsequent calls.
     */
    public Bitmap getProcessed(CastMemberChunk member, int ink, int backColor, Player player) {
        return getProcessed(member, ink, backColor, 0, false, false, player, null);
    }

    /**
     * Get an ink-processed bitmap with an optional palette override.
     * Used for palette swap animation where the runtime palette differs from the embedded one.
     */
    public Bitmap getProcessed(CastMemberChunk member, int ink, int backColor, Player player, Palette paletteOverride) {
        return getProcessed(member, ink, backColor, 0, false, false, player, paletteOverride);
    }

    /**
     * Get an ink-processed bitmap with foreColor/backColor colorization.
     * For 1-bit bitmaps, Director applies foreColor/backColor colorization BEFORE ink
     * processing. This ensures that masks with foreColor=white become fully white before
     * BLEND/MATTE ink removes the white background, making them transparent.
     */
    public Bitmap getProcessed(CastMemberChunk member, int ink, int backColor,
                                int foreColor, boolean hasForeColor, boolean hasBackColor,
                                Player player, Palette paletteOverride) {
        MemberCacheId memberId = memberKey(member);
        CacheKey key = new CacheKey(memberId, ink, hasForeColor ? foreColor : 0, backColor,
                hasForeColor, hasBackColor);

        Bitmap cached = cache.get(key);
        if (cached != null) {
            return cached;
        }

        if (player == null || decodeFailed.contains(memberId)) {
            return null;
        }

        try {
            Palette effectivePalette = paletteOverride;

            // Parse BitmapInfo for useAlpha and paletteId.
            // Resolve the palette BEFORE decoding so we use the same palette for
            // both bitmap decoding and InkProcessor backColor resolution.
            boolean useAlpha = false;
            Palette palette = effectivePalette;
            if (member.specificData() != null && member.specificData().length >= 10) {
                DirectorFile memberFile = member.file();
                int dirVer = 1200;
                if (memberFile != null && memberFile.getConfig() != null) {
                    dirVer = memberFile.getConfig().directorVersion();
                }
                BitmapInfo info = BitmapInfo.parse(member.specificData(), dirVer);
                useAlpha = info.useAlpha();
                if (palette == null && memberFile != null) {
                    palette = memberFile.resolvePalette(info.paletteId());
                }
            }

            // Decode with resolved palette to ensure pixel colors match the
            // palette used for InkProcessor backColor resolution.
            Optional<Bitmap> bitmap;
            if (palette != null) {
                bitmap = player.getBitmapResolver().decodeBitmap(member, palette);
            } else {
                bitmap = player.getBitmapResolver().decodeBitmap(member);
            }
            if (bitmap.isEmpty()) {
                decodeFailed.add(memberId);
                return null;
            }

            Bitmap raw = bitmap.get();

            // Director applies foreColor/backColor colorization BEFORE ink processing
            // for 1-bit bitmaps. This is critical for masks: a mask with foreColor=white
            // should become all-white, then BLEND/MATTE ink removes the white background
            // making the mask fully transparent.
            if (raw.getBitDepth() <= 1 && hasForeColor) {
                raw = InkProcessor.applyForeColorRemap(raw, foreColor, backColor);
            }

            Bitmap processed = applyIndexedMatteColorRemapIfNeeded(
                    raw,
                    InkProcessor.applyInk(raw, ink, backColor, useAlpha, palette),
                    ink, foreColor, backColor, hasForeColor, hasBackColor, palette);
            cache.put(key, processed);
            return processed;
        } catch (Exception e) {
            decodeFailed.add(memberId);
            return null;
        }
    }

    /**
     * Invalidate cache entries for a member if its palette version has changed.
     * Returns true if the cache was actually invalidated (palette changed since last render).
     */
    public boolean invalidateIfPaletteChanged(CastMemberChunk member, int paletteVersion) {
        MemberCacheId memberId = memberKey(member);
        Integer lastVersion = paletteVersions.get(memberId);
        if (lastVersion != null && lastVersion == paletteVersion) {
            return false; // No change
        }
        paletteVersions.put(memberId, paletteVersion);
        // Remove from caches - scan for any key containing this member ID
        cache.keySet().removeIf(key -> key.member().equals(memberId));
        decodeFailed.remove(memberId);
        return true;
    }

    /**
     * Get an ink-processed bitmap for a dynamic (runtime-created) cast member.
     * Synchronous — dynamic members already have their bitmap decoded.
     * NOT cached because dynamic member bitmaps are mutable (window system updates them).
     */
    public Bitmap getProcessedDynamic(CastMember dynMember, int ink, int backColor,
                                     int foreColor, boolean hasForeColor, boolean hasBackColor) {
        Bitmap bmp = dynMember.getBitmap();
        if (bmp == null) {
            return null;
        }

        if (InkProcessor.shouldProcessInk(ink)) {
            boolean useAlpha = bmp.getBitDepth() == 32 && bmp.isNativeAlpha();
            return applyIndexedMatteColorRemapIfNeeded(
                    bmp,
                    InkProcessor.applyInk(bmp, ink, backColor, useAlpha, bmp.getImagePalette()),
                    ink, foreColor, backColor, hasForeColor, hasBackColor, bmp.getImagePalette());
        }
        return applyIndexedMatteColorRemapIfNeeded(
                bmp,
                bmp,
                ink, foreColor, backColor, hasForeColor, hasBackColor, bmp.getImagePalette());
    }

    /**
     * Clear all cached bitmaps. Call when external casts are loaded.
     */
    public void clear() {
        cache.clear();
        decodeFailed.clear();
        paletteVersions.clear();
    }

    private MemberCacheId memberKey(CastMemberChunk member) {
        int fileIdentity = member != null && member.file() != null ? System.identityHashCode(member.file()) : 0;
        int memberId = member != null ? member.id().value() : 0;
        return new MemberCacheId(fileIdentity, memberId);
    }

    static IndexedMatteColorRemap resolveIndexedMatteColorRemap(
            Bitmap raw, int ink, int foreColor, int backColor,
            boolean hasForeColor, boolean hasBackColor, Palette palette) {
        if (raw == null || raw.getBitDepth() <= 1 || raw.getPaletteIndices() == null) {
            return null;
        }
        if (InkMode.fromCode(ink) != InkMode.MATTE) {
            return null;
        }
        if (!hasForeColor && !hasBackColor) {
            return null;
        }
        int effectiveForeColor = hasForeColor ? (foreColor & 0xFFFFFF) : 0x000000;
        int effectiveBackColor = hasBackColor
                ? InkProcessor.resolveBackColor(raw, InkMode.COPY, backColor, false, palette)
                : 0xFFFFFF;

        // Skip the default black→white identity ramp. Dynamic sprites inherit score defaults
        // as explicit colors, but Director only needs indexed MATTE recoloring when a script
        // actually changes the palette ramp (for example furni bgColor layers).
        if (effectiveForeColor == 0x000000 && effectiveBackColor == 0xFFFFFF) {
            return null;
        }
        return new IndexedMatteColorRemap(effectiveForeColor, effectiveBackColor);
    }

    static Bitmap applyIndexedMatteColorRemapIfNeeded(
            Bitmap raw, Bitmap processed, int ink, int foreColor, int backColor,
            boolean hasForeColor, boolean hasBackColor, Palette palette) {
        return applyIndexedMatteColorRemap(
                raw,
                processed,
                resolveIndexedMatteColorRemap(
                        raw, ink, foreColor, backColor, hasForeColor, hasBackColor, palette));
    }

    static Bitmap applyIndexedMatteColorRemap(Bitmap raw, Bitmap processed, IndexedMatteColorRemap remap) {
        if (raw == null || processed == null || remap == null) {
            return processed;
        }
        return InkProcessor.applyIndexedColorRemap(raw, processed, remap.foreColor(), remap.backColor());
    }
}
