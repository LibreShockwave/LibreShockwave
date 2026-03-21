package com.libreshockwave.editor.extraction;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.audio.SoundConverter;
import com.libreshockwave.chunks.PaletteChunk;
import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.chunks.ScriptNamesChunk;
import com.libreshockwave.chunks.SoundChunk;
import com.libreshockwave.chunks.TextChunk;
import com.libreshockwave.format.ScriptFormatUtils;
import com.libreshockwave.editor.format.InstructionFormatter;
import com.libreshockwave.editor.model.CastMemberInfo;
import com.libreshockwave.editor.scanning.MemberResolver;

import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * Extracts assets (bitmaps and sounds) from Director files.
 */
public class AssetExtractor {

    /**
     * Result of an extraction operation.
     */
    public record ExtractionResult(int bitmapsExtracted, int soundsExtracted) {}

    /**
     * Extracts a single asset and returns true if successful.
     */
    public boolean extract(DirectorFile dirFile, CastMemberInfo memberInfo, Path outputDir) {
        // Use the member's own DirectorFile for chunk resolution (essential for external casts)
        DirectorFile memberFile = memberInfo.member().file();
        if (memberFile == null) memberFile = dirFile;

        try {
            Files.createDirectories(outputDir);

            // Sanitize filename
            String safeName = memberInfo.name()
                    .replaceAll("[^a-zA-Z0-9._-]", "_");

            return switch (memberInfo.memberType()) {
                case BITMAP -> extractBitmap(memberFile, memberInfo, outputDir, safeName);
                case SOUND -> extractSound(memberFile, memberInfo, outputDir, safeName);
                case SCRIPT -> extractScript(memberFile, memberInfo, outputDir, safeName);
                case TEXT, BUTTON -> extractText(memberFile, memberInfo, outputDir, safeName);
                case PALETTE -> extractPalette(memberFile, memberInfo, outputDir, safeName);
                default -> extractGeneric(memberFile, memberInfo, outputDir, safeName);
            };
        } catch (Exception e) {
            return false;
        }
    }

    private boolean extractBitmap(DirectorFile dirFile, CastMemberInfo memberInfo, Path subDir, String safeName) {
        try {
            if (safeName.isEmpty()) {
                safeName = "bitmap_" + memberInfo.memberNum();
            }

            var bitmapOpt = dirFile.decodeBitmap(memberInfo.member());
            if (bitmapOpt.isPresent()) {
                BufferedImage image = bitmapOpt.get().toBufferedImage();
                Path outputFile = resolveUnique(subDir, safeName, ".png");
                ImageIO.write(image, "PNG", outputFile.toFile());
                return true;
            }
        } catch (Exception e) {
            // Ignore
        }
        return false;
    }

    private boolean extractSound(DirectorFile dirFile, CastMemberInfo memberInfo, Path subDir, String safeName) {
        try {
            if (safeName.isEmpty()) {
                safeName = "sound_" + memberInfo.memberNum();
            }

            SoundChunk soundChunk = MemberResolver.findSoundForMember(dirFile, memberInfo.member());
            if (soundChunk != null) {
                byte[] audioData;
                String extension;

                if (soundChunk.isMp3()) {
                    audioData = SoundConverter.extractMp3(soundChunk);
                    extension = ".mp3";
                } else {
                    audioData = SoundConverter.toWav(soundChunk);
                    extension = ".wav";
                }

                if (audioData != null && audioData.length > 0) {
                    Path outputFile = resolveUnique(subDir, safeName, extension);
                    Files.write(outputFile, audioData);
                    return true;
                }
            }
        } catch (Exception e) {
            // Ignore
        }
        return false;
    }

