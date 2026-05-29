package com.libreshockwave.player;

import com.libreshockwave.cast.MemberType;
import com.libreshockwave.id.InkMode;
import com.libreshockwave.player.behavior.BehaviorManager;
import com.libreshockwave.player.cast.CastLib;
import com.libreshockwave.player.cast.CastLibManager;
import com.libreshockwave.player.cast.CastMember;
import com.libreshockwave.player.event.EventDispatcher;
import com.libreshockwave.player.input.InputState;
import com.libreshockwave.player.render.pipeline.RenderSprite;
import com.libreshockwave.player.render.pipeline.StageRenderer;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Field;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;

class InputHandlerTest {

    @Test
    void mouseDownFocusesEditableTextSpriteWithoutMouseHandlers() throws Exception {
        StageRenderer renderer = new StageRenderer(null);
        CastLibManager castLibManager = castLibManagerWithInternalCast();
        CastMember field = castLibManager.getCastLib(1).createDynamicMember("text");
        field.setProp("editable", Datum.of(1));
        field.setDynamicText("abc");

        var sprite = renderer.getSpriteRegistry().getOrCreateDynamic(12);
        sprite.setDynamicMember(1, field.getMemberNumber());
        sprite.setLocH(50);
        sprite.setLocV(40);
        sprite.setWidth(80);
        sprite.setHeight(18);

        renderer.setLastBakedSprites(List.of(new RenderSprite(
                12,
                50, 40,
                80, 18,
                0,
                true,
                RenderSprite.SpriteType.TEXT,
                null,
                field,
                0, 0,
                false, false,
                InkMode.COPY.code(), 100,
                false, false,
                null,
                false)));

        InputState inputState = new InputState();
        EventDispatcher dispatcher = new EventDispatcher(
                null, new LingoVM(null), new BehaviorManager(null));
        InputHandler handler = new InputHandler(
                inputState, renderer, castLibManager, () -> 1, () -> dispatcher);

        handler.onMouseDown(55, 45, false);
        handler.processInputEvents();

        assertEquals(12, inputState.getKeyboardFocusSprite());
    }

    @SuppressWarnings("unchecked")
    private static CastLibManager castLibManagerWithInternalCast() throws Exception {
        CastLibManager manager = new CastLibManager(null, null);
        CastLib castLib = new CastLib(1, null, null);
        castLib.load();

        Field field = CastLibManager.class.getDeclaredField("castLibs");
        field.setAccessible(true);
        Map<Integer, CastLib> castLibs = (Map<Integer, CastLib>) field.get(manager);
        castLibs.put(1, castLib);
        return manager;
    }
}
