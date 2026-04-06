package com.libreshockwave.player.cast;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.chunks.CastChunk;
import com.libreshockwave.chunks.CastListChunk;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.id.SlotId;
import com.libreshockwave.util.FileUtil;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.builtin.cast.CastLibProvider;

import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.BiConsumer;

/**
 * Manages cast libraries for the player.
 * Provides access to cast libraries and their members for Lingo scripts.
 * Cast libraries are lazily loaded when first accessed via castLib().
 */
public class CastLibManager implements CastLibProvider {

    private final DirectorFile file;
    private final Map<Integer, CastLib> castLibs = new LinkedHashMap<>();
    private boolean initialized = false;

    // Callback for cast data loading: when Lingo sets castLib.fileName, this is called
    // with (castLibNumber, fileName). Can load data synchronously (JVM) or queue for
    // async delivery (WASM).
    private final BiConsumer<Integer, String> castDataRequestCallback;

    public CastLibManager(DirectorFile file, BiConsumer<Integer, String> castDataRequestCallback) {
        this.file = file;
        this.castDataRequestCallback = castDataRequestCallback;
    }

    /**
     * Initialize cast library references from the DirectorFile.
     * This creates CastLib objects but doesn't load their members yet.
     * Uses CastListChunk as the primary source for cast library info.
     */
    private void ensureInitialized() {
        if (initialized || file == null) {
            return;
        }

        initialized = true;

        CastListChunk castList = file.getCastList();
        List<CastChunk> casts = file.getCasts();

        String basePath = file.getBasePath();

        if (castList != null && !castList.entries().isEmpty()) {
            // Use CastListChunk entries as the source
            for (int i = 0; i < castList.entries().size(); i++) {
                CastListChunk.CastListEntry listEntry = castList.entries().get(i);

                // Cast lib number is 1-based index
                int castLibNumber = i + 1;

                // Get the corresponding CastChunk if available
                CastChunk castChunk = (i < casts.size()) ? casts.get(i) : null;

                CastLib castLib = new CastLib(castLibNumber, castChunk, listEntry);

                // Set preload mode from cast list entry
                castLib.setPreloadMode(listEntry.preloadSettings());

                // Set base path for external cast loading
                castLib.setBasePath(basePath);

                // For internal casts (no external fileName), set the source file directly
                boolean isExternal = listEntry.path() != null && !listEntry.path().isEmpty();
                if (!isExternal) {
                    castLib.setSourceFile(file);
                }

                castLibs.put(castLibNumber, castLib);
            }
        } else if (!casts.isEmpty()) {
            // Fallback: use CastChunks directly if no cast list
            for (int i = 0; i < casts.size(); i++) {
                int castLibNumber = i + 1;
                CastLib castLib = new CastLib(castLibNumber, casts.get(i), null);
                castLib.setBasePath(basePath);
                castLib.setSourceFile(file);
                castLibs.put(castLibNumber, castLib);
            }
        }
    }

    /**
     * Get a cast library by number, loading it if necessary.
     */
    public CastLib getCastLib(int castLibNumber) {
        ensureInitialized();

        CastLib castLib = castLibs.get(castLibNumber);
        if (castLib != null && !castLib.isLoaded()) {
            castLib.load();
        }
        return castLib;
    }

    /**
     * Get a cast library by name, loading it if necessary.
     */
    public CastLib getCastLibByNameInternal(String name) {
        ensureInitialized();

        for (CastLib castLib : castLibs.values()) {
            if (castLib.getName().equalsIgnoreCase(name)) {
                if (!castLib.isLoaded()) {
                    castLib.load();
                }
                return castLib;
            }
        }

        // Check for "Internal" as default name for cast 1
        if ("internal".equalsIgnoreCase(name)) {
            return getCastLib(1);
        }

        return null;
    }

    @Override
    public int getCastLibByNumber(int castLibNumber) {
        CastLib castLib = getCastLib(castLibNumber);
        return castLib != null ? castLib.getNumber() : -1;
    }

    @Override
    public int getCastLibByName(String name) {
        CastLib castLib = getCastLibByNameInternal(name);
        return castLib != null ? castLib.getNumber() : -1;
    }

    @Override
    public Datum getCastLibProp(int castLibNumber, String propName) {
        CastLib castLib = getCastLib(castLibNumber);
        if (castLib == null) {
            return Datum.VOID;
        }
        return castLib.getProp(propName);
    }

