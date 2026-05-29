package com.libreshockwave.player;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.player.input.InputState;
import com.libreshockwave.vm.xtra.Xtra;
import com.libreshockwave.vm.xtra.XtraManager;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import com.libreshockwave.DirectorFile;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

class MoviePropertiesTest {

    @Test
    void activeWindowForMainMovieIsStage() {
        MovieProperties properties = new MovieProperties(null, null);

        assertEquals(Datum.STAGE, properties.getMovieProp("activeWindow"));
    }

    @Test
    void movieNameDefaultsEmptyAndStageNameMatchesDirectorWindow() {
        MovieProperties properties = new MovieProperties(null, null);

        assertEquals("", properties.getMovieProp("name").toStr());
        assertEquals("stage", properties.getStageProp("name").toStr());
    }

    @Test
    void xtraListExposesRegisteredXtrasWithDirectorNames() {
        XtraManager xtraManager = new XtraManager();
        xtraManager.registerXtra(new FakeXtra("Multiuser"));
        MovieProperties properties = new MovieProperties(null, null, xtraManager);

        assertTrue(xtraManager.isXtraRegistered("Multiusr"));
        assertEquals(1, properties.getMovieProp("number of xtras").toInt());

        Datum.List xtraList = assertInstanceOf(Datum.List.class, properties.getMovieProp("xtraList"));
        assertEquals(1, xtraList.items().size());

        Datum.PropList entry = assertInstanceOf(Datum.PropList.class, xtraList.items().get(0));
        assertEquals("Multiusr", entry.get("name", true).toStr());
        assertEquals("Multiusr.x32", entry.get("fileName", true).toStr());
    }

    @Test
    void settingKeyboardFocusSpriteRestartsCaretBlink() {
        InputState inputState = new InputState();
        inputState.setCaretBlinkRate(1);
        inputState.setKeyboardFocusSprite(7);
        inputState.incrementCaretBlink();

        assertFalse(inputState.isCaretVisible());

        MovieProperties properties = new MovieProperties(null, null);
        properties.setInputState(inputState);
        properties.setMovieProp("keyboardFocusSprite", Datum.of(9));

        assertEquals(9, properties.getMovieProp("keyboardFocusSprite").toInt());
        assertTrue(inputState.isCaretVisible());
    }

    @Test
    void randomSeedMoviePropertyControlsVmRandomSequence() throws Exception {
        Path v1Movie = Path.of("/opt/git/v1_assets/projectorrays_lingo/habbo_entry/habbo_entry.dir");
        if (!Files.isRegularFile(v1Movie)) {
            return;
        }
        Player player = new Player(DirectorFile.load(v1Movie));
        MovieProperties properties = player.getMovieProperties();

        assertTrue(properties.setMovieProp("randomSeed", Datum.of(777)));
        int first = player.getVM().callHandler("random", List.of(Datum.of(1000))).toInt();
        int second = player.getVM().callHandler("random", List.of(Datum.of(1000))).toInt();

        properties.setMovieProp("randomSeed", Datum.of(777));

        assertEquals(777, properties.getMovieProp("randomSeed").toInt());
        assertEquals(first, player.getVM().callHandler("random", List.of(Datum.of(1000))).toInt());
        assertEquals(second, player.getVM().callHandler("random", List.of(Datum.of(1000))).toInt());
    }

    private record FakeXtra(String name) implements Xtra {
        @Override
        public String getName() {
            return name;
        }

        @Override
        public int createInstance(List<Datum> args) {
            return 1;
        }

        @Override
        public void destroyInstance(int instanceId) {
        }

        @Override
        public Datum callHandler(int instanceId, String handlerName, List<Datum> args) {
            return Datum.VOID;
        }

        @Override
        public Datum getProperty(int instanceId, String propertyName) {
            return Datum.VOID;
        }

        @Override
        public void setProperty(int instanceId, String propertyName, Datum value) {
        }
    }
}
