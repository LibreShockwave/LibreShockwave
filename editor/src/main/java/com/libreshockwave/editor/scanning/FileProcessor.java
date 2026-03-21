package com.libreshockwave.editor.scanning;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.cast.BitmapInfo;
import com.libreshockwave.cast.FilmLoopInfo;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.cast.ShapeInfo;
import com.libreshockwave.chunks.*;
import com.libreshockwave.format.ScriptFormatUtils;
import com.libreshockwave.editor.model.CastMemberInfo;

import java.util.ArrayList;
import java.util.List;

/**
 * Processes Director files and extracts member information.
 */
public class FileProcessor {

    public FileProcessor() {
    }

    /**
     * Process an already-loaded DirectorFile and return info for all non-null cast members.
     */
    public List<CastMemberInfo> processMembers(DirectorFile dirFile) {
        List<CastMemberInfo> members = new ArrayList<>();

        for (CastMemberChunk member : dirFile.getCastMembers()) {
            if (member.memberType() == MemberType.NULL) {
                continue;
            }

            String name = member.name();
            if (name == null || name.isEmpty()) {
                name = "Unnamed #" + member.id().value();
            }

            String details = buildMemberDetails(dirFile, member);

            members.add(new CastMemberInfo(
                    member.id().value(), name, member, member.memberType(), details
            ));
        }

        return members;
    }

    public String buildMemberDetails(DirectorFile dirFile, CastMemberChunk member) {
        MemberType type = member.memberType();
        String details = "";

        // Parse type-specific details
        if (type == MemberType.BITMAP && member.specificData().length > 0) {
            try {
                BitmapInfo info = BitmapInfo.parse(member.specificData());
                details = String.format("%dx%d, %d-bit", info.width(), info.height(), info.bitDepth());
            } catch (Exception ignored) {}
        } else if (type == MemberType.SHAPE && member.specificData().length > 0) {
            try {
                ShapeInfo info = ShapeInfo.parse(member.specificData());
                details = String.format("%s %dx%d", info.shapeType(), info.width(), info.height());
            } catch (Exception ignored) {}
        } else if (type == MemberType.FILM_LOOP && member.specificData().length > 0) {
            try {
                FilmLoopInfo info = FilmLoopInfo.parse(member.specificData());
                details = String.format("%dx%d", info.width(), info.height());
            } catch (Exception ignored) {}
        } else if (type == MemberType.SCRIPT) {
            details = buildScriptDetails(dirFile, member);
        } else if (type == MemberType.SOUND) {
            details = buildSoundDetails(dirFile, member);
        } else if (type == MemberType.PALETTE) {
            details = buildPaletteDetails(dirFile, member);
        } else if (type == MemberType.TEXT || type == MemberType.BUTTON) {
            details = buildTextDetails(dirFile, member);
        }

        return details;
    }

    private String buildScriptDetails(DirectorFile dirFile, CastMemberChunk member) {
        ScriptChunk script = MemberResolver.findScriptForMember(dirFile, member);
        if (script != null) {
            ScriptNamesChunk scriptNames = dirFile.getScriptNames();
            String scriptTypeName = ScriptFormatUtils.getScriptTypeName(script.getScriptType());

            // Build handler list summary
            List<String> handlerNames = new ArrayList<>();
            if (scriptNames != null) {
                for (ScriptChunk.Handler h : script.handlers()) {
                    String hName = ScriptFormatUtils.resolveName(scriptNames, h.nameId());
                    if (!hName.startsWith("<")) {
                        handlerNames.add(hName);
                    }
                }
            }

            // Format: "Movie Script" or "Parent Script" with handler names
            if (!handlerNames.isEmpty()) {
                String handlers = handlerNames.size() <= 3 ?
                        String.join(", ", handlerNames) :
                        handlerNames.get(0) + ", " + handlerNames.get(1) + "... +" + (handlerNames.size() - 2);
                return String.format("%s [%s]", scriptTypeName, handlers);
            } else {
                return scriptTypeName;
            }
        }
        return "";
    }

    private String buildSoundDetails(DirectorFile dirFile, CastMemberChunk member) {
        SoundChunk soundChunk = MemberResolver.findSoundForMember(dirFile, member);
        if (soundChunk != null) {
            String codec = soundChunk.isMp3() ? "MP3" : "PCM";
            double duration = soundChunk.durationSeconds();
            return String.format("%s, %dHz, %.1fs", codec, soundChunk.sampleRate(), duration);
        }
        return "sound data";
    }

    private String buildPaletteDetails(DirectorFile dirFile, CastMemberChunk member) {
        PaletteChunk paletteChunk = MemberResolver.findPaletteForMember(dirFile, member);
        if (paletteChunk != null) {
            return paletteChunk.colors().length + " colors";
        }
        return "palette";
    }

    private String buildTextDetails(DirectorFile dirFile, CastMemberChunk member) {
        TextChunk textChunk = MemberResolver.findTextForMember(dirFile, member);
        if (textChunk != null) {
            String text = ScriptFormatUtils.normalizeLineEndings(textChunk.text());
            text = ScriptFormatUtils.truncate(text, 50);
            return "\"" + text + "\"";
        }
        return "";
    }
}
