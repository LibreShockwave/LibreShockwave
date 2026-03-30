package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class LocalHabboBootstrapTest {

    private static final Path ARCHIVE_DIR =
            Path.of("/tmp/habbo_origins_src/archive/unprotected_client_dump_20_06_2024");

    @Test
    void localHabboStartsWithoutBrokerManagerRecursion() throws Exception {
        Path moviePath = ARCHIVE_DIR.resolve("habbo.dcr");
        Path fuseClientPath = ARCHIVE_DIR.resolve("fuse_client.cct");
        Path emptyCastPath = ARCHIVE_DIR.resolve("empty.cct");

        if (!Files.isRegularFile(moviePath) || !Files.isRegularFile(fuseClientPath)
                || !Files.isRegularFile(emptyCastPath)) {
            return;
        }

        DirectorFile file = DirectorFile.load(moviePath);
        Player player = new Player(file);
        player.setInitialBuiltinVariable("connection.info.id", Datum.symbol("info"));
        player.setInitialBuiltinVariable("connection.room.id", Datum.symbol("room"));
        player.setCompatibilityProfile(new FuseCompatibilityProfile());

        player.getCastLibManager().setExternalCastDataByUrl("fuse_client.cct", Files.readAllBytes(fuseClientPath));
        player.getCastLibManager().setExternalCastDataByUrl("empty.cct", Files.readAllBytes(emptyCastPath));

        player.play();

        String error = player.getRecentScriptErrorMessage(60_000);
        String stack = player.getRecentScriptErrorStack(60_000);
        assertEquals("", error, () -> error + "\n" + stack);
        assertTrue(player.getState() == PlayerState.PLAYING || player.getState() == PlayerState.PAUSED);
    }

    @Test
    void localHabboStartsWhenExternalCastsWereOnlyPreFetched() throws Exception {
        Path moviePath = ARCHIVE_DIR.resolve("habbo.dcr");
        Path fuseClientPath = ARCHIVE_DIR.resolve("fuse_client.cct");
        Path emptyCastPath = ARCHIVE_DIR.resolve("empty.cct");

        if (!Files.isRegularFile(moviePath) || !Files.isRegularFile(fuseClientPath)
                || !Files.isRegularFile(emptyCastPath)) {
            return;
        }

        DirectorFile file = DirectorFile.load(moviePath);
        Player player = new Player(file);
        player.setInitialBuiltinVariable("connection.info.id", Datum.symbol("info"));
        player.setInitialBuiltinVariable("connection.room.id", Datum.symbol("room"));
        player.setCompatibilityProfile(new FuseCompatibilityProfile());

        player.onNetFetchComplete("fuse_client.cct", Files.readAllBytes(fuseClientPath));
        player.onNetFetchComplete("empty.cct", Files.readAllBytes(emptyCastPath));

        player.play();

        String error = player.getRecentScriptErrorMessage(60_000);
        String stack = player.getRecentScriptErrorStack(60_000);
        assertEquals("", error, () -> error + "\n" + stack);
        assertTrue(player.getState() == PlayerState.PLAYING || player.getState() == PlayerState.PAUSED);
    }

}
