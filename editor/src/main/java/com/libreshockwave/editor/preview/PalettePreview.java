package com.libreshockwave.editor.preview;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.chunks.PaletteChunk;
import com.libreshockwave.editor.model.CastMemberInfo;
import com.libreshockwave.editor.scanning.MemberResolver;

import java.awt.image.BufferedImage;

/**
 * Generates palette preview content.
 */
public class PalettePreview {

    /**
     * Result of palette preview generation.
     */
    public record PaletteResult(BufferedImage swatchImage, int colorCount) {}

    /**
     * Generates a palette swatch image.
     */
    public PaletteResult generateSwatch(DirectorFile dirFile, CastMemberInfo memberInfo) {
        PaletteChunk paletteChunk = MemberResolver.findPaletteForMember(dirFile, memberInfo.member());
        if (paletteChunk != null) {
            int[] colors = paletteChunk.colors();
            Bitmap swatch = Bitmap.createPaletteSwatch(colors, 16, 16);
            return new PaletteResult(swatch.toBufferedImage(), colors.length);
        }
        return null;
    }

    /**
     * Formats palette details as text.
     */
    public String format(DirectorFile dirFile, CastMemberInfo memberInfo) {
        StringBuilder sb = new StringBuilder();
        PreviewFormatUtils.appendMemberHeader(sb, "PALETTE", memberInfo, true);

        PaletteChunk paletteChunk = MemberResolver.findPaletteForMember(dirFile, memberInfo.member());
        if (paletteChunk != null) {
            PreviewFormatUtils.appendPaletteInfo(sb, paletteChunk.colors());
        } else {
            sb.append("[Palette data not found]\n");
        }

        return sb.toString();
    }
}
