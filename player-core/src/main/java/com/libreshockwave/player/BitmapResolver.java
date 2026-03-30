package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.cast.BitmapInfo;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.player.cast.CastLib;
import com.libreshockwave.player.cast.CastLibManager;
import com.libreshockwave.player.frame.FrameContext;

import java.util.Optional;

/**
 * Resolves and decodes bitmap cast members, handling cross-file palette resolution
 * and movie-level palette management.
 * Extracted from Player to separate bitmap concerns.
 */
public class BitmapResolver {

    private final DirectorFile file;
    private final CastLibManager castLibManager;
    private final FrameContext frameContext;

    // Movie-level palette (from score's palette channel) — used for all 8-bit bitmap decoding
    private Palette moviePalette;
    private int moviePaletteFrame = -1; // frame at which moviePalette was last resolved

    public BitmapResolver(DirectorFile file, CastLibManager castLibManager, FrameContext frameContext) {
        this.file = file;
        this.castLibManager = castLibManager;
        this.frameContext = frameContext;
    }

    /**
     * Decode a bitmap from any loaded source — main file or external casts.
     * Uses the member's own file reference to avoid chunk ID collisions.
     */
    public Optional<Bitmap> decodeBitmap(CastMemberChunk member) {
        // Each CastMemberChunk stores a reference to the DirectorFile it was loaded from.
        // Use that file first to avoid cross-file chunk ID collisions.
        DirectorFile memberFile = member.file();

        // For external cast bitmaps, resolve palette cross-file.
        // External casts often don't contain palette cast members, so the bitmap's
        // palette reference (clutId) may point to a palette in the main movie file.
        if (memberFile != null && memberFile != file && member.isBitmap()) {
            Palette crossFilePalette = resolvePaletteCrossFile(member, memberFile);
            if (crossFilePalette != null) {
                Optional<Bitmap> result = memberFile.decodeBitmap(member, crossFilePalette);
                if (result.isPresent()) {
                    return result;
                }
            }
        }

        if (memberFile != null) {
            Optional<Bitmap> result = memberFile.decodeBitmap(member);
            if (result.isPresent()) {
                return result;
            }
        }

        // Fallback: try main file
        if (file != null && file != memberFile) {
            Optional<Bitmap> result = file.decodeBitmap(member);
            if (result.isPresent()) {
                return result;
            }
        }

        // Last resort: try all external casts
        if (castLibManager != null) {
            for (CastLib castLib : castLibManager.getCastLibs().values()) {
                if (!castLib.isLoaded()) continue;
                DirectorFile src = castLib.getSourceFile();
                if (src != null && src != memberFile && src != file) {
                    Optional<Bitmap> result = src.decodeBitmap(member);
                    if (result.isPresent()) {
                        return result;
                    }
                }
            }
        }

        return Optional.empty();
    }

    /**
     * Resolve palette cross-file for an external cast bitmap.
     * If the bitmap's palette can't be found in its own file, tries the main movie
     * file and other loaded cast libraries.
     * @return The resolved palette, or null if no cross-file resolution is needed
     */
    private Palette resolvePaletteCrossFile(CastMemberChunk member, DirectorFile memberFile) {
        if (member.specificData() == null || member.specificData().length < 10) {
            return null;
        }

        int dirVer = 1200;
        if (memberFile.getConfig() != null) {
            dirVer = memberFile.getConfig().directorVersion();
        }
        BitmapInfo info = BitmapInfo.parse(member.specificData(), dirVer);
        int paletteId = info.paletteId();
        int paletteCastLib = info.paletteCastLib();

        // Built-in palettes (negative IDs) don't need cross-file resolution
        if (paletteId < 0) {
            return null;
        }

        // Check if the palette exists in the member's own file
        Palette pal = memberFile.resolvePaletteExact(paletteId);
        if (pal != null) {
            return null; // Found in own file — no override needed
        }

        // Director bitmaps can explicitly reference a palette member in another castLib.
        // External room/library casts rely on this for authored palette sharing, so honor
        // the castLib hint before falling back to opportunistic cross-file scans.
        if (paletteCastLib > 0 && castLibManager != null) {
            pal = castLibManager.resolvePaletteByMember(paletteCastLib, paletteId + 1);
            if (pal != null) {
                return pal;
            }
        }

        // Not found in own file — try main movie file
        if (file != null) {
            pal = file.resolvePaletteExact(paletteId);
            if (pal != null) {
                return pal;
            }
        }

        // Try other loaded cast libraries
        if (castLibManager != null) {
            for (CastLib castLib : castLibManager.getCastLibs().values()) {
                if (!castLib.isLoaded()) continue;
                DirectorFile src = castLib.getSourceFile();
                if (src != null && src != memberFile && src != file) {
                    pal = src.resolvePaletteExact(paletteId);
                    if (pal != null) {
                        return pal;
                    }
                }
            }
        }

        return null;
    }