    @Override
    public boolean setCastLibProp(int castLibNumber, String propName, Datum value) {
        CastLib castLib = getCastLib(castLibNumber);
        if (castLib == null) {
            return false;
        }

        boolean result = castLib.setProp(propName, value);

        // When Lingo's CastLoad Manager sets castLib.fileName to a new URL (after downloading
        // the .cct file), we need to reload the cast from the cached downloaded data.
        // In real Director, setting castLib.fileName triggers an automatic reload.
        if (result && "filename".equalsIgnoreCase(propName)) {
            tryLoadCastFromCache(castLibNumber, value.toStr());
        }

        return result;
    }

    private void tryLoadCastFromCache(int castLibNumber, String newFileName) {
        if (newFileName == null || newFileName.isEmpty()) return;

        markPendingExternalLoad(castLibNumber, newFileName);

        // Player provides a callback that checks its internal caches safely
        // before delegating to system-specific async logic.
        if (castDataRequestCallback != null) {
            castDataRequestCallback.accept(castLibNumber, newFileName);
        }
    }

    @Override
    public Datum getMember(int castLibNumber, int memberNumber) {
        CastLib castLib = getCastLib(castLibNumber);
        if (castLib == null) {
            // Return reference anyway - will be invalid
            return Datum.CastMemberRef.of(castLibNumber, memberNumber);
        }

        // Validate member exists
        CastMemberChunk member = castLib.findMemberByNumber(memberNumber);
        if (member == null) {
            // Return reference anyway - member may not exist but reference is valid syntax
            return Datum.CastMemberRef.of(castLibNumber, memberNumber);
        }
        return Datum.CastMemberRef.of(castLibNumber, memberNumber);
    }

    @Override
    public boolean memberExists(int castLibNumber, int memberNumber) {
        CastLib castLib = getCastLib(castLibNumber);
        if (castLib == null) {
            return false;
        }
        return castLib.getMember(memberNumber) != null;
    }

    @Override
    public boolean isRegistryVisibleMember(int castLibNumber, int memberNumber) {
        if (memberNumber <= 0) {
            return false;
        }
        CastLib castLib = getCastLib(castLibNumber);
        if (!isRegistryFallbackEligibleCast(castLib)) {
            return false;
        }
        return castLib.getMember(memberNumber) != null;
    }

    @Override
    public Datum getMemberByName(int castLibNumber, String memberName) {
        ensureInitialized();
        if (castLibNumber > 0) {
            return getMemberByNameInCast(getCastLib(castLibNumber), memberName);
        } else {
            // Prefer the movie's stable/authored namespace first. Runtime-retargeted
            // scratch casts may legitimately contain members with colliding names,
            // but they should not hijack broad member("name") lookups while a
            // stable cast already exposes the same member.
            for (CastLib castLib : castLibs.values()) {
                CastLib loadedCast = getCastLib(castLib.getNumber());
                if (!isRegistryFallbackEligibleCast(loadedCast)) {
                    continue;
                }
                Datum found = getMemberByNameInCast(loadedCast, memberName);
                if (!found.isVoid()) {
                    return found;
                }
            }

            for (CastLib castLib : castLibs.values()) {
                Datum found = getMemberByNameInCast(getCastLib(castLib.getNumber()), memberName);
                if (!found.isVoid()) {
                    return found;
                }
            }
        }

        return Datum.VOID;
    }

    @Override
    public Datum getRegistryMemberByName(int castLibNumber, String memberName) {
        ensureInitialized();
        if (castLibNumber > 0) {
            CastLib castLib = getCastLib(castLibNumber);
            if (!isRegistryFallbackEligibleCast(castLib)) {
                return Datum.VOID;
            }
            return getMemberByNameInCast(castLib, memberName);
        }

        for (CastLib castLib : castLibs.values()) {
            CastLib loadedCast = getCastLib(castLib.getNumber());
            if (!isRegistryFallbackEligibleCast(loadedCast)) {
                continue;
            }
            Datum found = getMemberByNameInCast(loadedCast, memberName);
            if (!found.isVoid()) {
                return found;
            }
        }
        return Datum.VOID;
    }

    @Override
    public int getCastLibCount() {
        ensureInitialized();
        return castLibs.size();
    }

    @Override
    public int getMemberCount(int castLibNumber) {
        CastLib castLib = getCastLib(castLibNumber);
        if (castLib == null) {
            return 0;
        }
        return castLib.getMemberCount();
    }

    @Override
    public Datum getMemberProp(int castLibNumber, int memberNumber, String propName) {
        CastLib castLib = getCastLib(castLibNumber);
        if (castLib == null) {
            // Return defaults for invalid cast lib
            return CastLib.getInvalidMemberProp(propName);
        }
        return castLib.getMemberProp(memberNumber, propName);
    }

