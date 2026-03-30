package com.libreshockwave.player.render.output;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.player.cast.FontRegistry;
import com.libreshockwave.player.render.RenderConfig;

import java.awt.*;
import java.awt.font.TextAttribute;
import java.awt.image.BufferedImage;
import java.io.ByteArrayInputStream;
import java.util.HashMap;
import java.util.List;
import java.util.ArrayList;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * AWT-based text renderer for desktop environments.
 * Uses Graphics2D for proper text layout, font resolution, and anti-aliased rendering.
 */
public class AwtTextRenderer implements TextRenderer {

    /** Cache of available system font names (lowercase -> actual name) */
    private static Map<String, String> systemFontCache;

    /** Cache of AWT fonts created from PFR1-derived TTF data */
    private static final ConcurrentHashMap<String, Font> pfrAwtFontCache = new ConcurrentHashMap<>();

    @Override
    public Bitmap renderText(String text, int width, int height,
                             String fontName, int fontSize, String fontStyle,
                             String alignment, int textColor, int bgColor,
                             boolean wordWrap, boolean antialias,
                             int fixedLineSpace, int topSpacing) {
        if (text == null) text = "";
        if (width <= 0) width = 200;
        if (height <= 0) height = 20;

        // Create a temporary BufferedImage to render text with AWT
        BufferedImage bufImg = new BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB);
        Graphics2D g2d = bufImg.createGraphics();

