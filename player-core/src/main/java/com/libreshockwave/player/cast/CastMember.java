package com.libreshockwave.player.cast;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.cast.BitmapInfo;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.chunks.KeyTableChunk;
import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.chunks.TextChunk;
import com.libreshockwave.format.ChunkType;
import com.libreshockwave.id.CastLibId;
import com.libreshockwave.id.MemberId;
import com.libreshockwave.player.render.RenderConfig;
import com.libreshockwave.player.render.output.TextRenderer;
import com.libreshockwave.vm.datum.Datum;

import java.util.Arrays;
import java.util.function.BiConsumer;
import java.util.function.BiFunction;

/**
 * Represents a loaded cast member with lazy loading of media data.
 * Similar to dirplayer-rs player/cast_member.rs.
 *
 * Cast members contain the member definition (CastMemberChunk) and
 * optionally load their media data (bitmap pixels, text, etc.) on demand.
 */
public class CastMember {

    /** Platform-specific text renderer. Set by Player on startup. */
    private static TextRenderer textRenderer;

    private static Runnable memberVisualChangedCallback;
    private static BiConsumer<Integer, Integer> memberSlotRetiredCallback;
    private static BiFunction<Integer, Integer, com.libreshockwave.bitmap.Palette> paletteResolver;
    private static BiFunction<Integer, Integer, CastMember> memberResolver;

    public static void setTextRenderer(TextRenderer renderer) {
        textRenderer = renderer;
    }

    /** Static accessor for text renderer (used by SpriteBaker for file-based text). */
    public static TextRenderer getTextRendererStatic() {
        return textRenderer;
    }

    public static void setMemberVisualChangedCallback(Runnable callback) {
        memberVisualChangedCallback = callback;
    }

    public static void setMemberSlotRetiredCallback(BiConsumer<Integer, Integer> callback) {
        memberSlotRetiredCallback = callback;
    }

    public static void setPaletteResolver(BiFunction<Integer, Integer, com.libreshockwave.bitmap.Palette> resolver) {
        paletteResolver = resolver;
    }

    public static void setMemberResolver(BiFunction<Integer, Integer, CastMember> resolver) {
        memberResolver = resolver;
    }

    public enum State {
        NONE,
        LOADING,
        LOADED
    }

    private final CastLibId castLibId;
    private final MemberId memberId;
    private final CastMemberChunk chunk;
    private final DirectorFile sourceFile;

    private State state = State.NONE;

    // Loaded media data (lazy)
    private Bitmap bitmap;
    private ScriptChunk script;
    private String textContent;
    private com.libreshockwave.bitmap.Palette dynamicPalette;

    // Cached properties
    private String name;
    private MemberType memberType;
    private int regPointX;
    private int regPointY;
    private int bitmapAlphaThreshold = 0;

    // Dynamic text content (for dynamically created field/text members)
    private String dynamicText;

    // Runtime palette override (for palette swap animation)
    // Stores the castLib and memberNum of the palette cast member to use instead of the embedded one.
    private int paletteRefCastLib = -1;
    private int paletteRefMemberNum = -1;
    private int paletteVersion = 0; // Incremented on each paletteRef change
    private int lastDecodedPaletteVersion = 0; // Tracks which paletteVersion the bitmap was decoded with

    // Text rendering properties (set by Lingo scripts via member.font, member.fontSize, etc.)
    private String textFont = "Arial";
    private int textFontSize = 12;
    private String textFontStyle = "plain";
    private String textAlignment = "left";
    private int textColor = 0xFF000000; // ARGB black
    private int textBgColor = 0xFFFFFFFF; // ARGB white
    private boolean textWordWrap = false;
    private boolean textAntialias = false;
    private int textBoxType = 0; // 0 = adjust to fit, 1 = fixed
    private int textRectLeft = 0;
    private int textRectTop = 0;
    private int textRectRight = 480;
    private int textRectBottom = 480;
    private int textFixedLineSpace = 0;
    private int textTopSpacing = 0;
    private boolean textImageDirty = true; // Re-render when properties change
    private Bitmap textRenderedImage; // Cached rendered text image
    private boolean editable = false; // Whether this field/text member accepts keyboard input

    public CastMember(int castLibNumber, int memberNumber, CastMemberChunk chunk, DirectorFile sourceFile) {
        this.castLibId = new CastLibId(castLibNumber);
        this.memberId = new MemberId(memberNumber);
        this.chunk = chunk;
        this.sourceFile = sourceFile;

        // Copy basic properties from chunk
        this.name = chunk.name() != null ? chunk.name() : "";
        this.memberType = chunk.memberType();
        this.regPointX = chunk.regPointX();
        this.regPointY = chunk.regPointY();

        // Clear name for text members that have no STXT data (empty placeholder slots).
        // This prevents the Resource Manager from indexing them, avoiding false-positive
        // memberExists() results (e.g. Habbo .props members with no property data).
        // Clear name for text members with empty content (placeholder slots).
        // This prevents the Resource Manager from indexing them, avoiding
        // false-positive memberExists() results for .props members.
        if ((memberType == MemberType.TEXT || memberType == MemberType.BUTTON)
                && !this.name.isEmpty() && sourceFile != null) {
            String earlyText = loadTextEagerly();
            if (earlyText != null && earlyText.isEmpty()) {
                this.name = "";
            }
        }

        // Fallback: parse regPoint from BitmapInfo for members that may have bypassed CastMemberChunk.read()
        if (regPointX == 0 && regPointY == 0 && chunk.isBitmap()
                && chunk.specificData() != null && chunk.specificData().length >= 22) {
            BitmapInfo bi = BitmapInfo.parse(chunk.specificData());
            regPointX = bi.regX();
            regPointY = bi.regY();
            bitmapAlphaThreshold = bi.alphaThreshold();
        } else if (chunk != null && chunk.isBitmap()
                && chunk.specificData() != null && chunk.specificData().length >= 10) {
            bitmapAlphaThreshold = BitmapInfo.parse(chunk.specificData()).alphaThreshold();
        }
    }