    @Override
    public boolean setMemberProp(int castLibNumber, int memberNumber, String propName, Datum value) {
        CastLib castLib = getCastLib(castLibNumber);
        if (castLib == null) {
            return false;
        }
        return castLib.setMemberProp(memberNumber, propName, value);
    }

    @Override
    public Datum callMemberMethod(int castLibNumber, int memberNumber,
                                   String methodName, java.util.List<Datum> args) {
        CastLib castLib = getCastLib(castLibNumber);
        if (castLib == null) {
            return Datum.VOID;
        }
        CastMember member = castLib.getMember(memberNumber);
        if (member == null) {
            return Datum.VOID;
        }
        if ("duplicate".equalsIgnoreCase(methodName) && !args.isEmpty()) {
            Datum targetArg = args.get(0);
            Datum.CastMemberRef targetRef = null;

            if (targetArg instanceof Datum.CastMemberRef cmr) {
                targetRef = cmr;
            } else if (targetArg.isInt() || targetArg.isFloat()) {
                int slotValue = targetArg.toInt();
                SlotId slotId = new SlotId(slotValue);
                if (slotId.castLib() >= 1 && slotId.member() >= 1) {
                    Datum decodedRef = Datum.CastMemberRef.of(slotId.castLib(), slotId.member());
                    if (decodedRef instanceof Datum.CastMemberRef cmr) {
                        targetRef = cmr;
                    }
                } else if (castLibNumber >= 1 && slotValue >= 1) {
                    // Fallback for callers that pass a raw member number instead of member.number.
                    Datum fallbackRef = Datum.CastMemberRef.of(castLibNumber, slotValue);
                    if (fallbackRef instanceof Datum.CastMemberRef cmr) {
                        targetRef = cmr;
                    }
                }
            }

            if (targetRef == null) {
                return member.callMethod(methodName, args);
            }

            CastLib targetCastLib = getCastLib(targetRef.castLibNum());
            if (targetCastLib == null) {
                return Datum.VOID;
            }
            CastMember targetMember = targetCastLib.getMember(targetRef.memberNum());
            if (targetMember == null) {
                return Datum.VOID;
            }
            Palette sourcePalette = member.getPaletteData();
            if (sourcePalette != null) {
                targetMember.setPaletteData(sourcePalette);
                return targetArg;
            }
        }
        return member.callMethod(methodName, args);
    }

    /**
     * Get a property for an invalid member reference.
     */

    /**
     * Get a cast member chunk directly.
     */
    public CastMemberChunk getCastMember(int castLibNumber, int memberNumber) {
        CastLib castLib = getCastLib(castLibNumber);
        if (castLib == null) {
            return null;
        }
        return castLib.findMemberByNumber(memberNumber);
    }

    /**
     * Get the palette from a palette cast member.
     * Searches by member number within a specific cast lib, then all cast libs.
     */
    @Override
    public Palette getMemberPalette(int castLibNumber, int memberNumber) {
        // Try specified cast lib first
        CastLib castLib = getCastLib(castLibNumber);
        if (castLib != null && castLib.isLoaded()) {
            CastMember dynamicMember = castLib.getMember(memberNumber);
            if (dynamicMember != null) {
                Palette dynamicPalette = dynamicMember.getPaletteData();
                if (dynamicPalette != null) {
                    return dynamicPalette;
                }
            }
            CastMemberChunk chunk = castLib.findMemberByNumber(memberNumber);
            if (chunk != null && chunk.file() != null) {
                return chunk.file().resolvePaletteByMemberNumber(memberNumber);
            }
        }
        // Fallback: search all cast libs
        for (CastLib cl : castLibs.values()) {
            if (!cl.isLoaded()) continue;
            CastMember dynamicMember = cl.getMember(memberNumber);
            if (dynamicMember != null) {
                Palette dynamicPalette = dynamicMember.getPaletteData();
                if (dynamicPalette != null) {
                    return dynamicPalette;
                }
            }
            CastMemberChunk chunk = cl.findMemberByNumber(memberNumber);
            if (chunk != null && chunk.file() != null) {
                return chunk.file().resolvePaletteByMemberNumber(memberNumber);
            }
        }
        return null;
    }

