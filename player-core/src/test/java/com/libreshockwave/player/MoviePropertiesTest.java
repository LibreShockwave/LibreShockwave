package com.libreshockwave.player;

import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

class MoviePropertiesTest {

    @Test
    void activeWindowForMainMovieIsStage() {
        MovieProperties properties = new MovieProperties(null, null);

        assertEquals(Datum.STAGE, properties.getMovieProp("activeWindow"));
    }
}