    private boolean extractScript(DirectorFile dirFile, CastMemberInfo memberInfo, Path subDir, String safeName) {
        try {
            if (safeName.isEmpty()) {
                safeName = "script_" + memberInfo.memberNum();
            }

            ScriptChunk script = MemberResolver.findScriptForMember(dirFile, memberInfo.member());
            ScriptNamesChunk names = dirFile.getScriptNames();

            StringBuilder sb = new StringBuilder();
            if (script == null) {
                sb.append("-- No bytecode found for script member #").append(memberInfo.memberNum()).append("\n");
            } else {
                sb.append("-- Script: ").append(memberInfo.name()).append("\n");
                sb.append("-- Type: ").append(ScriptFormatUtils.getScriptTypeName(script.getScriptType())).append("\n\n");

                // Properties
                for (ScriptChunk.PropertyEntry prop : script.properties()) {
                    String propName = names != null ? names.getName(prop.nameId()) : "#" + prop.nameId();
                    sb.append("property ").append(propName).append("\n");
                }
                if (!script.properties().isEmpty()) sb.append("\n");

                // Globals
                for (ScriptChunk.GlobalEntry global : script.globals()) {
                    String globalName = names != null ? names.getName(global.nameId()) : "#" + global.nameId();
                    sb.append("global ").append(globalName).append("\n");
                }
                if (!script.globals().isEmpty()) sb.append("\n");

                // Handlers
                for (ScriptChunk.Handler handler : script.handlers()) {
                    String handlerName = names != null ? names.getName(handler.nameId()) : "#" + handler.nameId();
                    List<String> argNames = new ArrayList<>();
                    for (int argId : handler.argNameIds()) {
                        argNames.add(names != null ? names.getName(argId) : "#" + argId);
                    }

                    sb.append("on ").append(handlerName);
                    if (!argNames.isEmpty()) {
                        sb.append(" ").append(String.join(", ", argNames));
                    }
                    sb.append("\n");

                    for (ScriptChunk.Handler.Instruction instr : handler.instructions()) {
                        sb.append("  ").append(InstructionFormatter.format(instr, script, names)).append("\n");
                    }
                    sb.append("end\n\n");
                }
            }

            Path outputFile = resolveUnique(subDir, safeName, ".ls");
            Files.writeString(outputFile, sb.toString(), StandardCharsets.UTF_8);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    private boolean extractText(DirectorFile dirFile, CastMemberInfo memberInfo, Path subDir, String safeName) {
        try {
            if (safeName.isEmpty()) {
                safeName = "text_" + memberInfo.memberNum();
            }

            TextChunk textChunk = MemberResolver.findTextForMember(dirFile, memberInfo.member());
            if (textChunk != null) {
                String text = textChunk.text().replace("\r\n", "\n").replace("\r", "\n");
                Path outputFile = resolveUnique(subDir, safeName, ".txt");
                Files.writeString(outputFile, text, StandardCharsets.UTF_8);
                return true;
            }
        } catch (Exception e) {
            // Ignore
        }
        return false;
    }

    private boolean extractPalette(DirectorFile dirFile, CastMemberInfo memberInfo, Path subDir, String safeName) {
        try {
            if (safeName.isEmpty()) {
                safeName = "palette_" + memberInfo.memberNum();
            }

            PaletteChunk paletteChunk = MemberResolver.findPaletteForMember(dirFile, memberInfo.member());
            if (paletteChunk != null) {
                int[] colors = paletteChunk.colors();
                // Export as JASC-PAL format (compatible with most editors)
                StringBuilder sb = new StringBuilder();
                sb.append("JASC-PAL\n0100\n").append(colors.length).append("\n");
                for (int c : colors) {
                    int r = (c >> 16) & 0xFF;
                    int g = (c >> 8) & 0xFF;
                    int b = c & 0xFF;
                    sb.append(r).append(" ").append(g).append(" ").append(b).append("\n");
                }

                Path outputFile = resolveUnique(subDir, safeName, ".pal");
                Files.writeString(outputFile, sb.toString(), StandardCharsets.UTF_8);
                return true;
            }
        } catch (Exception e) {
            // Ignore
        }
        return false;
    }

    private boolean extractGeneric(DirectorFile dirFile, CastMemberInfo memberInfo, Path subDir, String safeName) {
        try {
            if (safeName.isEmpty()) {
                safeName = memberInfo.memberType().getName() + "_" + memberInfo.memberNum();
            }

            byte[] specificData = memberInfo.member().specificData();
            if (specificData.length > 0) {
                Path outputFile = resolveUnique(subDir, safeName, ".bin");
                Files.write(outputFile, specificData);
                return true;
            }
        } catch (Exception e) {
            // Ignore
        }
        return false;
    }

    private Path resolveUnique(Path dir, String baseName, String extension) {
        Path path = dir.resolve(baseName + extension);
        int counter = 1;
        while (Files.exists(path)) {
            path = dir.resolve(baseName + "_" + counter + extension);
            counter++;
        }
        return path;
    }
}