    /**
     * Get a dynamic CastMember object (created at runtime via new(#type, castLib)).
     * Used by the renderer when CastMemberChunk lookup fails for dynamically created members.
     */
    public CastMember getDynamicMember(int castLibNumber, int memberNumber) {
        CastLib castLib = getCastLib(castLibNumber);
        if (castLib == null) {
            return null;
        }
        return castLib.getMember(memberNumber);
    }

    /**
     * Resolve any runtime CastMember wrapper by cast/member number.
     * Unlike getDynamicMember(), this also works for file-backed members.
     */
    public CastMember resolveMember(int castLibNumber, int memberNumber) {
        CastLib castLib = getCastLib(castLibNumber);
        if (castLib == null) {
            return null;
        }
        return castLib.getMember(memberNumber);
    }

    /**
     * Find a CastMember object by name across all loaded cast libraries.
     * Searches dynamic members first (runtime-created), then file members wrapped as CastMember.
     * Returns null if not found.
     */
    public CastMember findCastMemberByName(String name) {
        ensureInitialized();

        // Search dynamic members first (runtime-created via new(#type, castLib))
        for (CastLib castLib : castLibs.values()) {
            if (!castLib.isLoaded()) continue;
            CastMember dynamic = castLib.getMemberByName(name);
            if (dynamic != null) {
                return dynamic;
            }
        }

        // Search file members — return from CastLib.getMember() to get a full CastMember
        for (CastLib castLib : castLibs.values()) {
            if (!castLib.isLoaded()) {
                castLib.load();
            }
            CastMemberChunk chunk = castLib.findMemberByName(name);
            if (chunk != null) {
                int memberNumber = castLib.getMemberNumber(chunk);
                CastMember member = castLib.getMember(memberNumber);
                if (member != null) {
                    return member;
                }
            }
        }

        return null;
    }

    /**
     * Get a cast member chunk by name.
     */
    public CastMemberChunk getCastMemberByName(String name) {
        ensureInitialized();

        for (CastLib castLib : castLibs.values()) {
            if (!castLib.isLoaded()) {
                castLib.load();
            }
            CastMemberChunk member = castLib.findMemberByName(name);
            if (member != null) {
                return member;
            }
        }
        return null;
    }

    /**
     * Find the runtime CastMember for a given CastMemberChunk.
     * Searches all cast libraries for the chunk by its chunk ID, then returns the
     * runtime CastMember wrapper. Used by SpriteBaker to get Lingo-set properties
     * for score-placed sprites that only have a CastMemberChunk.
     */
    public CastMember findRuntimeMember(CastMemberChunk target) {
        if (target == null) return null;
        ensureInitialized();

        for (CastLib castLib : castLibs.values()) {
            if (!castLib.isLoaded()) continue;
            int memberNum = castLib.getMemberNumber(target);
            if (memberNum >= 0) {
                return castLib.getMember(memberNum);
            }
        }
        return null;
    }

    /**
     * Get all loaded cast libraries.
     */
    public Map<Integer, CastLib> getCastLibs() {
        ensureInitialized();
        return castLibs;
    }

    @Override
    public String getCastLibName(int castLibNumber) {
        ensureInitialized();
        CastLib castLib = castLibs.get(castLibNumber);
        if (castLib == null) {
            return null;
        }
        return castLib.getName();
    }

    @Override
    public String getCastLibFileName(int castLibNumber) {
        ensureInitialized();
        CastLib castLib = castLibs.get(castLibNumber);
        if (castLib == null) {
            return null;
        }
        return castLib.getFileName();
    }

    @Override
    public boolean fetchCastLib(int castLibNumber) {
        // External cast fetching is now handled by preloadNetThing
        // This method returns true if the cast is already fetched
        ensureInitialized();
        CastLib castLib = castLibs.get(castLibNumber);
        if (castLib == null) {
            return false;
        }
        return castLib.isFetched();
    }

    @Override
    public boolean isCastLibExternal(int castLibNumber) {
        ensureInitialized();
        CastLib castLib = castLibs.get(castLibNumber);
        if (castLib == null) {
            return false;
        }
        return castLib.isExternal();
    }

    @Override
    public void preloadCasts(int mode) {
        ensureInitialized();

        for (CastLib castLib : castLibs.values()) {
            int preloadMode = castLib.getPreloadMode();

            // mode 1 = AfterFrameOne, mode 2 = BeforeFrameOne (MovieLoaded)
            if (preloadMode == mode) {
                // External casts must be fetched via preloadNetThing first
                if (castLib.isFetched() && !castLib.isLoaded()) {
                    castLib.load();
                }
            }
        }
    }

