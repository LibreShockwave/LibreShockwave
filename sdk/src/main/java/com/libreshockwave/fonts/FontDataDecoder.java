package com.libreshockwave.fonts;

import java.util.Base64;
import java.util.zip.DataFormatException;
import java.util.zip.Inflater;

/**
 * Shared decoder for generated embedded font classes.
 */
public final class FontDataDecoder {
    private FontDataDecoder() {
    }

    public static byte[] decode(String[] chunks, int compressedLength, int uncompressedLength) {
        byte[] compressed = decodeBase64Chunks(chunks, compressedLength);
        byte[] data = inflate(compressed, uncompressedLength);
        return data.length == uncompressedLength ? data : new byte[0];
    }

    private static byte[] decodeBase64Chunks(String[] chunks, int compressedLength) {
        Base64.Decoder decoder = Base64.getDecoder();
        byte[] compressed = new byte[compressedLength];
        int pos = 0;
        for (String chunk : chunks) {
            byte[] decoded = decoder.decode(chunk);
            System.arraycopy(decoded, 0, compressed, pos, decoded.length);
            pos += decoded.length;
        }
        return compressed;
    }

    private static byte[] inflate(byte[] compressed, int uncompressedLength) {
        Inflater inflater = new Inflater();
        byte[] output = new byte[uncompressedLength];
        int outputPos = 0;

        try {
            inflater.setInput(compressed);
            int zeroCount = 0;
            while (!inflater.finished() && outputPos < output.length) {
                int count = inflater.inflate(output, outputPos, output.length - outputPos);
                if (count == 0) {
                    if (inflater.needsInput() || ++zeroCount > 3) {
                        break;
                    }
                    continue;
                }
                outputPos += count;
                zeroCount = 0;
            }
        } catch (DataFormatException e) {
            return new byte[0];
        } finally {
            inflater.end();
        }

        if (outputPos == output.length) {
            return output;
        }
        byte[] partial = new byte[outputPos];
        System.arraycopy(output, 0, partial, 0, outputPos);
        return partial;
    }
}
