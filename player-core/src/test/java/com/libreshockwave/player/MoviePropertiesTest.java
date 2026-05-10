package com.libreshockwave.player;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.xtra.Xtra;
import com.libreshockwave.vm.xtra.XtraManager;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
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