    // Raw data cache keyed by baseName (e.g. "hh_interface").
    // When a .cct is fetched via preloadNetThing, the raw bytes are cached here
    // so that later castLib.fileName assignments can load instantly without
    // a JS round-trip.
    private final Map<String, byte[]> castDataCache = new HashMap<>();
    private final Map<Integer, String> pendingExternalLoads = new HashMap<>();

    /**
     * Cache raw external cast data by base name for later reuse.
     */
    public void cacheExternalData(String url, byte[] data) {
        String baseName = FileUtil.getFileNameWithoutExtension(FileUtil.getFileName(url));
        castDataCache.put(baseName, data);
        for (CastLib castLib : findCastLibsByUrl(url)) {
            castLib.cacheFetchedExternalData(data);
        }
    }

    /**
     * Look up cached raw cast data by base name.
     */
    public byte[] getCachedExternalData(String baseName) {
        return castDataCache.get(baseName);
    }

    public void clearPendingExternalLoad(int castLibNumber) {
        pendingExternalLoads.remove(castLibNumber);
    }

    private void markPendingExternalLoad(int castLibNumber, String fileName) {
        pendingExternalLoads.put(castLibNumber,
                FileUtil.getFileNameWithoutExtension(FileUtil.getFileName(fileName)));
    }

    /**
     * Set external cast data from preloadNetThing.
     * @param castLibNumber The cast library number
     * @param data The raw file data
     * @return true if parsing was successful
     */
    public boolean setExternalCastData(int castLibNumber, byte[] data) {
        ensureInitialized();
        CastLib castLib = castLibs.get(castLibNumber);
        if (castLib == null) {
            return false;
        }
        boolean loaded = castLib.setExternalData(data);
        if (loaded) {
            clearPendingExternalLoad(castLibNumber);
        }
        return loaded;
    }

    /**
     * Set external cast data by URL from preloadNetThing.
     * Multiple cast libraries may reference the same external file (e.g. empty.cst).
     * This loads the data into ALL matching casts, not just the first one.
     * Matching is done by comparing the filename portion of the cast's path with the URL.
     * @param url The URL that was fetched
     * @param data The raw file data
     * @return true if at least one cast was loaded successfully
     */
    public boolean setExternalCastDataByUrl(String url, byte[] data) {
        ensureInitialized();
        boolean anyLoaded = false;
        for (CastLib castLib : findCastLibsByUrl(url)) {
            // Skip if already loaded with member data (prevents re-parsing same file)
            if (castLib.isLoaded() && castLib.getMemberCount() > 0) {
                anyLoaded = true;
                continue;
            }
            if (setExternalCastData(castLib.getNumber(), data)) {
                anyLoaded = true;
            }
        }
        return anyLoaded;
    }

    /**
     * Find cast library numbers whose file name matches the given URL/file name (by base name).
     * Used to trigger post-load reindexing in the Resource Manager when external casts arrive late.
     */
    public java.util.List<Integer> getMatchingCastLibNumbersByUrl(String url) {
        ensureInitialized();
        java.util.List<Integer> result = new java.util.ArrayList<>();
        for (CastLib castLib : findCastLibsByUrl(url)) {
            result.add(castLib.getNumber());
        }
        return result;
    }

    public java.util.List<Integer> getRequestedExternalCastSlots(String url) {
        ensureInitialized();

        String baseName = FileUtil.getFileNameWithoutExtension(FileUtil.getFileName(url));
        java.util.List<Integer> slots = new java.util.ArrayList<>();
        
        for (CastLib castLib : findCastLibsByUrl(url)) {
            int castLibNumber = castLib.getNumber();
            boolean wasRequested = castLib.isFetching()
                    || baseName.equalsIgnoreCase(pendingExternalLoads.get(castLibNumber))
                    || (!castLib.isLoaded() && castLib.matchesAuthoredExternalFile(baseName));
            if (wasRequested) {
                slots.add(castLibNumber);
            }
        }
        return slots;
    }

    private java.util.List<CastLib> findCastLibsByUrl(String url) {
        String extractedFileName = FileUtil.getFileName(url);
        String fileNameNoExt = FileUtil.getFileNameWithoutExtension(extractedFileName);
        java.util.List<CastLib> result = new java.util.ArrayList<>();

        for (CastLib castLib : castLibs.values()) {
            String castPath = castLib.getFileName();
            if (castPath == null || castPath.isEmpty()) continue;

            String castFileNoExt = FileUtil.getFileNameWithoutExtension(
                    FileUtil.getFileName(castPath));
            if (castFileNoExt.equalsIgnoreCase(fileNameNoExt)) {
                result.add(castLib);
            }
        }
        return result;
    }

