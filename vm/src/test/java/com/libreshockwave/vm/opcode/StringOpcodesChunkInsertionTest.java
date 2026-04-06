package com.libreshockwave.vm.opcode;

import com.libreshockwave.lingo.StringChunkType;
import org.junit.jupiter.api.Test;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

import static org.junit.jupiter.api.Assertions.assertEquals;

class StringOpcodesChunkInsertionTest {

    @Test
    void puttingAfterCharZeroAppendsToEmptyAndExistingStrings() {
        assertEquals("abc", invokePuttingAfter("", StringChunkType.CHAR, 0, "abc"));
        assertEquals("zabc", invokePuttingAfter("z", StringChunkType.CHAR, 1, "abc"));
    }

    @Test
    void puttingBeforeAndAfterMissingChunksClampToStringEdges() {
        assertEquals("abcz", invokePuttingBefore("abc", StringChunkType.CHAR, 99, "z"));
        assertEquals("zabc", invokePuttingBefore("abc", StringChunkType.CHAR, 1, "z"));
        assertEquals("abcz", invokePuttingAfter("abc", StringChunkType.CHAR, 99, "z"));
    }

    @Test
    void puttingAfterMissingItemFallsBackToAppend() {
        assertEquals("a,b,z", invokePuttingAfter("a,b", StringChunkType.ITEM, 99, ",z"));
    }

    private static String invokePuttingBefore(String str, StringChunkType type, int index, String value) {
        return invokeChunkInsert("stringByPuttingBeforeChunk", str, type, index, value);
    }

    private static String invokePuttingAfter(String str, StringChunkType type, int index, String value) {
        return invokeChunkInsert("stringByPuttingAfterChunk", str, type, index, value);
    }

    private static String invokeChunkInsert(String methodName, String str, StringChunkType type, int index, String value) {
        try {
            Class<?> chunkExprClass = Class.forName("com.libreshockwave.vm.opcode.StringOpcodes$ChunkExpr");
            var constructor = chunkExprClass.getDeclaredConstructors()[0];
            constructor.setAccessible(true);
            Object chunkExpr = constructor.newInstance(type, index, index);

            Method method = StringOpcodes.class.getDeclaredMethod(
                    methodName,
                    String.class,
                    chunkExprClass,
                    String.class,
                    char.class);
            method.setAccessible(true);
            return (String) method.invoke(null, str, chunkExpr, value, ',');
        } catch (NoSuchMethodException | IllegalAccessException | ClassNotFoundException | InstantiationException e) {
            throw new AssertionError(e);
        } catch (InvocationTargetException e) {
            Throwable cause = e.getCause();
            if (cause instanceof RuntimeException runtimeException) {
                throw runtimeException;
            }
            if (cause instanceof Error error) {
                throw error;
            }
            throw new RuntimeException(cause);
        }
    }
}