    /**
     * Constructor for dynamically created members (via new(#type, castLib)).
     */
    public CastMember(int castLibNumber, int memberNumber, MemberType memberType) {
        this.castLibId = new CastLibId(castLibNumber);
        this.memberId = new MemberId(memberNumber);
        this.chunk = null;
        this.sourceFile = null;
        this.name = "";
        this.memberType = memberType;
        this.state = State.LOADED; // Dynamic members are immediately ready
    }

    /**
     * Load media data for this member.
     * For bitmaps, this loads the pixel data.
     * For scripts, this loads the script bytecode.
     */
    public void load() {
        if (state == State.LOADED) {
            return;
        }

        state = State.LOADING;

        if (chunk == null || sourceFile == null) {
            state = State.LOADED;
            return;
        }

        // Load type-specific data
        switch (memberType) {
            case BITMAP -> loadBitmap();
            case SCRIPT -> loadScript();
            case TEXT, BUTTON -> loadText();
            case XTRA -> {
                // Director 7+ "Text Asset" Xtras store text in XMED chunks
                if (chunk.isTextXtra()) {
                    loadXmedText();
                }
            }
            default -> {}
        }

        state = State.LOADED;
    }

    /**
     * Re-decode the bitmap using the paletteRef palette override.
     * Called when Lingo sets member.paletteRef and then accesses member.image.
     * This is how Director's avatar system remaps greyscale body parts to clothing colors.
     */
    private void redecodeBitmapWithPaletteRef() {
        if (sourceFile == null || chunk == null) return;

        try {
            com.libreshockwave.bitmap.Palette palette = null;

            if (paletteResolver != null && paletteRefCastLib >= 1 && paletteRefMemberNum >= 1) {
                palette = paletteResolver.apply(paletteRefCastLib, paletteRefMemberNum);
            }

            // Fallback for same-file palettes when no cross-cast resolver is installed.
            if (palette == null) {
                palette = sourceFile.resolvePaletteByMemberNumber(paletteRefMemberNum);
            }

            if (palette != null) {
                sourceFile.decodeBitmap(chunk, palette).ifPresent(b -> bitmap = b);
                lastDecodedPaletteVersion = paletteVersion;
            }
        } catch (Exception e) {
            // Fall back to the existing bitmap on error
        }
    }

    private com.libreshockwave.bitmap.Palette resolvePaletteDatum(Datum value) {
        if (value instanceof Datum.CastMemberRef cmr) {
            if (paletteResolver != null) {
                com.libreshockwave.bitmap.Palette resolved =
                        paletteResolver.apply(cmr.castLibNum(), cmr.memberNum());
                if (resolved != null) {
                    return resolved;
                }
            }
            if (sourceFile != null) {
                return sourceFile.resolvePaletteByMemberNumber(cmr.memberNum());
            }
        }
        if (value instanceof Datum.Symbol sym) {
            String name = sym.name().toLowerCase();
            if ("systemmac".equals(name)) {
                return com.libreshockwave.bitmap.Palette.SYSTEM_MAC_PALETTE;
            }
            if ("systemwin".equals(name) || "systemwindows".equals(name)) {
                return com.libreshockwave.bitmap.Palette.SYSTEM_WIN_PALETTE;
            }
        }
        return null;
    }

    private boolean applyRuntimePaletteOverride(Datum value) {
        int newCastLib = -1;
        int newMemberNum = -1;
        if (value instanceof Datum.CastMemberRef cmr) {
            newCastLib = cmr.castLibNum();
            newMemberNum = cmr.memberNum();
        } else if (!(value instanceof Datum.Symbol)) {
            return false;
        }

        com.libreshockwave.bitmap.Palette palette = resolvePaletteDatum(value);
        if (palette == null) {
            return false;
        }

        boolean changed = newCastLib != paletteRefCastLib || newMemberNum != paletteRefMemberNum;
        paletteRefCastLib = newCastLib;
        paletteRefMemberNum = newMemberNum;

        if (bitmap != null && (sourceFile == null || chunk == null || bitmap.isScriptModified())) {
            bitmap.remapImagePalette(palette);
        }

        if (changed) {
            paletteVersion++;
            notifyMemberVisualChanged();
        }
        return true;
    }

    private void loadBitmap() {
        if (sourceFile == null || chunk == null) {
            return;
        }

        // Use DirectorFile's decodeBitmap method which handles all bitmap types
        try {
            sourceFile.decodeBitmap(chunk).ifPresent(b -> bitmap = b);
        } catch (Exception e) {
            System.err.println("[CastMember] Failed to decode bitmap: " + e.getMessage());
        }
    }

    private void loadScript() {
        if (sourceFile == null || chunk.scriptId() <= 0) {
            return;
        }

        script = sourceFile.getScriptByContextId(chunk.scriptId());
    }

