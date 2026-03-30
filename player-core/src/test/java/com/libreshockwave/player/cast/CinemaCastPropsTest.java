package com.libreshockwave.player.cast;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.chunks.KeyTableChunk;
import com.libreshockwave.player.Player;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.stream.Collectors;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class CinemaCastPropsTest {

    private static final Path CINEMA_CAST =
            Path.of("temp/habbo_origins_src/archive/unprotected_client_dump_20_06_2024/hh_room_cinema.cst");
    private static final Path CINEMA_COMPRESSED_CAST =
            Path.of("temp/habbo_origins_src/archive/unprotected_client_dump_20_06_2024/hh_room_cinema.cct");
    private static final Path HABBO_MOVIE =
            Path.of("temp/habbo_origins_src/archive/unprotected_client_dump_20_06_2024/habbo.dcr");

    @Test
    void cinemaLightpolePropsTextLoads() throws Exception {
        if (!Files.isRegularFile(CINEMA_CAST)) {
            return;
        }

        DirectorFile file = DirectorFile.load(CINEMA_CAST);
        CastLib castLib = new CastLib(1, null, null);
        castLib.setSourceFile(file);
        castLib.load();

        CastMemberChunk chunk = castLib.findMemberByName("lightpole.props");
        assertNotNull(chunk, "lightpole.props chunk missing");

        int memberNumber = castLib.getMemberNumber(chunk);
        CastMember member = castLib.getMember(memberNumber);
        assertNotNull(member, "lightpole.props member missing");

        String debugEntries = file.getKeyTable().getEntriesForOwner(chunk.id()).stream()
                .map(entry -> entry.fourccString() + ":" + entry.sectionId().value())
                .collect(Collectors.joining(", "));

        String text = member.getTextContent();
        assertEquals("[\"a\": [#ink: 33]]", text,
                () -> "entries=" + debugEntries + " rawText=" + text);
    }

    @Test
    void cinemaLightpolePropsHasStyledTextEntry() throws Exception {
        if (!Files.isRegularFile(CINEMA_CAST)) {
            return;
        }

        DirectorFile file = DirectorFile.load(CINEMA_CAST);
        CastLib castLib = new CastLib(1, null, null);
        castLib.setSourceFile(file);
        castLib.load();

        CastMemberChunk chunk = castLib.findMemberByName("lightpole.props");
        assertNotNull(chunk, "lightpole.props chunk missing");

        KeyTableChunk keyTable = file.getKeyTable();
        assertNotNull(keyTable, "missing KEY* table");

        boolean hasStxt = keyTable.getEntriesForOwner(chunk.id()).stream()
                .anyMatch(entry -> "STXT".equals(entry.fourccString()));
        assertTrue(hasStxt, "lightpole.props has no STXT entry");
    }

    @Test
    void compressedCinemaLightpolePropsTextLoads() throws Exception {
        if (!Files.isRegularFile(CINEMA_COMPRESSED_CAST)) {
            return;
        }

        DirectorFile file = DirectorFile.load(CINEMA_COMPRESSED_CAST);
        CastLib castLib = new CastLib(1, null, null);
        castLib.setSourceFile(file);
        castLib.load();

        CastMemberChunk chunk = castLib.findMemberByName("lightpole.props");
        assertNotNull(chunk, "lightpole.props chunk missing in cct");

        int memberNumber = castLib.getMemberNumber(chunk);
        CastMember member = castLib.getMember(memberNumber);
        assertNotNull(member, "lightpole.props member missing in cct");

        String debugEntries = file.getKeyTable().getEntriesForOwner(chunk.id()).stream()
                .map(entry -> entry.fourccString() + ":" + entry.sectionId().value())
                .collect(Collectors.joining(", "));

        String text = member.getTextContent();
        assertEquals("[\"a\": [#ink: 33]]", text,
                () -> "entries=" + debugEntries + " rawText=" + text);
    }

    @Test
    void movieLevelLookupFindsCinemaLightpoleProps() throws Exception {
        if (!Files.isRegularFile(HABBO_MOVIE) || !Files.isRegularFile(CINEMA_COMPRESSED_CAST)) {
            return;
        }

        DirectorFile movie = DirectorFile.load(HABBO_MOVIE);
        Player player = new Player(movie);
        try {
            boolean loaded = player.getCastLibManager().setExternalCastData(12, Files.readAllBytes(CINEMA_COMPRESSED_CAST));
            assertTrue(loaded, "failed to load cinema cast into slot 12");

            Datum memberRef = player.getCastLibManager().getMemberByName(0, "lightpole.props");
            assertTrue(memberRef instanceof Datum.CastMemberRef, "lookup did not return a cast member ref: " + memberRef);

            String fieldText = player.getCastLibManager().getFieldValue("lightpole.props", 0);
            assertEquals("[\"a\": [#ink: 33]]", fieldText);
        } finally {
            player.shutdown();
        }
    }

    @Test
    void movieLevelValueParsesLightpolePropsToPropList() throws Exception {
        if (!Files.isRegularFile(HABBO_MOVIE) || !Files.isRegularFile(CINEMA_COMPRESSED_CAST)) {
            return;
        }

        DirectorFile movie = DirectorFile.load(HABBO_MOVIE);
        Player player = new Player(movie);
        try {
            boolean loaded = player.getCastLibManager().setExternalCastData(12, Files.readAllBytes(CINEMA_COMPRESSED_CAST));
            assertTrue(loaded, "failed to load cinema cast into slot 12");

            LingoVM vm = player.getVM();
            Datum encoded = vm.callHandler("getmemnum", java.util.List.of(Datum.of("lightpole.props")));
            Datum field = vm.callHandler("field", java.util.List.of(encoded));
            Datum parsed = vm.callHandler("value", java.util.List.of(field));

            assertTrue(parsed.isPropList(), "parsed value was not a propList: " + parsed);

            Datum.PropList props = (Datum.PropList) parsed;
            Datum lightpolePart = props.get("a", false);
            assertTrue(lightpolePart instanceof Datum.PropList, "part a missing: " + lightpolePart);
            Datum.PropList partProps = (Datum.PropList) lightpolePart;
            Datum ink = partProps.get("ink", true);
            assertEquals(33, ink.toInt());
        } finally {
            player.shutdown();
        }
    }
}
