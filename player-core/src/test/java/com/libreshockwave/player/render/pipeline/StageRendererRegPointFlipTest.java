package com.libreshockwave.player.render.pipeline;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.format.ChunkType;
import com.libreshockwave.id.ChunkId;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

import static org.junit.jupiter.api.Assertions.assertEquals;

class StageRendererRegPointFlipTest {

    @Test
    void flippedBitmapMirrorsRegistrationPointAroundSpriteBounds() throws Exception {
        StageRenderer renderer = new StageRenderer(newEmptyDirectorFile());
        CastMemberChunk member = new CastMemberChunk(
                null,
                new ChunkId(77),
                MemberType.BITMAP,
                0,
                bitmapSpecificData().length,
                new byte[0],
                bitmapSpecificData(),
                "puppet_hilite_test",
                0,
                6,
                5
        );

        Object normal = invokeScaledRegPoint(renderer, member, 20, 40, false, false);
        Object flipped = invokeScaledRegPoint(renderer, member, 20, 40, true, false);

        assertEquals(6, regX(normal));
        assertEquals(5, regY(normal));
        assertEquals(14, regX(flipped));
        assertEquals(5, regY(flipped));
    }

    private static Object invokeScaledRegPoint(StageRenderer renderer, CastMemberChunk member,
                                               int width, int height,
                                               boolean flipH, boolean flipV) throws Exception {
        Method method = StageRenderer.class.getDeclaredMethod(
                "scaledRegPoint",
                CastMemberChunk.class,
                int.class,
                int.class,
                int.class,
                int.class,
                boolean.class,
                boolean.class
        );
        method.setAccessible(true);
        return method.invoke(renderer, member, width, height, 100, 100, flipH, flipV);
    }

    private static int regX(Object regPoint) throws Exception {
        Method x = regPoint.getClass().getDeclaredMethod("x");
        x.setAccessible(true);
        return (int) x.invoke(regPoint);
    }

    private static int regY(Object regPoint) throws Exception {
        Method y = regPoint.getClass().getDeclaredMethod("y");
        y.setAccessible(true);
        return (int) y.invoke(regPoint);
    }

    private static byte[] bitmapSpecificData() {
        ByteBuffer buffer = ByteBuffer.allocate(28).order(ByteOrder.BIG_ENDIAN);
        buffer.putShort((short) 0x8001); // rawPitch with color flag
        buffer.putShort((short) 0);      // top
        buffer.putShort((short) 0);      // left
        buffer.putShort((short) 40);     // bottom
        buffer.putShort((short) 20);     // right
        buffer.putLong(0L);              // skipped D6+ fields
        buffer.putShort((short) 5);      // regY
        buffer.putShort((short) 6);      // regX
        buffer.put((byte) 0);            // updateFlags
        buffer.put((byte) 8);            // bitsPerPixel
        buffer.putShort((short) 0);      // paletteCastLib
        buffer.putShort((short) 1);      // clutId (stored as +1)
        return buffer.array();
    }

    private static DirectorFile newEmptyDirectorFile() throws Exception {
        Constructor<DirectorFile> ctor = DirectorFile.class.getDeclaredConstructor(
                ByteOrder.class, boolean.class, int.class, ChunkType.class);
        ctor.setAccessible(true);
        return ctor.newInstance(ByteOrder.BIG_ENDIAN, false, 0, ChunkType.RIFX);
    }
}