    /**
     * Read text content eagerly (during construction) without full member load.
     * Returns the text if an STXT chunk was found, or null if no text data exists.
     */
    private String loadTextEagerly() {
        if (sourceFile == null || chunk == null) return "";
        KeyTableChunk keyTable = sourceFile.getKeyTable();
        if (keyTable != null) {
            var entry = keyTable.findEntry(chunk.id(), ChunkType.STXT.getFourCC());
            if (entry != null) {
                var textChunk = sourceFile.getChunk(entry.sectionId(), TextChunk.class);
                if (textChunk.isPresent()) {
                    return textChunk.get().text();
                }
            }
        }
        var textChunk = sourceFile.getChunk(chunk.id(), TextChunk.class);
        return textChunk.map(TextChunk::text).orElse("");
    }

    private void loadText() {
        if (sourceFile == null || chunk == null) {
            textContent = "";
            return;
        }

        // Text content is stored in an STXT chunk associated with this member.
        // Use the KeyTableChunk to find the associated STXT chunk.
        KeyTableChunk keyTable = sourceFile.getKeyTable();
        if (keyTable != null) {
            // The fourcc for STXT in the key table
            int stxtFourcc = ChunkType.STXT.getFourCC();
            var entry = keyTable.findEntry(chunk.id(), stxtFourcc);
            if (entry != null) {
                var textChunk = sourceFile.getChunk(entry.sectionId(), TextChunk.class);
                if (textChunk.isPresent()) {
                    textContent = textChunk.get().text();
                    return;
                }
            }
        }

        // Fallback: Try to find a text chunk with the same ID as the member
        var textChunk = sourceFile.getChunk(chunk.id(), TextChunk.class);
        if (textChunk.isPresent()) {
            textContent = textChunk.get().text();
        } else {
                textContent = "";
        }
    }

    private void loadXmedText() {
        if (sourceFile == null || chunk == null) {
            textContent = "";
            return;
        }

        var styledText = sourceFile.getXmedStyledTextForMember(chunk);
        if (styledText != null && styledText.text() != null) {
            textContent = styledText.text();
            // Apply font info from XmedStyledText
            if (styledText.fontName() != null) {
                textFont = styledText.fontName();
            }
            textFontSize = styledText.fontSize();
        } else {
            textContent = "";
        }
    }

    /**
     * Check if this member has Lingo-set dynamic text (member.text = value).
     */
    public boolean hasDynamicText() {
        return dynamicText != null;
    }

    /**
     * Get the text content for text/field members.
     * Returns dynamicText if set (via member.text = value), otherwise the loaded text content.
     */
    public String getTextContent() {
        if (dynamicText != null) {
            return dynamicText;
        }
        if (!isLoaded()) {
            load();

            // Normalize line endings to \n (convert \r\n and \r to \n)
            if (textContent != null && !textContent.isEmpty()) {
                textContent = textContent.replace("\r\n", "\r").replace("\n", "\r");
            }
        }
        return textContent != null ? textContent : "";
    }

    // Accessors

    public CastLibId getCastLibId() {
        return castLibId;
    }

    public int getCastLibNumber() {
        return castLibId.value();
    }

    public MemberId getMemberId() {
        return memberId;
    }

    public int getMemberNumber() {
        return memberId.value();
    }

    public CastMemberChunk getChunk() {
        return chunk;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        updateName(name);
    }

    private void updateName(String newName) {
        String oldName = this.name != null ? this.name : "";
        String nextName = newName != null ? newName : "";
        this.name = nextName;
        if (isRuntimeDynamicMember() && !oldName.isEmpty() && nextName.isEmpty()) {
            notifyMemberSlotRetired();
        }
    }

    private boolean isRuntimeDynamicMember() {
        return chunk == null && sourceFile == null;
    }

    public boolean isReusableDynamicSlot() {
        return isRuntimeDynamicMember()
                && memberType == MemberType.NULL
                && (name == null || name.isEmpty());
    }

    public void reuseAs(MemberType newType) {
        resetRuntimePayload();
        memberType = newType != null ? newType : MemberType.NULL;
        state = State.LOADED;
    }

    private void notifyMemberSlotRetired() {
        if (memberSlotRetiredCallback != null) {
            memberSlotRetiredCallback.accept(castLibId.value(), memberId.value());
        }
    }

    private void notifyMemberVisualChanged() {
        if (memberVisualChangedCallback != null) {
            memberVisualChangedCallback.run();
        }
    }

    public MemberType getMemberType() {
        return memberType;
    }

    public State getState() {
        return state;
    }

    public boolean isEditable() {
        return editable;
    }

    /**
     * Set the dynamic text content (used by built-in keyboard input for editable fields).
     */
    public void setDynamicText(String text) {
        this.dynamicText = text;
        this.textImageDirty = true;
    }

    public TextRenderer getTextRenderer() { return textRenderer; }
    public String getTextFont() { return textFont; }
    public int getTextFontSize() { return textFontSize; }
    public String getTextFontStyle() { return textFontStyle; }
    public int getTextFixedLineSpace() { return textFixedLineSpace; }

    /**
     * Convert a local pixel coordinate to a character index in the text.
     * Used for mouse click → caret placement.
     */
    public int locToCharPos(int localX, int localY, int fieldWidth) {
        if (textRenderer == null) return 0;
        String text = getTextContent();
        if (text == null || text.isEmpty()) return 0;
        return textRenderer.locToCharPos(text, localX, localY,
                textFont, textFontSize, textFontStyle, textFixedLineSpace,
                textAlignment, fieldWidth);
    }