        // Set rendering hints — use global config, allow per-member override
        boolean aa = antialias || RenderConfig.isAntialias();
        if (aa) {
            g2d.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING,
                    RenderingHints.VALUE_TEXT_ANTIALIAS_ON);
            g2d.setRenderingHint(RenderingHints.KEY_RENDERING, RenderingHints.VALUE_RENDER_QUALITY);
        } else {
            // Explicitly disable AA to match Director's bitmap-style text rendering
            g2d.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING,
                    RenderingHints.VALUE_TEXT_ANTIALIAS_OFF);
        }

        // Fill background
        g2d.setColor(new Color(bgColor, true));
        g2d.fillRect(0, 0, width, height);

        // Set font — try PFR-derived TTF first, then system fonts
        int fontStyleAwt = Font.PLAIN;
        String style = fontStyle.toLowerCase();
        if (style.contains("bold")) fontStyleAwt |= Font.BOLD;
        if (style.contains("italic")) fontStyleAwt |= Font.ITALIC;
        Font font = resolvePfrAwtFont(fontName, fontStyleAwt, fontSize);
        if (font == null) {
            font = resolveDirectorFont(fontName, fontStyleAwt, fontSize);
        }
        if (style.contains("underline")) {
            Map<TextAttribute, Object> attrs = new HashMap<>();
            attrs.put(TextAttribute.UNDERLINE, TextAttribute.UNDERLINE_ON);
            font = font.deriveFont(attrs);
        }
        g2d.setFont(font);

        // Set text color
        g2d.setColor(new Color(textColor, true));

        FontMetrics fm = g2d.getFontMetrics();
        int lineHeight = fixedLineSpace > 0 ? fixedLineSpace : fm.getHeight();
        int ascent = fm.getAscent();

        // Split text into lines
        String[] rawLines = TextRenderer.splitLines(text);

        // Word wrap if enabled
        List<String> lines = new ArrayList<>();
        if (wordWrap) {
            for (String rawLine : rawLines) {
                TextRenderer.wrapLine(rawLine, fm::stringWidth, width, lines);
            }
        } else {
            for (String rawLine : rawLines) {
                lines.add(rawLine);
            }
        }

        // In Director, topSpacing adds leading above EACH line (per-line leading),
        // so the effective line advance = fixedLineSpace + topSpacing.
        int lineAdvance = lineHeight + topSpacing;
        int neededHeight = lines.size() * lineAdvance;
        if (neededHeight > height) {
            g2d.dispose();
            bufImg = new BufferedImage(width, neededHeight, BufferedImage.TYPE_INT_ARGB);
            g2d = bufImg.createGraphics();
            if (aa) {
                g2d.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING,
                        RenderingHints.VALUE_TEXT_ANTIALIAS_ON);
            }
            g2d.setColor(new Color(bgColor, true));
            g2d.fillRect(0, 0, width, neededHeight);
            g2d.setFont(font);
            g2d.setColor(new Color(textColor, true));
            height = neededHeight;
        }

        // Draw lines
        int y = ascent + topSpacing;
        for (String line : lines) {
            if (y > height) break;

            int x = 0;
            switch (alignment) {
                case "center" -> x = (width - fm.stringWidth(line)) / 2;
                case "right" -> x = width - fm.stringWidth(line);
                default -> x = 0; // left
            }

            g2d.drawString(line, x, y);
            y += lineAdvance;
        }

        g2d.dispose();

        // Convert BufferedImage to our Bitmap format
        int[] pixels = bufImg.getRGB(0, 0, bufImg.getWidth(), bufImg.getHeight(),
                null, 0, bufImg.getWidth());
        return new Bitmap(bufImg.getWidth(), bufImg.getHeight(), 32, pixels);
    }

    @Override
    public int[] charPosToLoc(String text, int charIndex,
                              String fontName, int fontSize, String fontStyle,
                              int fixedLineSpace, String alignment, int fieldWidth) {
        int fontStyleAwt = Font.PLAIN;
        String style = fontStyle != null ? fontStyle.toLowerCase() : "";
        if (style.contains("bold")) fontStyleAwt |= Font.BOLD;
        if (style.contains("italic")) fontStyleAwt |= Font.ITALIC;
        Font font = resolvePfrAwtFont(fontName, fontStyleAwt, fontSize);
        if (font == null) {
            font = resolveDirectorFont(fontName, fontStyleAwt, fontSize);
        }

        BufferedImage tmpImg = new BufferedImage(1, 1, BufferedImage.TYPE_INT_ARGB);
        Graphics2D g2d = tmpImg.createGraphics();
        g2d.setFont(font);
        FontMetrics fm = g2d.getFontMetrics();
        int lineHeight = fixedLineSpace > 0 ? fixedLineSpace : fm.getHeight();

        if (text == null || text.isEmpty() || charIndex <= 0) {
            int alignX = alignmentOffset(alignment, fieldWidth, text == null || text.isEmpty() ? 0 :
                    fm.stringWidth(TextRenderer.splitLines(text)[0]));
            g2d.dispose();
            return new int[]{alignX, 0};
        }

        int[] lineInfo = TextRenderer.findCharLine(text, charIndex);
        int lineNum = lineInfo[0];
        int charsOnLine = lineInfo[1];

        String[] lines = TextRenderer.splitLines(text);
        String fullLine = (lineNum < lines.length) ? lines[lineNum] : "";
        String lineSubstr = (lineNum < lines.length) ? fullLine.substring(0, charsOnLine) : "";
        int x = fm.stringWidth(lineSubstr);
        int alignX = alignmentOffset(alignment, fieldWidth, fm.stringWidth(fullLine));
        int y = lineNum * lineHeight;

        g2d.dispose();
        return new int[]{x + alignX, y};
    }

    @Override
    public int getLineHeight(String fontName, int fontSize, String fontStyle,
                             int fixedLineSpace) {
        if (fixedLineSpace > 0) return fixedLineSpace;
        int fontStyleAwt = Font.PLAIN;
        String style = fontStyle != null ? fontStyle.toLowerCase() : "";
        if (style.contains("bold")) fontStyleAwt |= Font.BOLD;
        if (style.contains("italic")) fontStyleAwt |= Font.ITALIC;
        Font font = resolvePfrAwtFont(fontName, fontStyleAwt, fontSize);
        if (font == null) font = resolveDirectorFont(fontName, fontStyleAwt, fontSize);
        BufferedImage tmpImg = new BufferedImage(1, 1, BufferedImage.TYPE_INT_ARGB);
        Graphics2D g2d = tmpImg.createGraphics();
        g2d.setFont(font);
        int h = g2d.getFontMetrics().getHeight();
        g2d.dispose();
        return h;
    }

    @Override
    public int locToCharPos(String text, int x, int y,
                            String fontName, int fontSize, String fontStyle,
                            int fixedLineSpace, String alignment, int fieldWidth) {
        if (text == null || text.isEmpty()) return 0;

        int fontStyleAwt = Font.PLAIN;
        String style = fontStyle != null ? fontStyle.toLowerCase() : "";
        if (style.contains("bold")) fontStyleAwt |= Font.BOLD;
        if (style.contains("italic")) fontStyleAwt |= Font.ITALIC;
        Font font = resolvePfrAwtFont(fontName, fontStyleAwt, fontSize);
        if (font == null) font = resolveDirectorFont(fontName, fontStyleAwt, fontSize);

        BufferedImage tmpImg = new BufferedImage(1, 1, BufferedImage.TYPE_INT_ARGB);
        Graphics2D g2d = tmpImg.createGraphics();
        g2d.setFont(font);
        FontMetrics fm = g2d.getFontMetrics();

        int lineHeight = fixedLineSpace > 0 ? fixedLineSpace : fm.getHeight();
        String[] lines = TextRenderer.splitLines(text);
        int lineIndex = Math.max(0, Math.min(y / Math.max(1, lineHeight), lines.length - 1));

        int charsBefore = TextRenderer.lineStartIndex(text, lineIndex);

        String line = lines[lineIndex];
        int alignX = alignmentOffset(alignment, fieldWidth, fm.stringWidth(line));
        int localX = x - alignX;
        int charOnLine = line.length();
        for (int i = 0; i < line.length(); i++) {
            int cx = fm.stringWidth(line.substring(0, i + 1));
            if (cx - fm.charWidth(line.charAt(i)) / 2 >= localX) {
                charOnLine = i;
                break;
            }
        }

        g2d.dispose();
        return charsBefore + charOnLine;
    }

    private static int alignmentOffset(String alignment, int fieldWidth, int lineWidth) {
        if (alignment == null || fieldWidth <= 0) return 0;
        return switch (alignment) {
            case "center" -> (fieldWidth - lineWidth) / 2;
            case "right" -> fieldWidth - lineWidth;
            default -> 0;
        };
    }

    // --- PFR TTF Font Resolution ---

    /**
     * Try to create an AWT Font from PFR1-derived TTF data.
     * Returns null if no PFR font is registered for this name.
     */
    private static Font resolvePfrAwtFont(String fontName, int style, int size) {
        if (fontName == null) return null;

        String key = fontName.toLowerCase() + ":" + style;
        Font cached = pfrAwtFontCache.get(key);
        if (cached != null) {
            return cached.deriveFont((float) size);
        }

        byte[] ttfBytes = FontRegistry.getTtfBytes(fontName);
        if (ttfBytes == null) {
            // Try canonical/fuzzy match
            String resolved = FontRegistry.resolveFont(fontName);
            if (resolved != null) {
                ttfBytes = FontRegistry.getTtfBytes(resolved);
            }
            if (ttfBytes == null) return null;
        }

        try {
            Font baseFont = Font.createFont(Font.TRUETYPE_FONT, new ByteArrayInputStream(ttfBytes));
            if (style != Font.PLAIN) {
                baseFont = baseFont.deriveFont(style);
            }
            pfrAwtFontCache.put(key, baseFont);
            return baseFont.deriveFont((float) size);
        } catch (Exception e) {
            return null;
        }
    }

    // --- Font resolution (extracted from CastMember) ---

    private static Font resolveDirectorFont(String directorName, int style, int size) {
        if (directorName == null || directorName.isEmpty()) {
            return new Font("SansSerif", style, size);
        }

        String resolved = resolveDirectorFontName(directorName);

        int extraStyle = extractFontStyle(directorName);
        if (extraStyle != Font.PLAIN) {
            style |= extraStyle;
        }

        return new Font(resolved, style, size);
    }

    private static Map<String, String> getSystemFontCache() {
        if (systemFontCache == null) {
            synchronized (AwtTextRenderer.class) {
                if (systemFontCache == null) {
                    Map<String, String> cache = new HashMap<>();
                    for (String fontName : GraphicsEnvironment.getLocalGraphicsEnvironment()
                            .getAvailableFontFamilyNames()) {
                        cache.put(fontName.toLowerCase(), fontName);
                    }
                    systemFontCache = cache;
                }
            }
        }
        return systemFontCache;
    }

    /** Classic Mac → Windows font mapping for Director movies.
     * Director's Shockwave plugin on Windows mapped Mac fonts to these equivalents. */
    private static final java.util.Map<String, String> MAC_FONT_MAP = java.util.Map.of(
            "geneva", "SansSerif",
            "chicago", "Arial",
            "monaco", "Courier New",
            "new york", "Times New Roman",
            "charcoal", "Tahoma"
    );

    private static String resolveDirectorFontName(String name) {
        var cache = getSystemFontCache();

        // 0. Mac → Windows font mapping
        String mapped = MAC_FONT_MAP.get(name.toLowerCase());
        if (mapped != null) {
            String exact = cache.get(mapped.toLowerCase());
            if (exact != null) return exact;
        }

        // 1. Exact match (case-insensitive)
        String exact = cache.get(name.toLowerCase());
        if (exact != null) return exact;

        // 2. Strip trailing style indicators (b=bold, i=italic, bi=bold-italic)
        String baseName = name.replaceAll("(?i)[bi]+$", "");
        if (!baseName.isEmpty() && !baseName.equals(name)) {
            exact = cache.get(baseName.toLowerCase());
            if (exact != null) return exact;
        }

        // 3. Try prefix matching for very short names
        if (name.length() <= 3) {
            String prefix = baseName.isEmpty() ? name.toLowerCase() : baseName.toLowerCase();
            String[] commonFonts = {
                "Verdana", "Arial", "Tahoma", "Times New Roman", "Courier New",
                "Georgia", "Helvetica", "Trebuchet MS", "Comic Sans MS"
            };
            for (String candidate : commonFonts) {
                if (candidate.toLowerCase().startsWith(prefix)) {
                    String resolved = cache.get(candidate.toLowerCase());
                    if (resolved != null) return resolved;
                }
            }
            for (var entry : cache.entrySet()) {
                if (entry.getKey().startsWith(prefix)) {
                    return entry.getValue();
                }
            }
        }

        return "SansSerif";
    }

    private static int extractFontStyle(String name) {
        if (name.length() <= 1) return Font.PLAIN;

        String baseName = name.replaceAll("(?i)[bi]+$", "");
        if (baseName.length() == name.length()) return Font.PLAIN;

        String suffix = name.substring(baseName.length()).toLowerCase();
        int style = Font.PLAIN;
        if (suffix.contains("b")) style |= Font.BOLD;
        if (suffix.contains("i")) style |= Font.ITALIC;
        return style;
    }

}
