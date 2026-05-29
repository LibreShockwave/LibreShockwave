package com.libreshockwave.player.render.pipeline;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.chunks.ConfigChunk;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.format.ChunkType;
import com.libreshockwave.id.ChunkId;
import com.libreshockwave.player.sprite.SpriteState;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

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

    @Test
    void directorHorizontalMirrorIsTreatedAsEffectiveFlipForRegPointMath() throws Exception {
        StageRenderer renderer = new StageRenderer(newEmptyDirectorFile());
        SpriteState state = new SpriteState(7);
        state.setRotation(180);
        state.setSkew(180);

        Method method = StageRenderer.class.getDeclaredMethod("effectiveFlipH", SpriteState.class);
        method.setAccessible(true);

        assertTrue((boolean) method.invoke(renderer, state));
    }

    @Test
    void legacyDirectorMoviesRoundStretchedBitmapRegistrationScale() throws Exception {
        StageRenderer renderer = new StageRenderer(newDirectorFileWithVersion(1600));
        CastMemberChunk member = bitmapMember(rendererFile(renderer), bitmapSpecificData(214, 2, 0, 1));

        Object scaled = invokeScaledRegPoint(renderer, member, 214, 67, false, false);

        assertEquals(34, regY(scaled));
    }

    @Test
    void modernDirectorMoviesTruncateStretchedBitmapRegistrationScale() throws Exception {
        StageRenderer renderer = new StageRenderer(newDirectorFileWithVersion(1858));
        CastMemberChunk member = bitmapMember(rendererFile(renderer), bitmapSpecificData(214, 2, 0, 1));

        Object scaled = invokeScaledRegPoint(renderer, member, 214, 67, false, false);

        assertEquals(33, regY(scaled));
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
        return bitmapSpecificData(20, 40, 6, 5);
    }

    private static byte[] bitmapSpecificData(int width, int height, int regX, int regY) {
        ByteBuffer buffer = ByteBuffer.allocate(28).order(ByteOrder.BIG_ENDIAN);
        buffer.putShort((short) 0x8001); // rawPitch with color flag
        buffer.putShort((short) 0);      // top
        buffer.putShort((short) 0);      // left
        buffer.putShort((short) height); // bottom
        buffer.putShort((short) width);  // right
        buffer.putLong(0L);              // skipped D6+ fields
        buffer.putShort((short) regY);   // regY
        buffer.putShort((short) regX);   // regX
        buffer.put((byte) 0);            // updateFlags
        buffer.put((byte) 8);            // bitsPerPixel
        buffer.putShort((short) 0);      // paletteCastLib
        buffer.putShort((short) 1);      // clutId (stored as +1)
        return buffer.array();
    }

    private static CastMemberChunk bitmapMember(DirectorFile file, byte[] specificData) {
        return new CastMemberChunk(
                file,
                new ChunkId(77),
                MemberType.BITMAP,
                0,
                specificData.length,
                new byte[0],
                specificData,
                "window_middle",
                0,
                0,
                1
        );
    }

    private static DirectorFile newEmptyDirectorFile() throws Exception {
        Constructor<DirectorFile> ctor = DirectorFile.class.getDeclaredConstructor(
                ByteOrder.class, boolean.class, int.class, ChunkType.class);
        ctor.setAccessible(true);
        return ctor.newInstance(ByteOrder.BIG_ENDIAN, false, 0, ChunkType.RIFX);
    }

    private static DirectorFile newDirectorFileWithVersion(int directorVersion) throws Exception {
        DirectorFile file = newEmptyDirectorFile();
        ConfigChunk config = new ConfigChunk(file, new ChunkId(1), directorVersion,
                0, 0, 540, 720, 1, 1, 12, 0, 0, 0,
                0, 0, 0, 0, 0, 0, (short) 0);
        Field configField = DirectorFile.class.getDeclaredField("config");
        configField.setAccessible(true);
        configField.set(file, config);
        return file;
    }

    private static DirectorFile rendererFile(StageRenderer renderer) throws Exception {
        Field fileField = StageRenderer.class.getDeclaredField("file");
        fileField.setAccessible(true);
        return (DirectorFile) fileField.get(renderer);
    }
}
