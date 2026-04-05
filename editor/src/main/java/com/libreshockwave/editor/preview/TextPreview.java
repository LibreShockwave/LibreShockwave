package com.libreshockwave.editor.preview;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.chunks.TextChunk;
import com.libreshockwave.editor.model.CastMemberInfo;
import com.libreshockwave.editor.scanning.MemberResolver;

/**
 * Generates text member preview content.
 */
public class TextPreview {

    /**
     * Formats text member content for display.
     */
    public String format(DirectorFile dirFile, CastMemberInfo memberInfo) {
        StringBuilder sb = new StringBuilder();
        String typeName = memberInfo.memberType() == MemberType.BUTTON ? "BUTTON" : "TEXT";
        PreviewFormatUtils.appendMemberHeader(sb, typeName, memberInfo, true);

        // Find the text chunk for this member
        TextChunk textChunk = MemberResolver.findTextForMember(dirFile, memberInfo.member());

        if (textChunk != null) {
            String text = textChunk.text();
            // Normalize line endings: \r\n -> \n, \r -> \n
            text = text.replace("\r\n", "\n").replace("\r", "\n");

            sb.append("--- Text Content ---\n");
            sb.append(text);
            sb.append("\n\n");

            // Show formatting info if present
            if (!textChunk.runs().isEmpty()) {
                sb.append("--- Formatting Runs ---\n");
                for (TextChunk.TextRun run : textChunk.runs()) {
                    sb.append(String.format("  Offset %d: Font #%d, Size %d, Style 0x%02X\n",
                            run.startOffset(), run.fontId(), run.fontSize(), run.fontStyle()));
                }
            }
        } else {
            sb.append("[Text data not found]\n");
        }

        return sb.toString();
    }
}