    public String getTextAlignment() { return textAlignment; }
    public int getTextBgColor() { return textBgColor; }

    public boolean isLoaded() {
        return state == State.LOADED;
    }

    public Bitmap getBitmap() {
        if (!isLoaded()) {
            load();
        }
        // If paletteRef has been set and the bitmap hasn't been re-decoded yet,
        // re-decode with the override palette.
        if (hasPaletteOverride() && bitmap != null && !bitmap.isScriptModified()
                && paletteVersion > lastDecodedPaletteVersion) {
            redecodeBitmapWithPaletteRef();
        }
        return bitmap;
    }

    /** Set bitmap directly (for initial load, not Lingo assignment). Does NOT mark as script-modified. */
    public void setBitmapDirectly(Bitmap bmp) {
        this.bitmap = bmp;
    }

    public ScriptChunk getScript() {
        if (!isLoaded()) {
            load();
        }
        return script;
    }

    public int getRegPointX() {
        return regPointX;
    }

    public int getRegPointY() {
        return regPointY;
    }

    public int getBitmapAlphaThreshold() {
        return bitmapAlphaThreshold;
    }

    public void setBitmapAlphaThreshold(int alphaThreshold) {
        this.bitmapAlphaThreshold = Math.max(0, Math.min(255, alphaThreshold));
    }

    public void setRegPoint(int x, int y) {
        this.regPointX = x;
        this.regPointY = y;
    }

    /** Returns true if this member has a runtime palette override (from paletteRef). */
    public boolean hasPaletteOverride() {
        return paletteRefCastLib >= 1 && paletteRefMemberNum >= 1;
    }

    public int getPaletteRefCastLib() { return paletteRefCastLib; }
    public int getPaletteRefMemberNum() { return paletteRefMemberNum; }
    public int getPaletteVersion() { return paletteVersion; }

    /**
     * Get a property value for this member.
     */
    public Datum getProp(String propName) {
        String prop = propName.toLowerCase();

        // Common properties for all member types
        return switch (prop) {
            case "name" -> Datum.of(name);
            case "number" -> Datum.of(getSlotNumber());
            case "membernum" -> Datum.of(memberId.value());
            case "type" -> Datum.symbol(memberType == MemberType.NULL ? "empty" : memberType.getName());
            case "castlibnum" -> Datum.of(castLibId.value());
            case "castlib" -> Datum.CastLibRef.of(castLibId.value());
            case "media" -> Datum.CastMemberRef.of(castLibId.value(), memberId.value());
            case "mediaready" -> Datum.of(1); // Always ready for now
            default -> getTypeProp(prop);
        };
    }

    /**
     * Get a type-specific property.
     */
    private Datum getTypeProp(String prop) {
        // XTRA "text" sub-type members behave like TEXT for property access
        if (memberType == MemberType.XTRA && chunk != null && chunk.isTextXtra()) {
            return getTextProp(prop);
        }
        return switch (memberType) {
            case BITMAP -> getBitmapProp(prop);
            case TEXT, BUTTON -> getTextProp(prop);
            case SCRIPT -> getScriptProp(prop);
            case SHAPE -> getShapeProp(prop);
            case PALETTE -> getPaletteProp(prop);
            default -> Datum.VOID;
        };
    }

