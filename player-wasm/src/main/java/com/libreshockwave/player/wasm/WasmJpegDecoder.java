package com.libreshockwave.player.wasm;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;

import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.zip.CRC32;

final class WasmJpegDecoder {
    private static final Map<Integer, byte[]> pending = new LinkedHashMap<>();
    private static final Map<Integer, Bitmap> decoded = new LinkedHashMap<>();
    private static byte[] currentData;

    private WasmJpegDecoder() {
    }

    static Bitmap decode(byte[] jpegData) {
        int id = idFor(jpegData);
        Bitmap bitmap = decoded.get(id);
        if (bitmap != null) {
            return bitmap;
        }
        pending.putIfAbsent(id, Arrays.copyOf(jpegData, jpegData.length));
        DirectorFile.markJpegDecodePending();
        return null;
    }

    static int pendingCount() {
        return pending.size();
    }

    static int pendingId(int index) {
        int i = 0;
        for (Integer id : pending.keySet()) {
            if (i++ == index) {
                return id;
            }
        }
        return 0;
    }

    static int prepareData(int id) {
        currentData = pending.get(id);
        return currentData != null ? currentData.length : 0;
    }

    static byte[] currentData() {
        return currentData;
    }

    static void reset() {
        pending.clear();
        decoded.clear();
        currentData = null;
    }

    static void resetForTest() {
        reset();
    }

    static void deliverDecoded(int id, int width, int height, byte[] rgba) {
        if (width <= 0 || height <= 0 || rgba == null || rgba.length < width * height * 4) {
            pending.remove(id);
            return;
        }
        Bitmap bitmap = new Bitmap(width, height, 32);
        int p = 0;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int r = rgba[p++] & 0xFF;
                int g = rgba[p++] & 0xFF;
                int b = rgba[p++] & 0xFF;
                int a = rgba[p++] & 0xFF;
                bitmap.setPixel(x, y, (a << 24) | (r << 16) | (g << 8) | b);
            }
        }
        bitmap.setNativeAlpha(true);
        decoded.put(id, bitmap);
        pending.remove(id);
        currentData = null;
    }

    private static int idFor(byte[] data) {
        CRC32 crc = new CRC32();
        crc.update(data, 0, data.length);
        return (int) crc.getValue();
    }
}