    private static Datum getMemberByNameInCast(CastLib castLib, String memberName) {
        if (castLib == null || memberName == null || memberName.isEmpty()) {
            return Datum.VOID;
        }
        CastMemberChunk member = castLib.findMemberByName(memberName);
        if (member != null) {
            int memberNumber = castLib.getMemberNumber(member);
            return Datum.CastMemberRef.of(castLib.getNumber(), memberNumber);
        }
        CastMember dynamic = castLib.getMemberByName(memberName);
        if (dynamic != null) {
            return Datum.CastMemberRef.of(castLib.getNumber(), dynamic.getMemberNumber());
        }
        return Datum.VOID;
    }

    private static boolean isRegistryFallbackEligibleCast(CastLib castLib) {
        return castLib != null && castLib.usesStableRegistryBinding();
    }

    @Override
    public Datum createMember(int castLibNumber, String memberType) {
        CastLib castLib = getCastLib(castLibNumber);
        if (castLib == null) {
            return Datum.VOID;
        }
        CastMember member = castLib.createDynamicMember(memberType);
        if (member == null) {
            return Datum.VOID;
        }
        return Datum.CastMemberRef.of(castLibNumber, member.getMemberNumber());
    }

    @Override
    public com.libreshockwave.bitmap.Palette resolvePaletteByName(String name) {
        ensureInitialized();
        com.libreshockwave.bitmap.Palette firstMatch = null;

        for (CastLib castLib : castLibs.values()) {
            com.libreshockwave.chunks.CastMemberChunk chunk = castLib.findMemberByName(name);
            if (chunk != null && chunk.file() != null) {
                int memberNum = castLib.getMemberNumber(chunk);
                com.libreshockwave.bitmap.Palette pal =
                        memberNum > 0 ? chunk.file().resolvePaletteByMemberNumber(memberNum) : null;
                if (pal != null) {
                    if (firstMatch == null) {
                        firstMatch = pal;
                    }
                }
            }
            CastMember dynamic = castLib.getMemberByName(name);
            if (dynamic != null) {
                com.libreshockwave.bitmap.Palette pal = dynamic.getPaletteData();
                if (pal != null) {
                    if (firstMatch == null) {
                        firstMatch = pal;
                    }
                }
            }
        }
        return firstMatch;
    }

    @Override
    public com.libreshockwave.bitmap.Palette resolvePaletteByMember(int castLibNum, int memberNum) {
        ensureInitialized();
        CastLib castLib = getCastLib(castLibNum);
        if (castLib == null) {
            return null;
        }
        CastMember dynamicMember = castLib.getMember(memberNum);
        if (dynamicMember != null) {
            com.libreshockwave.bitmap.Palette dynamicPalette = dynamicMember.getPaletteData();
            if (dynamicPalette != null) {
                return dynamicPalette;
            }
        }
        com.libreshockwave.chunks.CastMemberChunk chunk = castLib.findMemberByNumber(memberNum);
        if (chunk != null && chunk.file() != null) {
            return chunk.file().resolvePaletteByMemberNumber(memberNum);
        }
        return null;
    }

    @Override
    public String getFieldValue(Object memberNameOrNum, int castId) {
        ensureInitialized();
        CastMember member = resolveFieldMember(memberNameOrNum, castId);
        if (member != null) {
            return resolveFieldText(member);
        }
        return "";
    }

    @Override
    public Datum getFieldDatum(Object memberNameOrNum, int castId) {
        ensureInitialized();
        CastMember member = resolveFieldMember(memberNameOrNum, castId);
        if (member == null) {
            return Datum.EMPTY_STRING;
        }
        return new Datum.FieldText(resolveFieldText(member), member.getCastLibNumber(), member.getMemberNumber());
    }

    @Override
    public Datum getFieldParsedValue(int castLibNumber, int memberNumber, com.libreshockwave.vm.LingoVM vm) {
        ensureInitialized();
        CastLib castLib = getCastLib(castLibNumber);
        if (castLib == null) {
            return Datum.VOID;
        }
        CastMember member = castLib.getMember(memberNumber);
        if (member == null) {
            return Datum.VOID;
        }
        return member.getParsedTextValue(vm);
    }

    @Override
    public void setFieldValue(Object memberNameOrNum, int castId, String value) {
        ensureInitialized();
        CastMember member = resolveFieldMember(memberNameOrNum, castId);
        if (member != null) {
            member.setDynamicText(value);
        }
    }

