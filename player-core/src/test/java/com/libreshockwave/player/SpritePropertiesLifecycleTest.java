package com.libreshockwave.player;

import com.libreshockwave.player.render.SpriteRegistry;
import com.libreshockwave.player.sprite.SpriteState;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class SpritePropertiesLifecycleTest {

    @Test
    void memberZeroClearsDynamicMemberOverride() {
        SpriteRegistry registry = new SpriteRegistry();
        SpriteProperties props = new SpriteProperties(registry);

        assertTrue(props.setSpriteProp(17, "member", Datum.CastMemberRef.of(3, 42)));

        SpriteState state = registry.get(17);
        assertTrue(state.hasDynamicMember());
        assertEquals(3, state.getEffectiveCastLib());
        assertEquals(42, state.getEffectiveCastMember());

        assertTrue(props.setSpriteProp(17, "member", Datum.CastMemberRef.of(1, 0)));

        assertFalse(state.hasDynamicMember());
        assertEquals(0, state.getEffectiveCastLib());
        assertEquals(0, state.getEffectiveCastMember());
    }

    @Test
    void memberZeroResetsReleasedSpriteTransformState() {
        SpriteRegistry registry = new SpriteRegistry();
        SpriteProperties props = new SpriteProperties(registry);

        assertTrue(props.setSpriteProp(17, "member", Datum.CastMemberRef.of(3, 42)));

        SpriteState state = registry.get(17);
        state.setFlipH(true);
        state.setFlipV(true);
        state.setRotation(180.0);
        state.setSkew(180.0);

        assertTrue(props.setSpriteProp(17, "member", Datum.CastMemberRef.of(1, 0)));

        assertFalse(state.isFlipH());
        assertFalse(state.isFlipV());
        assertEquals(0.0, state.getRotation());
        assertEquals(0.0, state.getSkew());
    }

    @Test
    void memberNumZeroClearsDynamicMemberOverride() {
        SpriteRegistry registry = new SpriteRegistry();
        SpriteProperties props = new SpriteProperties(registry);

        assertTrue(props.setSpriteProp(23, "member", Datum.CastMemberRef.of(4, 88)));

        SpriteState state = registry.get(23);
        assertTrue(state.hasDynamicMember());

        assertTrue(props.setSpriteProp(23, "memberNum", Datum.ZERO));

        assertFalse(state.hasDynamicMember());
        assertEquals(0, state.getEffectiveCastMember());
    }

    @Test
    void disablingPuppetOnEmptySpriteClearsLingeringScriptInstances() {
        SpriteRegistry registry = new SpriteRegistry();
        SpriteProperties props = new SpriteProperties(registry);

        SpriteState state = registry.getOrCreateDynamic(23);
        state.setScriptInstanceList(java.util.List.of(new Datum.ScriptInstance(99, new java.util.LinkedHashMap<>())));

        assertTrue(props.setSpriteProp(23, "member", Datum.ZERO));
        assertTrue(props.setSpriteProp(23, "puppet", Datum.ZERO));

        assertTrue(state.getScriptInstanceList().isEmpty());
    }

    @Test
    void clearDynamicMemberBindingsDetachesOnlyMatchingSprites() {
        SpriteRegistry registry = new SpriteRegistry();

        SpriteState floor = registry.getOrCreateDynamic(11);
        floor.setDynamicMember(7, 10001);
        floor.setFlipH(true);
        floor.setRotation(180.0);
        floor.setSkew(180.0);

        SpriteState other = registry.getOrCreateDynamic(12);
        other.setDynamicMember(7, 10002);
        other.setFlipH(true);
        other.setRotation(180.0);
        other.setSkew(180.0);

        assertTrue(registry.clearDynamicMemberBindings(7, 10001));
        assertFalse(floor.hasDynamicMember());
        assertFalse(floor.isFlipH());
        assertEquals(0.0, floor.getRotation());
        assertEquals(0.0, floor.getSkew());
        assertTrue(other.hasDynamicMember());
        assertEquals(7, other.getEffectiveCastLib());
        assertEquals(10002, other.getEffectiveCastMember());
        assertTrue(other.isFlipH());
        assertEquals(180.0, other.getRotation());
        assertEquals(180.0, other.getSkew());
        assertEquals(1, registry.getRevision());
    }
}