    /**
     * Decode a bitmap with a palette override.
     * Used for palette swap animation where the runtime palette differs from the embedded one.
     */
    public Optional<Bitmap> decodeBitmap(CastMemberChunk member, Palette paletteOverride) {
        DirectorFile memberFile = member.file();
        if (memberFile != null) {
            Optional<Bitmap> result = memberFile.decodeBitmap(member, paletteOverride);
            if (result.isPresent()) {
                return result;
            }
        }
        if (file != null && file != memberFile) {
            Optional<Bitmap> result = file.decodeBitmap(member, paletteOverride);
            if (result.isPresent()) {
                return result;
            }
        }
        if (castLibManager != null) {
            for (CastLib castLib : castLibManager.getCastLibs().values()) {
                if (!castLib.isLoaded()) continue;
                DirectorFile src = castLib.getSourceFile();
                if (src != null && src != memberFile && src != file) {
                    Optional<Bitmap> result = src.decodeBitmap(member, paletteOverride);
                    if (result.isPresent()) {
                        return result;
                    }
                }
            }
        }
        return Optional.empty();
    }

    /**
     * Get the movie's current palette (from score's palette channel).
     * In Director's 8-bit color model, all bitmaps on stage share this palette.
     * Returns null if no palette channel is set (use bitmap's own palette).
     */
    public Palette getMoviePalette() {
        int currentFrame = frameContext.getCurrentFrame() - 1; // 0-indexed
        if (currentFrame != moviePaletteFrame) {
            moviePaletteFrame = currentFrame;
            moviePalette = resolveMoviePalette(currentFrame);
        }
        return moviePalette;
    }

    private Palette resolveMoviePalette(int frame) {
        if (file == null) return null;

        // Priority 1: Score palette channel (per-frame)
        var paletteData = file.getScorePalette(frame);
        if (paletteData != null) {
            // ScummVM: negative member values are built-in palette IDs
            if (paletteData.castMember() < 0) {
                return Palette.getBuiltIn(paletteData.castMember());
            }
            Palette pal = resolvePaletteByMember(paletteData.castLib(), paletteData.castMember());
            if (pal != null) return pal;
        }

        // Priority 2: Config default palette (movie-level)
        var config = file.getConfig();
        if (config != null && config.defaultPaletteMember() != 0) {
            int castLib = config.defaultPaletteCastLib();
            int member = config.defaultPaletteMember();
            // ScummVM: negative member values are built-in palette IDs
            if (member < 0) {
                return Palette.getBuiltIn(member);
            }
            Palette pal = resolvePaletteByMember(castLib, member);
            if (pal != null) return pal;
        }

        return null;
    }

    private Palette resolvePaletteByMember(int castLib, int memberNum) {
        if (castLibManager != null) {
            Palette dynamicOrFilePalette = castLibManager.resolvePaletteByMember(
                    castLib > 0 ? castLib : 1, memberNum);
            if (dynamicOrFilePalette != null) {
                return dynamicOrFilePalette;
            }
        }
        if (file != null) {
            return file.resolvePaletteByMemberNumber(memberNum);
        }
        return null;
    }
}