    /**
     * Get a property of a PALETTE member.
     * Director palette members expose a "color" property that returns
     * a list of color values that can be indexed: member("pal").color[42]
     */
    private Datum getPaletteProp(String prop) {
        if ("color".equals(prop)) {
            com.libreshockwave.bitmap.Palette pal = getPaletteData();
            if (pal != null) {
                // Build a list of Color datums for each palette entry
                java.util.List<Datum> colors = new java.util.ArrayList<>(pal.size());
                for (int i = 0; i < pal.size(); i++) {
                    int rgb = pal.getColor(i);
                    colors.add(new Datum.Color((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
                }
                return new Datum.List(colors);
            }
            return Datum.VOID;
        }
        return Datum.VOID;
    }

    public com.libreshockwave.bitmap.Palette getPaletteData() {
        if (dynamicPalette != null) {
            return dynamicPalette;
        }
        if (sourceFile != null && memberType == MemberType.PALETTE) {
            return sourceFile.resolvePaletteByMemberNumber(memberId.value());
        }
        return null;
    }

    public void setPaletteData(com.libreshockwave.bitmap.Palette palette) {
        this.dynamicPalette = palette;
    }

    private Datum getBitmapProp(String prop) {
        // Ensure bitmap is loaded
        Bitmap bmp = getBitmap();

        return switch (prop) {
            case "width" -> Datum.of(bmp != null ? bmp.getWidth() : 0);
            case "height" -> Datum.of(bmp != null ? bmp.getHeight() : 0);
            case "depth" -> Datum.of(bmp != null ? bmp.getBitDepth() : 0);
            case "alphathreshold" -> Datum.of(bitmapAlphaThreshold);
            case "regpoint" -> new Datum.Point(regPointX, regPointY);
            case "paletteref" -> {
                // Return the palette override if set, otherwise the embedded palette reference
                if (paletteRefCastLib >= 1 && paletteRefMemberNum >= 1) {
                    yield Datum.CastMemberRef.of(paletteRefCastLib, paletteRefMemberNum);
                }
                // Default: derive from BitmapInfo embedded palette ID
                if (chunk != null && chunk.specificData() != null && chunk.specificData().length >= 10) {
                    BitmapInfo info = BitmapInfo.parse(chunk.specificData());
                    int palMemberNum = info.paletteId() + 1;
                    if (palMemberNum >= 1) {
                        yield Datum.CastMemberRef.of(castLibId.value(), palMemberNum);
                    }
                }
                yield Datum.VOID;
            }
            case "palette" -> getBitmapProp("paletteref");
            case "rect" -> {
                int w = bmp != null ? bmp.getWidth() : 0;
                int h = bmp != null ? bmp.getHeight() : 0;
                yield new Datum.Rect(0, 0, w, h);
            }
            case "image" -> {
                if (bmp == null && chunk == null) {
                    // Dynamic bitmap member - create a default 1x1 bitmap
                    bitmap = new Bitmap(1, 1, 32);
                    bitmap.fill(0xFFFFFFFF);
                }
                // Return a live ImageRef that always resolves to this member's current bitmap.
                // This is critical: Lingo like pImg = member.image must stay in sync even after
                // member.image = newImage replaces the member's bitmap (e.g., cloud initCloud).
                yield new Datum.ImageRef(this::getBitmap);
            }
            default -> Datum.VOID;
        };
    }

    private Datum getTextProp(String prop) {
        return switch (prop) {
            case "text" -> Datum.of(getTextContent());
            case "width" -> Datum.of(textRectRight - textRectLeft);
            case "height" -> {
                // Director auto-expands text member height for boxType=adjust.
                // Must render eagerly so height reflects actual text content.
                if (textBoxType == 0) {
                    Bitmap rendered = renderTextToImage();
                    if (rendered != null) {
                        yield Datum.of(rendered.getHeight());
                    }
                }
                yield Datum.of(textRectBottom - textRectTop);
            }
            case "rect" -> {
                // With boxType=adjust (0), rect auto-expands to fit rendered text content.
                // Director auto-expands immediately when text is set, so we must
                // render eagerly here (not wait for .image access).
                if (textBoxType == 0) {
                    Bitmap rendered = renderTextToImage();
                    if (rendered != null) {
                        int renderedHeight = rendered.getHeight();
                        int rectHeight = textRectBottom - textRectTop;
                        if (renderedHeight > rectHeight) {
                            yield new Datum.Rect(textRectLeft, textRectTop,
                                    textRectRight, textRectTop + renderedHeight);
                        }
                    }
                }
                yield new Datum.Rect(textRectLeft, textRectTop, textRectRight, textRectBottom);
            }
            case "image" -> {
                // Render text content to a bitmap image
                Bitmap img = renderTextToImage();
                if (img != null) {
                    yield new Datum.ImageRef(img);
                }
                yield Datum.VOID;
            }
            case "font" -> Datum.of(textFont);
            case "fontsize" -> Datum.of(textFontSize);
            case "fontstyle" -> Datum.of(textFontStyle);
            case "alignment" -> Datum.symbol(textAlignment);
            case "color" -> new Datum.Color(
                    (textColor >> 16) & 0xFF,
                    (textColor >> 8) & 0xFF,
                    textColor & 0xFF);
            case "bgcolor" -> new Datum.Color(
                    (textBgColor >> 16) & 0xFF,
                    (textBgColor >> 8) & 0xFF,
                    textBgColor & 0xFF);
            case "wordwrap" -> Datum.of(textWordWrap ? 1 : 0);
            case "antialias" -> Datum.of(textAntialias ? 1 : 0);
            case "boxtype" -> Datum.of(textBoxType);
            case "fixedlinespace" -> Datum.of(textFixedLineSpace);
            case "topspacing" -> Datum.of(textTopSpacing);
            case "editable" -> Datum.of(editable ? 1 : 0);
            case "charposttoloc" -> Datum.VOID; // handled as method, not property
            default -> Datum.VOID;
        };
    }

    /**
     * Render the text content of this member to a Bitmap.
     * Delegates to the platform-specific TextRenderer.
     * This implements Director's text member .image property.
     */
    public Bitmap renderTextToImage() {
        int width = textRectRight - textRectLeft;
        // For boxType=adjust (0), let the renderer auto-size the height to fit
        // the text content. Director auto-adjusts the rect height when text is set,
        // so the stored rectBottom may not reflect the actual content height.
        int height = textBoxType == 0 ? 0 : (textRectBottom - textRectTop);
        // Text members in Director use Background Transparent behavior when the
        // background is white (default): white pixels become transparent so text
        // can be composited over other content without a white rectangle.
        // Use alpha=0 with white RGB (0x00FFFFFF) so COPY ink skips background
        // pixels while #color/#bgColor remapping still sees white for colorization.
        // Non-white backgrounds (e.g., black bg for white text) must stay opaque.
        int bgColor = (textBgColor == 0xFFFFFFFF) ? 0x00FFFFFF : textBgColor;
        return renderTextToImage(width, height, bgColor);
    }

    /**
     * Render the text content to a Bitmap with explicit dimensions and background color.
     * Used by SpriteBaker for dynamic text where sprite dimensions and ink mode
     * determine the size and background instead of the member's own rect.
     */
    public Bitmap renderTextToImage(int width, int height, int bgColor) {
        // Return cached image if still valid and dimensions match
        if (textRenderedImage != null && !textImageDirty
                && textRenderedImage.getWidth() == width
                && textRenderedImage.getHeight() == height) {
            return textRenderedImage;
        }

        if (textRenderer == null) {
            return null;
        }

        String text = getTextContent();

        textRenderedImage = textRenderer.renderText(text, width, height,
                textFont, textFontSize, textFontStyle,
                textAlignment, textColor, bgColor,
                textWordWrap, textAntialias,
                textFixedLineSpace, textTopSpacing);
        textImageDirty = false;

        return textRenderedImage;
    }

    private Datum getScriptProp(String prop) {
        ScriptChunk s = getScript();
        return switch (prop) {
            case "text" -> Datum.EMPTY_STRING; // Script source not typically available
            case "scripttype" -> {
                if (s != null && s.getScriptType() != null) {
                    yield Datum.of(s.getScriptType().name().toLowerCase());
                }
                yield Datum.VOID;
            }
            default -> Datum.VOID;
        };
    }

    private Datum getShapeProp(String prop) {
        // Shape properties from specificData
        return switch (prop) {
            case "width", "height" -> Datum.of(0); // TODO: parse from specificData
            default -> Datum.VOID;
        };
    }

    /**
     * Set a property value for this member.
     */
    public boolean setProp(String propName, Datum value) {
        String prop = propName.toLowerCase();

        switch (prop) {
            case "name" -> {
                updateName(value.toStr());
                return true;
            }
            case "media" -> {
                return setMediaProp(value);
            }
            case "regpoint" -> {
                if (value instanceof Datum.Point p) {
                    this.regPointX = p.x();
                    this.regPointY = p.y();
                    return true;
                }
                return false;
            }
            default -> {
                return setTypeProp(prop, value);
            }
        }
    }

    private boolean setMediaProp(Datum value) {
        if (value instanceof Datum.CastMemberRef ref) {
            if (memberResolver == null) {
                return false;
            }
            CastMember source = memberResolver.apply(ref.castLibNum(), ref.memberNum());
            if (source == null) {
                return false;
            }
            return copyMediaFrom(source);
        }
        if (memberType == MemberType.BITMAP && value instanceof Datum.ImageRef ir) {
            Bitmap newBmp = ir.bitmap().copy();
            newBmp.markScriptModified();
            this.bitmap = newBmp;
            this.state = State.LOADED;
            return true;
        }
        if ((memberType == MemberType.TEXT || memberType == MemberType.BUTTON)
                && (value instanceof Datum.Str || value instanceof Datum.Symbol)) {
            this.dynamicText = value.toStr();
            this.textImageDirty = true;
            this.state = State.LOADED;
            return true;
        }
        return false;
    }

    private boolean copyMediaFrom(CastMember source) {
        if (source == null) {
            return false;
        }
        switch (source.getMemberType()) {
            case BITMAP -> {
                Bitmap srcBitmap = source.getBitmap();
                if (srcBitmap == null) {
                    return false;
                }
                Bitmap copy = srcBitmap.copy();
                copy.markScriptModified();
                this.bitmap = copy;
                this.regPointX = source.getRegPointX();
                this.regPointY = source.getRegPointY();
                this.bitmapAlphaThreshold = source.bitmapAlphaThreshold;
                this.paletteRefCastLib = source.paletteRefCastLib;
                this.paletteRefMemberNum = source.paletteRefMemberNum;
                this.paletteVersion = source.paletteVersion;
                this.lastDecodedPaletteVersion = source.lastDecodedPaletteVersion;
                this.state = State.LOADED;
                return true;
            }
            case PALETTE -> {
                com.libreshockwave.bitmap.Palette palette = source.getPaletteData();
                if (palette == null) {
                    return false;
                }
                int[] colors = new int[palette.size()];
                for (int i = 0; i < colors.length; i++) {
                    colors[i] = palette.getColor(i);
                }
                this.dynamicPalette = new com.libreshockwave.bitmap.Palette(
                        Arrays.copyOf(colors, colors.length), palette.getName());
                this.state = State.LOADED;
                return true;
            }
            case TEXT, BUTTON -> {
                this.dynamicText = source.getTextContent();
                this.textContent = source.getTextContent();
                this.textFont = source.textFont;
                this.textFontSize = source.textFontSize;
                this.textFontStyle = source.textFontStyle;
                this.textAlignment = source.textAlignment;
                this.textColor = source.textColor;
                this.textBgColor = source.textBgColor;
                this.textWordWrap = source.textWordWrap;
                this.textAntialias = source.textAntialias;
                this.textBoxType = source.textBoxType;
                this.textRectLeft = source.textRectLeft;
                this.textRectTop = source.textRectTop;
                this.textRectRight = source.textRectRight;
                this.textRectBottom = source.textRectBottom;
                this.textFixedLineSpace = source.textFixedLineSpace;
                this.textTopSpacing = source.textTopSpacing;
                this.editable = source.editable;
                this.textImageDirty = true;
                this.state = State.LOADED;
                return true;
            }
            case SCRIPT -> {
                this.script = source.getScript();
                this.state = State.LOADED;
                return this.script != null;
            }
            default -> {
                return false;
            }
        }
    }

    private boolean setTypeProp(String prop, Datum value) {
        if (memberType == MemberType.TEXT || memberType == MemberType.BUTTON) {
            return setTextProp(prop, value);
        }
        // XTRA "text" sub-type members behave like TEXT for property access
        if (memberType == MemberType.XTRA && chunk != null && chunk.isTextXtra()) {
            return setTextProp(prop, value);
        }
        if (memberType == MemberType.BITMAP) {
            return setBitmapProp(prop, value);
        }
        return false;
    }

    /**
     * Set a property on a text/field member.
     * Handles font, fontSize, alignment, color, rect, text, image, and more.
     */
    private boolean setTextProp(String prop, Datum value) {
        switch (prop) {
            case "text" -> {
                this.dynamicText = value.toStr();
                textImageDirty = true;
                return true;
            }
            case "html" -> {
                // Strip HTML tags for now (basic support)
                String html = value.toStr();
                this.dynamicText = html.replaceAll("<[^>]*>", "");
                textImageDirty = true;
                return true;
            }
            case "font" -> {
                this.textFont = value.toStr();
                textImageDirty = true;
                return true;
            }
            case "fontsize" -> {
                this.textFontSize = value.toInt();
                textImageDirty = true;
                return true;
            }
            case "fontstyle" -> {
                if (value instanceof Datum.List list) {
                    // Director fontStyle is a list like [#bold, #italic]
                    StringBuilder sb = new StringBuilder();
                    for (Datum item : list.items()) {
                        if (sb.length() > 0) sb.append(",");
                        sb.append(item.toStr());
                    }
                    this.textFontStyle = sb.toString();
                } else {
                    this.textFontStyle = value.toStr();
                }
                textImageDirty = true;
                return true;
            }
            case "alignment" -> {
                if (value instanceof Datum.Symbol s) {
                    this.textAlignment = s.name().toLowerCase();
                } else {
                    this.textAlignment = value.toStr().toLowerCase();
                }
                textImageDirty = true;
                return true;
            }
            case "color" -> {
                // Director ignores VOID — keeps the current color
                if (!value.isVoid()) {
                    this.textColor = Datum.datumToArgb(value);
                    textImageDirty = true;
                }
                return true;
            }
            case "bgcolor" -> {
                // Director ignores VOID — keeps the current bgColor (default white)
                if (!value.isVoid()) {
                    this.textBgColor = Datum.datumToArgb(value);
                    textImageDirty = true;
                }
                return true;
            }
            case "wordwrap" -> {
                this.textWordWrap = value.toInt() != 0;
                textImageDirty = true;
                return true;
            }
            case "antialias" -> {
                this.textAntialias = value.toInt() != 0;
                textImageDirty = true;
                return true;
            }
            case "boxtype" -> {
                this.textBoxType = value.toInt();
                textImageDirty = true;
                return true;
            }
            case "rect" -> {
                if (value instanceof Datum.Rect r) {
                    this.textRectLeft = r.left();
                    this.textRectTop = r.top();
                    this.textRectRight = r.right();
                    this.textRectBottom = r.bottom();
                    textImageDirty = true;
                    return true;
                }
                return false;
            }
            case "width" -> {
                this.textRectRight = this.textRectLeft + value.toInt();
                textImageDirty = true;
                return true;
            }
            case "height" -> {
                this.textRectBottom = this.textRectTop + value.toInt();
                textImageDirty = true;
                return true;
            }
            case "fixedlinespace" -> {
                this.textFixedLineSpace = value.toInt();
                textImageDirty = true;
                return true;
            }
            case "topspacing" -> {
                this.textTopSpacing = value.toInt();
                textImageDirty = true;
                return true;
            }
            case "editable" -> {
                this.editable = value.toInt() != 0;
                return true;
            }
            case "image" -> {
                // Director copies the bitmap into the member (not a reference assignment).
                if (value instanceof Datum.ImageRef ir) {
                    this.bitmap = ir.bitmap().copy();
                    this.textRenderedImage = this.bitmap;
                    this.textImageDirty = false;
                    Bitmap nb = this.bitmap;
                    if (nb.getWidth() > 50 || nb.getHeight() > 50) {
                        System.out.println("[TXT-IMG] cl=" + castLibId.value() + " cm=" + getMemberNumber()
                                + " " + nb.getWidth() + "x" + nb.getHeight());
                    }
                    return true;
                }
                return false;
            }
            default -> {
                return false;
            }
        }
    }

    private boolean setBitmapProp(String prop, Datum value) {
        return switch (prop) {
            case "paletteref", "palette" -> applyRuntimePaletteOverride(value);
            case "image" -> {
                if (value instanceof Datum.ImageRef ir) {
                    Bitmap newBmp = ir.bitmap().copy();
                    newBmp.markScriptModified();
                    this.bitmap = newBmp;
                    yield true;
                }
                yield false;
            }
            case "width" -> {
                // Create a new bitmap with the given width (preserving height)
                int newW = value.toInt();
                int h = bitmap != null ? bitmap.getHeight() : 1;
                if (newW > 0) {
                    this.bitmap = new Bitmap(newW, h, bitmap != null ? bitmap.getBitDepth() : 32);
                    this.bitmap.fill(0xFFFFFFFF);
                    this.bitmap.markScriptModified();
                }
                yield true;
            }
            case "alphathreshold" -> {
                setBitmapAlphaThreshold(value.toInt());
                yield true;
            }
            case "height" -> {
                int w = bitmap != null ? bitmap.getWidth() : 1;
                int newH = value.toInt();
                if (newH > 0) {
                    this.bitmap = new Bitmap(w, newH, bitmap != null ? bitmap.getBitDepth() : 32);
                    this.bitmap.fill(0xFFFFFFFF);
                    this.bitmap.markScriptModified();
                }
                yield true;
            }
            default -> false;
        };
    }

    /**
     * Get the combined slot number (castLib << 16 | memberId).
     */
    public int getSlotNumber() {
        return (castLibId.value() << 16) | (memberId.value() & 0xFFFF);
    }

    /**
     * Erase runtime data from this member while keeping the slot alive.
     * Director uses this for dynamic non-bitmap members that are deleted from
     * app-level registries but still occupy a cast slot.
     */
    public void erase() {
        boolean retiredDynamicSlot = isRuntimeDynamicMember();
        resetRuntimePayload();
        memberType = MemberType.NULL;
        state = State.LOADED;

        if (retiredDynamicSlot) {
            notifyMemberSlotRetired();
        }
        notifyMemberVisualChanged();
    }

    private void resetRuntimePayload() {
        name = "";
        bitmap = null;
        script = null;
        textContent = "";
        dynamicText = null;
        dynamicPalette = null;
        regPointX = 0;
        regPointY = 0;
        bitmapAlphaThreshold = 0;
        editable = false;

        textFont = "Arial";
        textFontSize = 12;
        textFontStyle = "plain";
        textAlignment = "left";
        textColor = 0xFF000000;
        textBgColor = 0xFFFFFFFF;
        textWordWrap = false;
        textAntialias = false;
        textBoxType = 0;
        textRectLeft = 0;
        textRectTop = 0;
        textRectRight = 480;
        textRectBottom = 480;
        textFixedLineSpace = 0;
        textTopSpacing = 0;
        textImageDirty = true;
        textRenderedImage = null;

        paletteRefCastLib = -1;
        paletteRefMemberNum = -1;
        paletteVersion++;
        lastDecodedPaletteVersion = 0;
    }

    /**
     * Call a method on this cast member.
     * Handles Director member methods like charPosToLoc.
     */
    public Datum callMethod(String methodName, java.util.List<Datum> args) {
        String method = methodName.toLowerCase();
        return switch (method) {
            case "getprop" -> {
                // Director: member.getProp(#propName, index)
                // Returns a sub-element of a compound property (e.g., Point, Rect)
                if (args.size() < 2) yield Datum.VOID;
                String propSymbol = args.get(0) instanceof Datum.Symbol sym
                        ? sym.name() : args.get(0).toStr();
                int index = args.get(1).toInt();
                Datum propValue = getProp(propSymbol.toLowerCase());
                if (propValue instanceof Datum.Point p) {
                    yield Datum.of(index == 1 ? p.x() : p.y());
                } else if (propValue instanceof Datum.Rect r) {
                    yield switch (index) {
                        case 1 -> Datum.of(r.left());
                        case 2 -> Datum.of(r.top());
                        case 3 -> Datum.of(r.right());
                        case 4 -> Datum.of(r.bottom());
                        default -> Datum.VOID;
                    };
                }
                yield propValue;
            }
            case "charposttoloc", "charpostoloc" -> {
                // charPosToLoc(charIndex) → point(x, y)
                if (args.isEmpty()) yield new Datum.Point(0, 0);
                int charIndex = args.get(0).toInt();

                String text = getTextContent();
                if (text == null || text.isEmpty() || charIndex <= 0) {
                    yield new Datum.Point(0, 0);
                }

                if (textRenderer == null) {
                    yield new Datum.Point(0, 0);
                }

                int[] pos = textRenderer.charPosToLoc(text, charIndex,
                        textFont, textFontSize, textFontStyle, textFixedLineSpace,
                        textAlignment, 0);
                yield new Datum.Point(pos[0], pos[1]);
            }
            case "count" -> {
                // member.count(#char) / member.count(#word) / member.count(#line) / member.count(#item)
                // Returns the number of chunks of the specified type in the member's text content.
                // Compiled from Lingo expressions like: pTextMem.char.count
                // NOTE: Uses traditional instanceof+cast instead of pattern matching
                // because TeaVM WASM doesn't support Java 16+ instanceof patterns.
                if (args.isEmpty()) yield Datum.ZERO;
                Datum arg0 = args.get(0);
                String chunkType;
                if (arg0 instanceof Datum.Symbol) {
                    chunkType = ((Datum.Symbol) arg0).name().toLowerCase();
                } else {
                    chunkType = arg0.toStr().toLowerCase();
                }
                String text = getTextContent();
                if (text == null || text.isEmpty()) yield Datum.ZERO;
                if ("char".equals(chunkType)) {
                    yield Datum.of(text.length());
                } else if ("word".equals(chunkType)) {
                    String trimmed = text.trim();
                    yield trimmed.isEmpty() ? Datum.ZERO : Datum.of(trimmed.split("\\s+").length);
                } else if ("line".equals(chunkType)) {
                    yield Datum.of(text.split("[\r\n]", -1).length);
                } else if ("item".equals(chunkType)) {
                    yield Datum.of(text.split(",", -1).length);
                } else {
                    yield Datum.ZERO;
                }
            }
            case "erase" -> {
                erase();
                yield Datum.of(1);
            }
            default -> Datum.VOID;
        };
    }

    @Override
    public String toString() {
        return "CastMember{castLib=" + castLibId.value() + ", member=" + memberId.value() +
               ", name='" + name + "', type=" + memberType + "}";
    }
}
