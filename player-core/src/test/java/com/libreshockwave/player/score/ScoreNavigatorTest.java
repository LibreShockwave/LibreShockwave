package com.libreshockwave.player.score;

import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

class ScoreNavigatorTest {

    @Test
    void parsesSavedBehaviorPropertyListFromScoreEntryPointer() {
        List<byte[]> entries = List.of(
                new byte[0],
                new byte[0],
                "[#turnpoint: 332]\0\0\0".getBytes(StandardCharsets.ISO_8859_1)
        );

        List<Datum> parameters = ScoreNavigator.parseBehaviorParameters(entries, 2);

        assertEquals(1, parameters.size());
        Datum.PropList props = assertInstanceOf(Datum.PropList.class, parameters.get(0));
        assertEquals(332, props.get("turnpoint", true).toInt());
    }

    @Test
    void parsesQuotedSavedBehaviorPropertyValues() {
        List<byte[]> entries = List.of(
                new byte[0],
                "[#loginpw: \"password_field\", #isLoginField: 0]\0".getBytes(StandardCharsets.ISO_8859_1)
        );

        List<Datum> parameters = ScoreNavigator.parseBehaviorParameters(entries, 1);

        Datum.PropList props = assertInstanceOf(Datum.PropList.class, parameters.get(0));
        Datum.Str loginPassword = assertInstanceOf(Datum.Str.class, props.get("loginpw", true));
        assertEquals("password_field", loginPassword.value());
        assertEquals(0, props.get("isLoginField", true).toInt());
    }

    @Test
    void ignoresMissingOrNonPropertyParameterEntries() {
        assertTrue(ScoreNavigator.parseBehaviorParameters(List.of(new byte[0]), 1).isEmpty());
        assertTrue(ScoreNavigator.parseBehaviorParameters(
                List.of(new byte[0], "not a list".getBytes(StandardCharsets.ISO_8859_1)), 1).isEmpty());
    }
}
