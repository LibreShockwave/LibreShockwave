package com.libreshockwave.editor.cli;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.editor.extraction.AssetExtractor;
import com.libreshockwave.editor.model.CastMemberInfo;
import com.libreshockwave.editor.scanning.FileProcessor;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * Headless asset exporter. Extracts every cast member's asset (bitmaps to PNG, sounds to WAV/MP3,
 * scripts, text, palettes) from a Director/Shockwave movie into per-type subdirectories of the
 * output folder. Useful for scripted/CI extraction without launching the Swing editor.
 *
 * <p>Usage: {@code AssetExportCli <input.dir|.dxr|.dcr|.cct|.cxt|.cst> <outputDir>}
 */
public final class AssetExportCli {

    private AssetExportCli() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 2) {
            System.err.println("usage: AssetExportCli <input.dir|.dxr|.dcr|.cct|.cxt|.cst> <outputDir>");
            System.exit(2);
        }

        DirectorFile file = DirectorFile.load(Path.of(args[0]));
        Path outDir = Path.of(args[1]);
        AssetExtractor extractor = new AssetExtractor();

        List<CastMemberInfo> members = new FileProcessor().processMembers(file);
        Map<String, Integer> counts = new TreeMap<>();
        int total = 0;
        int exported = 0;
        for (CastMemberInfo member : members) {
            total++;
            String type = member.memberType().name().toLowerCase();
            if (extractor.extract(file, member, outDir.resolve(type))) {
                exported++;
                counts.merge(type, 1, Integer::sum);
            }
        }

        System.out.printf("Exported %d/%d cast members to %s%n", exported, total, outDir);
        counts.forEach((type, n) -> System.out.printf("  %-12s %d%n", type, n));
    }
}