    private CastMember resolveFieldMember(Object memberNameOrNum, int castId) {
        CastMember member = null;

        if (memberNameOrNum instanceof String name) {
            if (castId > 0) {
                CastLib castLib = getCastLib(castId);
                if (castLib != null) {
                    member = castLib.getMemberByName(name);
                }
            } else {
                for (CastLib castLib : castLibs.values()) {
                    if (!castLib.isLoaded()) {
                        castLib.load();
                    }
                    member = castLib.getMemberByName(name);
                    if (member != null) {
                        break;
                    }
                }
            }
        } else if (memberNameOrNum instanceof Integer num) {
            int effectiveCastId;
            int effectiveMemberNum;
            if (num > 65535) {
                effectiveCastId = num >> 16;
                effectiveMemberNum = num & 0xFFFF;
            } else {
                effectiveCastId = castId > 0 ? castId : 1;
                effectiveMemberNum = num;
            }
            CastLib castLib = getCastLib(effectiveCastId);
            if (castLib != null) {
                member = castLib.getMember(effectiveMemberNum);
            }
        }

        return member;
    }

    private String resolveFieldText(CastMember member) {
        if (member == null) {
            return "";
        }

        String text = member.getTextContent();
        if (text.isEmpty() || !"memberalias.index".equalsIgnoreCase(member.getName())) {
            return text;
        }

        CastLib castLib = getCastLib(member.getCastLibNumber());
        if (castLib == null) {
            return text;
        }
        // Authored Resource Manager.preIndexMembers() indexes exact member names
        // before importing memberalias.index. Some public-room casts keep their
        // actual members under s_-prefixed source names while the alias table
        // still points at the canonical unprefixed names, so normalize the field
        // text before legacy scripts consume it.
        return rewriteAliasIndexTargetsForSourcePrefixedMembers(castLib, text);
    }

    private static String rewriteAliasIndexTargetsForSourcePrefixedMembers(CastLib castLib, String aliasText) {
        if (castLib == null || aliasText == null || aliasText.isEmpty()) {
            return aliasText != null ? aliasText : "";
        }

        boolean changed = false;
        StringBuilder rewritten = new StringBuilder(aliasText.length() + 32);
        String[] lines = aliasText.split("\\r\\n|\\r|\\n", -1);
        for (int i = 0; i < lines.length; i++) {
            String rawLine = lines[i];
            String normalizedLine = rawLine;

            int delimiter = rawLine.indexOf('=');
            if (delimiter > 0 && delimiter < rawLine.length() - 1) {
                String targetName = rawLine.substring(delimiter + 1);
                boolean mirrored = targetName.charAt(targetName.length() - 1) == '*';
                String bareTarget = mirrored ? targetName.substring(0, targetName.length() - 1) : targetName;
                String prefixedTarget = sourcePrefixedLookupName(bareTarget);
                if (prefixedTarget != null
                        && !castLib.hasMemberNamedExact(bareTarget)
                        && castLib.hasMemberNamedExact(prefixedTarget)) {
                    normalizedLine = rawLine.substring(0, delimiter + 1)
                            + prefixedTarget
                            + (mirrored ? "*" : "");
                    changed = true;
                }
            }

            if (i > 0) {
                rewritten.append('\r');
            }
            rewritten.append(normalizedLine);
        }

        return changed ? rewritten.toString() : aliasText;
    }

    private static String sourcePrefixedLookupName(String requestedName) {
        if (requestedName == null || requestedName.isEmpty()) {
            return null;
        }
        return requestedName.regionMatches(true, 0, "s_", 0, 2) ? null : "s_" + requestedName;
    }

    /**
     * Find a handler by name across all cast libraries.
     * Searches loaded external casts for the handler.
     */
    @Override
    public HandlerLocation findHandler(String handlerName) {
        ensureInitialized();

        for (CastLib castLib : castLibs.values()) {
            if (!castLib.isLoaded()) {
                // Only search loaded casts - don't trigger lazy load for handler search
                continue;
            }

            var defaultNames = castLib.getScriptNames();
            if (defaultNames == null) {
                continue;
            }

            for (var script : castLib.getAllScripts()) {
                if (!isGlobalHandlerScriptType(script.getScriptType())) {
                    continue;
                }
                // Use per-script Lnam (each Lctx has its own lnamSectionId)
                var scriptNames = getPerScriptNames(script, defaultNames);
                var handler = script.findHandler(handlerName, scriptNames);
                if (handler != null) {
                    return new HandlerLocation(castLib.getNumber(), script, handler, scriptNames);
                }
            }
        }

        return null;
    }

