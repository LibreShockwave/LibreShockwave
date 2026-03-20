package com.libreshockwave.player;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.id.InkMode;
import com.libreshockwave.player.input.InputState;
import com.libreshockwave.player.render.pipeline.RenderSprite;
import com.libreshockwave.player.render.pipeline.StageRenderer;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

class CursorManagerTest {

    @Test
    void suppressesGlobalCursorOnNavigatorWhitespace() {
        InputState inputState = new InputState();
        inputState.setMousePosition(10, 20);

        CursorManager cursorManager = createCursorManager(inputState);

        assertEquals(-1, cursorManager.getCursorAtMouse());
    }

    @Test
    void keepsGlobalCursorOnNavigatorContent() {
        InputState inputState = new InputState();
        inputState.setMousePosition(11, 21);

        CursorManager cursorManager = createCursorManager(inputState);

        assertEquals(5, cursorManager.getCursorAtMouse());
    }

    private static CursorManager createCursorManager(InputState inputState) {
        StageRenderer renderer = new StageRenderer(null);
        renderer.setLastBakedSprites(List.of(createNavigatorSprite()));
        return new CursorManager(
                renderer,
                inputState,
                null,
                null,
                () -> 1,
                () -> null,
                () -> Datum.of(5));
    }

    private static RenderSprite createNavigatorSprite() {
        Bitmap bitmap = new Bitmap(4, 4, 32);
        bitmap.fill(0xFFFFFFFF);
        bitmap.fillRect(1, 1, 2, 2, 0xFF9DA3A6);

        return new RenderSprite(
                75,
                10, 20,
                4, 4,
                0,
                true,
                RenderSprite.SpriteType.BITMAP,
                null,
                null,
                0, 0,
                false, false,
                InkMode.MATTE.code(), 100,
                false, false,
                bitmap,
                true);
    }
}
