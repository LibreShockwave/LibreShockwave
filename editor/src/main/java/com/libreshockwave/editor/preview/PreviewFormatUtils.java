package com.libreshockwave.editor.preview;

import com.libreshockwave.editor.model.FrameAppearance;
import com.libreshockwave.editor.model.CastMemberInfo;
import com.libreshockwave.editor.score.FrameAppearanceFinder;

import java.util.List;

final class PreviewFormatUtils {

    private PreviewFormatUtils() {}

    static void appendMemberHeader(StringBuilder sb, String memberKind, CastMemberInfo memberInfo, boolean blankLineAfterId) {
        sb.append("=== ").append(memberKind).append(": ").append(memberInfo.name()).append(" ===\n\n");
        sb.append("Member ID: ").append(memberInfo.memberNum()).append("\n");
        if (blankLineAfterId) {
            sb.append("\n");
        }
    }

    static void appendPaletteInfo(StringBuilder sb, int[] colors) {
        sb.append("--- Palette Info ---\n");
        sb.append("Color Count: ").append(colors.length).append("\n");
        sb.append("\n--- Colors ---\n");
        for (int i = 0; i < colors.length; i++) {
            int c = colors[i];
            int r = (c >> 16) & 0xFF;
            int g = (c >> 8) & 0xFF;
            int b = c & 0xFF;
            sb.append(String.format("[%3d] #%02X%02X%02X (R:%3d G:%3d B:%3d)\n", i, r, g, b, r, g, b));
        }
    }

    static void appendScoreAppearances(
            StringBuilder sb,
            List<FrameAppearance> appearances,
            FrameAppearanceFinder appearanceFinder,
            boolean includePosition) {
        if (appearances.isEmpty()) {
            sb.append("Not used in score\n");
            return;
        }

        sb.append(appearanceFinder.format(appearances)).append("\n");
        if (appearances.size() > 20) {
            return;
        }

        sb.append("\nDetailed appearances:\n");
        for (FrameAppearance app : appearances) {
            if (includePosition) {
                sb.append(String.format(
                        "  Frame %d, %s at (%d, %d)",
                        app.frame(),
                        app.channelName(),
                        app.posX(),
                        app.posY()));
            } else {
                sb.append(String.format("  Frame %d, %s", app.frame(), app.channelName()));
            }
            if (app.frameLabel() != null) {
                sb.append(" [").append(app.frameLabel()).append("]");
            }
            sb.append("\n");
        }
    }
}
