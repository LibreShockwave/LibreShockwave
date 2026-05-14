package com.libreshockwave.player.wasm;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class WasmJpegDecoderTest {
    @Test
    void queuesEmbeddedJpegUntilBrowserDeliversDecodedRgba() {
        WasmJpegDecoder.resetForTest();
        byte[] jpegBytes = new byte[] {(byte) 0xFF, (byte) 0xD8, 1, 2, 3};

        DirectorFile.clearJpegDecodePending();
        Bitmap first = WasmJpegDecoder.decode(jpegBytes);

        assertNull(first);
        assertEquals(1, WasmJpegDecoder.pendingCount());
        assertTrue(DirectorFile.consumeJpegDecodePending());

        int id = WasmJpegDecoder.pendingId(0);
        assertEquals(jpegBytes.length, WasmJpegDecoder.prepareData(id));
        WasmJpegDecoder.deliverDecoded(id, 1, 1, new byte[] {10, 20, 30, (byte) 255});

        Bitmap decoded = WasmJpegDecoder.decode(jpegBytes);

        assertNotNull(decoded);
        assertEquals(0, WasmJpegDecoder.pendingCount());
        assertEquals(0xFF0A141E, decoded.getPixel(0, 0));
    }

    @Test
    void pendingFlagSurvivesUnrelatedDecodeAttemptsUntilConsumed() {
        WasmJpegDecoder.resetForTest();
        DirectorFile.clearJpegDecodePending();

        WasmJpegDecoder.decode(new byte[] {(byte) 0xFF, (byte) 0xD8, 6, 7, 8});
        DirectorFile.clearJpegDecodePending();
        WasmJpegDecoder.decode(new byte[] {(byte) 0xFF, (byte) 0xD8, 9, 10, 11});

        assertTrue(DirectorFile.consumeJpegDecodePending());
        assertFalse(DirectorFile.consumeJpegDecodePending());
    }

    @Test
    void failedBrowserDecodeClearsPendingEntry() {
        WasmJpegDecoder.resetForTest();
        DirectorFile.clearJpegDecodePending();

        WasmJpegDecoder.decode(new byte[] {(byte) 0xFF, (byte) 0xD8, 12, 13, 14});
        assertTrue(DirectorFile.consumeJpegDecodePending());

        int id = WasmJpegDecoder.pendingId(0);
        WasmJpegDecoder.deliverDecoded(id, 0, 0, new byte[0]);

        assertEquals(0, WasmJpegDecoder.pendingCount());
        assertFalse(DirectorFile.consumeJpegDecodePending());
    }
}
