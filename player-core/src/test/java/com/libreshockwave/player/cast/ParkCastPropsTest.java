package com.libreshockwave.player.cast;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.player.Player;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ParkCastPropsTest {

    private static final Path PARK_CAST =
            Path.of("temp/habbo_origins_src/archive/unprotected_client_dump_20_06_2024/hh_room_park.cst");
    private static final Path PARK_COMPRESSED_CAST =
            Path.of("temp/habbo_origins_src/archive/unprotected_client_dump_20_06_2024/hh_room_park.cct");
    private static final Path PARK_MOVIE =
            Path.of("temp/habbo_origins_src/archive/unprotected_client_dump_20_06_2024/hh_room_park.dcr");
    private static final Path HABBO_MOVIE =
            Path.of("temp/habbo_origins_src/archive/unprotected_client_dump_20_06_2024/habbo.dcr");

    @Test
    void standaloneParkBenchPropsLoad() throws Exception {
        if (!Files.isRegularFile(PARK_CAST)) {
            return;
        }

        DirectorFile file = DirectorFile.load(PARK_CAST);
        CastLib castLib = new CastLib(1, null, null);
        castLib.setSourceFile(file);
        castLib.load();

        assertEquals("[\"a\": [#ink: 36]]", castLib.getMemberByName("bench.props").getTextContent());
        assertEquals("[\"a\": [#ink: 36]]", castLib.getMemberByName("bench2.props").getTextContent());
    }

    @Test
    void movieLevelParkBenchPropsParseToPropList() throws Exception {
        if (!Files.isRegularFile(PARK_MOVIE) || !Files.isRegularFile(PARK_COMPRESSED_CAST)) {
            return;
        }

        DirectorFile movie = DirectorFile.load(PARK_MOVIE);
        Player player = new Player(movie);
        try {
            LingoVM vm = player.getVM();

            Datum benchEncoded = vm.callHandler("getmemnum", java.util.List.of(Datum.of("bench.props")));
            Datum benchField = vm.callHandler("field", java.util.List.of(benchEncoded));
            Datum benchParsed = vm.callHandler("value", java.util.List.of(benchField));

            Datum bench2Encoded = vm.callHandler("getmemnum", java.util.List.of(Datum.of("bench2.props")));
            Datum bench2Field = vm.callHandler("field", java.util.List.of(bench2Encoded));
            Datum bench2Parsed = vm.callHandler("value", java.util.List.of(bench2Field));

            assertTrue(benchParsed.isPropList(), "bench.props parsed as " + benchParsed);
            assertTrue(bench2Parsed.isPropList(), "bench2.props parsed as " + bench2Parsed);

            Datum.PropList benchProps = (Datum.PropList) benchParsed;
            Datum.PropList bench2Props = (Datum.PropList) bench2Parsed;

            assertEquals(36, ((Datum.PropList) benchProps.get("a", false)).get("ink", true).toInt());
            assertEquals(36, ((Datum.PropList) bench2Props.get("a", false)).get("ink", true).toInt());
        } finally {
            player.shutdown();
        }
    }

    @Test
    void habboHostMovieParkBenchPropsParseToPropList() throws Exception {
        if (!Files.isRegularFile(HABBO_MOVIE) || !Files.isRegularFile(PARK_COMPRESSED_CAST)) {
            return;
        }

        DirectorFile movie = DirectorFile.load(HABBO_MOVIE);
        Player player = new Player(movie);
        try {
            int parkCastSlot = findParkCastSlot(player);
            assertTrue(parkCastSlot > 0, "could not find authored park cast slot");
            assertTrue(player.getCastLibManager().setExternalCastData(parkCastSlot, Files.readAllBytes(PARK_COMPRESSED_CAST)));

            LingoVM vm = player.getVM();

            Datum benchEncoded = vm.callHandler("getmemnum", java.util.List.of(Datum.of("bench.props")));
            Datum benchField = vm.callHandler("field", java.util.List.of(benchEncoded));
            Datum benchParsed = vm.callHandler("value", java.util.List.of(benchField));

            Datum bench2Encoded = vm.callHandler("getmemnum", java.util.List.of(Datum.of("bench2.props")));
            Datum bench2Field = vm.callHandler("field", java.util.List.of(bench2Encoded));
            Datum bench2Parsed = vm.callHandler("value", java.util.List.of(bench2Field));

            assertTrue(benchParsed.isPropList(), "bench.props parsed as " + benchParsed + " from " + benchField);
            assertTrue(bench2Parsed.isPropList(), "bench2.props parsed as " + bench2Parsed + " from " + bench2Field);
        } finally {
            player.shutdown();
        }
    }

    @Test
    void retargetingExternalCastFileNameClearsStaleParkPropsUntilReload() throws Exception {
        if (!Files.isRegularFile(HABBO_MOVIE) || !Files.isRegularFile(PARK_COMPRESSED_CAST)) {
            return;
        }

        DirectorFile movie = DirectorFile.load(HABBO_MOVIE);
        Player player = new Player(movie);
        try {
            int parkCastSlot = findParkCastSlot(player);
            assertTrue(parkCastSlot > 0, "could not find authored park cast slot");
            assertTrue(player.getCastLibManager().setExternalCastData(parkCastSlot, Files.readAllBytes(PARK_COMPRESSED_CAST)));

            LingoVM vm = player.getVM();
            Datum encodedBeforeRetarget = vm.callHandler("getmemnum", java.util.List.of(Datum.of("bench.props")));
            assertTrue(encodedBeforeRetarget.toInt() > 0, "bench.props should resolve before retarget");

            player.getCastLibManager().setCastLibProp(
                    parkCastSlot,
                    "fileName",
                    Datum.of("https://example.invalid/dcr/external/hh_room_cinema.cct"));

            assertEquals(Datum.VOID, player.getCastLibManager().getMemberByName(parkCastSlot, "bench.props"));

            Datum encodedAfterRetarget = vm.callHandler("getmemnum", java.util.List.of(Datum.of("bench.props")));
            assertEquals(0, encodedAfterRetarget.toInt(), "bench.props should not resolve after retarget without reload");

            Datum staleField = vm.callHandler("field", java.util.List.of(encodedBeforeRetarget));
            Datum staleParsed = vm.callHandler("value", java.util.List.of(staleField));

            assertEquals("", staleField.toStr(), "stale slot should no longer expose old bench.props text");
            assertTrue(staleParsed.isVoid(), "stale slot should parse as VOID after retarget, got " + staleParsed);
        } finally {
            player.shutdown();
        }
    }

    private static int findParkCastSlot(Player player) {
        return player.getCastLibManager().getCastLibs().entrySet().stream()
                .filter(entry -> {
                    String fileName = entry.getValue().getFileName();
                    return fileName != null && fileName.toLowerCase().contains("hh_room_park");
                })
                .map(java.util.Map.Entry::getKey)
                .findFirst()
                .orElse(-1);
    }
}