    private static boolean isGlobalHandlerScriptType(com.libreshockwave.chunks.ScriptChunk.ScriptType scriptType) {
        return scriptType != com.libreshockwave.chunks.ScriptChunk.ScriptType.PARENT
                && scriptType != com.libreshockwave.chunks.ScriptChunk.ScriptType.BEHAVIOR;
    }

    @Override
    public int getScriptChunkId(int castLibNumber, int memberNumber) {
        ensureInitialized();
        CastLib castLib = castLibs.get(castLibNumber);
        if (castLib == null || !castLib.isLoaded()) {
            return -1;
        }
        var script = castLib.getScript(memberNumber);
        return script != null ? script.id().value() : -1;
    }

    /**
     * Find a handler in a specific script by its cast member number.
     * Used for method calls on script instances - only searches the instance's parent script.
     *
     * @param memberNumber The cast member number (not the script chunk ID)
     * @param handlerName The handler name to find
     * @return The handler location if found, null otherwise
     */
    @Override
    public HandlerLocation findHandlerInScript(int memberNumber, String handlerName) {
        ensureInitialized();

        for (CastLib castLib : castLibs.values()) {
            if (!castLib.isLoaded()) {
                continue;
            }

            var defaultNames = castLib.getScriptNames();
            if (defaultNames == null) {
                continue;
            }

            // Look up script by member number
            var script = castLib.getScript(memberNumber);
            if (script != null) {
                var scriptNames = getPerScriptNames(script, defaultNames);
                var handler = script.findHandler(handlerName, scriptNames);
                if (handler != null) {
                    return new HandlerLocation(castLib.getNumber(), script, handler, scriptNames);
                }
                // Found the script but no handler - don't continue searching
                return null;
            }
        }

        return null;
    }

    /**
     * Find a handler in a specific script in a specific cast library.
     * Used when we know both the cast lib number and member number.
     */
    public HandlerLocation findHandlerInScript(int castLibNumber, int memberNumber, String handlerName) {
        ensureInitialized();

        CastLib castLib = castLibs.get(castLibNumber);
        if (castLib == null || !castLib.isLoaded()) {
            return null;
        }

        var defaultNames = castLib.getScriptNames();
        if (defaultNames == null) {
            return null;
        }

        var script = castLib.getScript(memberNumber);
        if (script != null) {
            var scriptNames = getPerScriptNames(script, defaultNames);
            var handler = script.findHandler(handlerName, scriptNames);
            if (handler != null) {
                return new HandlerLocation(castLib.getNumber(), script, handler, scriptNames);
            }
        }

        return null;
    }

    /**
     * Check if all external casts have been loaded.
     * Returns true when every external cast library has reached the LOADED state.
     */
    public boolean areAllCastsLoaded() {
        ensureInitialized();
        for (CastLib castLib : castLibs.values()) {
            if (castLib.isExternal() && !castLib.isLoaded()) {
                return false;
            }
        }
        return true;
    }

    @Override
    public java.util.List<String> getScriptPropertyNames(int castLibNumber, int memberNumber) {
        ensureInitialized();

        // If castLibNumber is 0, search all cast libs
        if (castLibNumber == 0) {
            for (CastLib castLib : castLibs.values()) {
                if (!castLib.isLoaded()) continue;
                var script = castLib.getScript(memberNumber);
                if (script != null && script.hasProperties()) {
                    var scriptNames = getPerScriptNames(script, castLib.getScriptNames());
                    return script.getPropertyNames(scriptNames);
                }
            }
            return java.util.List.of();
        }

        CastLib castLib = castLibs.get(castLibNumber);
        if (castLib == null || !castLib.isLoaded()) {
            return java.util.List.of();
        }

        var script = castLib.getScript(memberNumber);
        if (script == null || !script.hasProperties()) {
            return java.util.List.of();
        }

        var scriptNames = getPerScriptNames(script, castLib.getScriptNames());
        return script.getPropertyNames(scriptNames);
    }

    /**
     * Get the per-script Lnam for a script, falling back to a default.
     */
    private static com.libreshockwave.chunks.ScriptNamesChunk getPerScriptNames(
            com.libreshockwave.chunks.ScriptChunk script,
            com.libreshockwave.chunks.ScriptNamesChunk defaultNames) {
        if (script.file() != null) {
            var names = script.file().getScriptNamesForScript(script);
            if (names != null) return names;
        }
        return defaultNames;
    }
}
