package com.libreshockwave.cast;

import org.junit.jupiter.api.Test;

import java.io.ByteArrayOutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class XmedTextParserTest {

    @Test
    void alignmentValueOneMeansCenter() {
        byte[] xmed = xmedWithAlignmentValue('1');

        XmedStyledText styled = XmedTextParser.parseStyled(xmed, textXtraSpecificData(120, 20));

        assertEquals("center", styled.alignment());
    }

    @Test
    void alignmentValueTwoMeansRight() {
        byte[] xmed = xmedWithAlignmentValue('2');

        XmedStyledText styled = XmedTextParser.parseStyled(xmed, textXtraSpecificData(120, 20));

        assertEquals("right", styled.alignment());
    }

    @Test
    void section0004StyleRunsMarkUnderlineRanges() {
        byte[] xmed = xmedWithStyleRuns();

        XmedStyledText styled = XmedTextParser.parseStyled(xmed, textXtraSpecificData(120, 20));

        assertEquals("ABCD", styled.text());
        assertEquals(2, styled.styledSpans().size());
        assertEquals(0, styled.styledSpans().get(0).startOffset());
        assertEquals(2, styled.styledSpans().get(0).endOffset());
        assertFalse(styled.styledSpans().get(0).underline());
        assertEquals(2, styled.styledSpans().get(1).startOffset());
        assertEquals(4, styled.styledSpans().get(1).endOffset());
        assertTrue(styled.styledSpans().get(1).underline());
    }

    @Test
    void closeFontSizeTiesPreferSmallerConcreteRunSize() {
        byte[] xmed = xmedWithFontSizes("C0000", "C0000", "A0000", "A0000", "90000");

        XmedStyledText styled = XmedTextParser.parseStyled(xmed, textXtraSpecificData(120, 20));

        assertEquals(10, styled.fontSize());
    }

    @Test
    void referencedStyleRunSelectsConcreteFontSize() {
        byte[] xmed = xmedWithReferencedStyleSize();

        XmedStyledText styled = XmedTextParser.parseStyled(xmed, textXtraSpecificData(120, 20));

        assertEquals(9, styled.fontSize());
    }

    @Test
    void wideFontSizeTiesKeepFirstCommonSize() {
        byte[] xmed = xmedWithFontSizes("C0000", "C0000", "90000", "90000");

        XmedStyledText styled = XmedTextParser.parseStyled(xmed, textXtraSpecificData(120, 20));

        assertEquals(12, styled.fontSize());
    }

    @Test
    void corpusMultilineMemberKeepsParagraphStyleMetadataSeparateFromAlignment() throws Exception {
        Path xmed = Path.of("/opt/git/v1_assets/habbo_entry/raw_chunks/03742_member_4209_XMED.bin");
        if (!Files.isRegularFile(xmed)) {
            return;
        }

        XmedStyledText styled = XmedTextParser.parseStyled(Files.readAllBytes(xmed), textXtraSpecificData(220, 40));

        assertEquals("center", styled.alignment());
        assertEquals(1, styled.primaryParagraphStyleIndex());
        assertEquals(2, styled.paragraphStyleCount());
    }

    @Test
    void corpusLeftAlignedMemberUsesStyleIndexZero() throws Exception {
        Path xmed = Path.of("/opt/git/v1_assets/habbo_entry/raw_chunks/03709_member_4163_XMED.bin");
        if (!Files.isRegularFile(xmed)) {
            return;
        }

        XmedStyledText styled = XmedTextParser.parseStyled(Files.readAllBytes(xmed), textXtraSpecificData(160, 20));

        assertEquals("left", styled.alignment());
        assertEquals(0, styled.primaryParagraphStyleIndex());
        assertEquals(2, styled.primaryParagraphAlignmentCode());
        assertEquals(1, styled.paragraphStyleCount());
    }

    private static byte[] xmedWithAlignmentValue(char value) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        writeAscii(out, "0002");
        out.write(0);
        writeAscii(out, "4,Test");
        out.write(3);
        writeAscii(out, "00050000000400000001");
        out.write(2);
        writeAscii(out, "0");
        out.write(1);
        writeAscii(out, Character.toString(value));
        out.write(3);
        return out.toByteArray();
    }

    private static byte[] xmedWithStyleRuns() {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        writeAscii(out, "0002");
        out.write(0);
        writeAscii(out, "4,ABCD");
        out.write(3);
        writeAscii(out, "00040000000C00000003");
        out.write(2);
        writeAscii(out, "0");
        out.write(1);
        writeAscii(out, "4");
        out.write(2);
        writeAscii(out, "2");
        out.write(1);
        writeAscii(out, "5");
        out.write(2);
        writeAscii(out, "4");
        out.write(1);
        writeAscii(out, "4");
        out.write(3);
        return out.toByteArray();
    }

    private static byte[] xmedWithFontSizes(String... sizes) {
        ByteArrayOutputStream body = new ByteArrayOutputStream();
        for (String size : sizes) {
            body.write(2);
            writeAscii(body, size);
            body.write(2);
            writeAscii(body, "0");
        }

        ByteArrayOutputStream out = new ByteArrayOutputStream();
        writeAscii(out, "0002");
        out.write(0);
        writeAscii(out, "4,Test");
        out.write(3);
        writeAscii(out, "0006");
        writeAscii(out, "%08X".formatted(body.size()));
        writeAscii(out, "%08X".formatted(sizes.length));
        out.writeBytes(body.toByteArray());
        out.write(3);
        return out.toByteArray();
    }

    private static byte[] xmedWithReferencedStyleSize() {
        ByteArrayOutputStream body = new ByteArrayOutputStream();
        for (String size : new String[]{"C0000", "C0000", "A0000", "A0000", "90000"}) {
            body.write(2);
            writeAscii(body, size);
            body.write(2);
            writeAscii(body, "0");
        }

        ByteArrayOutputStream out = new ByteArrayOutputStream();
        writeAscii(out, "0002");
        out.write(0);
        writeAscii(out, "12,Name of your Habbo");
        out.write(3);
        writeAscii(out, "00040000000A00000002");
        out.write(2);
        writeAscii(out, "0");
        out.write(1);
        writeAscii(out, "4");
        out.write(2);
        writeAscii(out, "12");
        out.write(1);
        writeAscii(out, "4");
        out.write(3);
        writeAscii(out, "0006");
        writeAscii(out, "%08X".formatted(body.size()));
        writeAscii(out, "%08X".formatted(5));
        out.writeBytes(body.toByteArray());
        out.write(3);
        return out.toByteArray();
    }

    private static byte[] textXtraSpecificData(int width, int height) {
        byte[] data = new byte[56];
        data[4] = 't';
        data[5] = 'e';
        data[6] = 'x';
        data[7] = 't';
        writeU32(data, 48, height);
        writeU32(data, 52, width);
        return data;
    }

    private static void writeU32(byte[] data, int offset, int value) {
        data[offset] = (byte) ((value >>> 24) & 0xFF);
        data[offset + 1] = (byte) ((value >>> 16) & 0xFF);
        data[offset + 2] = (byte) ((value >>> 8) & 0xFF);
        data[offset + 3] = (byte) (value & 0xFF);
    }

    private static void writeAscii(ByteArrayOutputStream out, String value) {
        out.writeBytes(value.getBytes(StandardCharsets.ISO_8859_1));
    }
}
