package com.libreshockwave.player.wasm;

import com.libreshockwave.util.FileUtil;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.opcode.dispatch.StringMethodDispatcher;
import com.libreshockwave.vm.builtin.movie.MoviePropertyProvider;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class WasmBootstrapTest {

    private static final Path ARCHIVE_DIR =
            Path.of("/tmp/habbo_origins_src/archive/unprotected_client_dump_20_06_2024");
    private static final Path LIVE_FUSE_CLIENT_PATH = Path.of("/tmp/fuse_client_14_1_b8.cct");

    @Test
    void wasmEntryQueuesGotoNetMovieRequests() throws Exception {
        while (WasmEntry.readNextGotoNetMovie() != 0) {
            // Drain stale requests from previous tests.
        }

        String url = "https://example.com/rooms/welcome_lounge.dcr";
        byte[] expected = url.getBytes(StandardCharsets.UTF_8);
        int requestId = WasmEntry.enqueueGotoNetMovie(url);
        assertTrue(requestId > 0);

        int len = WasmEntry.readNextGotoNetMovie();
        assertEquals(expected.length, len);
        assertEquals(url, new String(getStaticByteArray("stringBuffer"), 0, len, StandardCharsets.UTF_8));
        assertEquals(0, WasmEntry.readNextGotoNetMovie());
    }

    @Test
    void habboStartsThroughWasmPreloadPath() throws Exception {
        Path moviePath = ARCHIVE_DIR.resolve("habbo.dcr");
        Path fuseClientPath = ARCHIVE_DIR.resolve("fuse_client.cct");
        Path emptyCastPath = ARCHIVE_DIR.resolve("empty.cct");

        if (!Files.isRegularFile(moviePath) || !Files.isRegularFile(fuseClientPath)
                || !Files.isRegularFile(emptyCastPath)) {
            return;
        }

        WasmPlayer wasmPlayer = new WasmPlayer();
        WasmPlayer[] wasmRef = new WasmPlayer[1];
        assertTrue(wasmPlayer.loadMovie(Files.readAllBytes(moviePath), moviePath.toUri().toString(),
                (castLibNumber, fileName) -> {
                    WasmPlayer current = wasmRef[0];
                    String baseName = FileUtil.getFileNameWithoutExtension(FileUtil.getFileName(fileName));
                    byte[] cached = current.getPlayer().getCastLibManager().getCachedExternalData(baseName);
                    if (cached != null
                            && current.getPlayer().getCastLibManager().setExternalCastData(castLibNumber, cached)) {
                        current.getPlayer().onSynchronousExternalCastLoad(castLibNumber);
                        current.bumpCastRevision();
                    }
                }));
        wasmRef[0] = wasmPlayer;

        int queued = wasmPlayer.preloadCasts();
        assertTrue(queued > 0);

        QueuedNetProvider netProvider = wasmPlayer.getNetProvider();
        for (QueuedNetProvider.PendingRequest request : netProvider.getPendingRequests()) {
            String fileName = FileUtil.getFileName(request.url).toLowerCase();
            if ("fuse_client.cct".equals(fileName)) {
                netProvider.onFetchComplete(request.taskId, Files.readAllBytes(fuseClientPath));
            } else if ("empty.cct".equals(fileName)) {
                netProvider.onFetchComplete(request.taskId, Files.readAllBytes(emptyCastPath));
            }
        }
        netProvider.drainPendingRequests();

        String systemProps = wasmPlayer.getPlayer().getCastLibManager().getFieldValue("System Props", 0);
        assertFalse(systemProps.isEmpty());
        assertTrue(systemProps.contains("broker.manager.class"));

        wasmPlayer.play();

        String error = wasmPlayer.getPlayer().getRecentScriptErrorMessage(60_000);
        String stack = wasmPlayer.getPlayer().getRecentScriptErrorStack(60_000);
        assertEquals("", error, () -> error + "\n" + stack);
    }

    @Test
    void habboStartsThroughWasmEntryExportPath() throws Exception {
        Path moviePath = ARCHIVE_DIR.resolve("habbo.dcr");
        Path fuseClientPath = ARCHIVE_DIR.resolve("fuse_client.cct");
        Path emptyCastPath = ARCHIVE_DIR.resolve("empty.cct");

        if (!Files.isRegularFile(moviePath) || !Files.isRegularFile(fuseClientPath)
                || !Files.isRegularFile(emptyCastPath)) {
            return;
        }

        byte[] movieBytes = Files.readAllBytes(moviePath);
        byte[] fuseClientBytes = Files.readAllBytes(fuseClientPath);
        byte[] emptyCastBytes = Files.readAllBytes(emptyCastPath);
        byte[] stringBuffer = getStaticByteArray("stringBuffer");

        setStaticByteArray("movieBuffer", movieBytes.clone());

        byte[] basePathBytes = moviePath.toUri().toString().getBytes();
        System.arraycopy(basePathBytes, 0, stringBuffer, 0, basePathBytes.length);
        assertTrue(WasmEntry.loadMovie(movieBytes.length, basePathBytes.length) != 0);

        assertTrue(WasmEntry.preloadCasts() > 0);
        drainQueuedFetches(fuseClientBytes, emptyCastBytes, stringBuffer);

        WasmPlayer entryPlayer = getStaticWasmPlayer();
        entryPlayer.getPlayer().getVM().addTraceHandler("exitFrame");
        entryPlayer.getPlayer().getVM().addTraceHandler("startClient");
        entryPlayer.getPlayer().getVM().addTraceHandler("dumpVariableField");
        entryPlayer.getPlayer().getVM().addTraceHandler("getBrokerManager");
        entryPlayer.getPlayer().getVM().addTraceHandler("constructBrokerManager");
        entryPlayer.getPlayer().getVM().addTraceHandler("getClassVariable");
        String systemProps = entryPlayer.getPlayer().getCastLibManager().getFieldValue("System Props", 0);
        assertFalse(systemProps.isEmpty());
        assertTrue(systemProps.contains("broker.manager.class"));

        WasmEntry.play();

        String error = entryPlayer.getPlayer().getRecentScriptErrorMessage(60_000);
        String stack = entryPlayer.getPlayer().getRecentScriptErrorStack(60_000);
        assertEquals("", error, () -> error + "\n" + stack);

        int lastErrorLen = WasmEntry.getLastError();
        String lastError = lastErrorLen > 0 ? new String(stringBuffer, 0, lastErrorLen) : "";
        assertEquals("", lastError);
    }

    @Test
    void liveProtectedFuseClientExposesSystemPropsThroughWasmEntryPath() throws Exception {
        Path moviePath = ARCHIVE_DIR.resolve("habbo.dcr");
        Path emptyCastPath = ARCHIVE_DIR.resolve("empty.cct");

        if (!Files.isRegularFile(moviePath) || !Files.isRegularFile(emptyCastPath)
                || !Files.isRegularFile(LIVE_FUSE_CLIENT_PATH)) {
            return;
        }

        byte[] movieBytes = Files.readAllBytes(moviePath);
        byte[] fuseClientBytes = Files.readAllBytes(LIVE_FUSE_CLIENT_PATH);
        byte[] emptyCastBytes = Files.readAllBytes(emptyCastPath);
        byte[] stringBuffer = getStaticByteArray("stringBuffer");

        setStaticByteArray("movieBuffer", movieBytes.clone());

        byte[] basePathBytes = moviePath.toUri().toString().getBytes();
        System.arraycopy(basePathBytes, 0, stringBuffer, 0, basePathBytes.length);
        assertTrue(WasmEntry.loadMovie(movieBytes.length, basePathBytes.length) != 0);

        assertTrue(WasmEntry.preloadCasts() > 0);
        drainQueuedFetches(fuseClientBytes, emptyCastBytes, stringBuffer);

        WasmPlayer entryPlayer = getStaticWasmPlayer();
        String systemProps = entryPlayer.getPlayer().getCastLibManager().getFieldValue("System Props", 0);
        Map<String, String> dumped = emulateVariableContainerDump(systemProps);
        assertFalse(systemProps.isEmpty());
        assertTrue(systemProps.contains("broker.manager.class"));
        assertTrue(dumped.containsKey("broker.manager.class"));
        assertEquals("[\"Broker Manager Class\"]", dumped.get("broker.manager.class"));

        WasmEntry.play();

        String error = entryPlayer.getPlayer().getRecentScriptErrorMessage(60_000);
        String stack = entryPlayer.getPlayer().getRecentScriptErrorStack(60_000);
        assertEquals("", error, () -> error + "\n" + stack);

        int lastErrorLen = WasmEntry.getLastError();
        String lastError = lastErrorLen > 0 ? new String(stringBuffer, 0, lastErrorLen) : "";
        assertEquals("", lastError);
    }

    @Test
    void liveProtectedFuseClientSurvivesFirstTickLateRefetch() throws Exception {
        Path moviePath = ARCHIVE_DIR.resolve("habbo.dcr");
        Path emptyCastPath = ARCHIVE_DIR.resolve("empty.cct");

        if (!Files.isRegularFile(moviePath) || !Files.isRegularFile(emptyCastPath)
                || !Files.isRegularFile(LIVE_FUSE_CLIENT_PATH)) {
            return;
        }

        byte[] movieBytes = Files.readAllBytes(moviePath);
        byte[] fuseClientBytes = Files.readAllBytes(LIVE_FUSE_CLIENT_PATH);
        byte[] emptyCastBytes = Files.readAllBytes(emptyCastPath);
        byte[] stringBuffer = getStaticByteArray("stringBuffer");

        setStaticByteArray("movieBuffer", movieBytes.clone());

        byte[] basePathBytes = moviePath.toUri().toString().getBytes();
        System.arraycopy(basePathBytes, 0, stringBuffer, 0, basePathBytes.length);
        assertTrue(WasmEntry.loadMovie(movieBytes.length, basePathBytes.length) != 0);

        assertTrue(WasmEntry.preloadCasts() > 0);
        drainQueuedFetches(fuseClientBytes, emptyCastBytes, stringBuffer);

        WasmPlayer prePlayPlayer = getStaticWasmPlayer();
        var prePlayCast2 = prePlayPlayer.getPlayer().getCastLibManager().getCastLib(2);
        boolean cast2LoadedBeforePlay = prePlayCast2 != null && prePlayCast2.isLoaded();
        int cast2MemberCountBeforePlay = prePlayCast2 != null ? prePlayCast2.getMemberCount() : -1;
        String coreThreadByNumberBeforePlay = prePlayPlayer.getPlayer()
                .getCastLibManager()
                .getMemberProp(2, 75, "name")
                .toStr();

        getStaticWasmPlayer().getPlayer().getVM().addTraceHandler("initThread");
        getStaticWasmPlayer().getPlayer().getVM().addTraceHandler("buildThreadObj");
        getStaticWasmPlayer().getPlayer().getVM().addTraceHandler("getResourceManager");
        getStaticWasmPlayer().getPlayer().getVM().addTraceHandler("constructResourceManager");
        getStaticWasmPlayer().getPlayer().getVM().addTraceHandler("preIndexMembers");
        getStaticWasmPlayer().getPlayer().getVM().addTraceHandler("getThreadManager");
        getStaticWasmPlayer().getPlayer().getVM().addTraceHandler("constructThreadManager");
        WasmEntry.play();
        int pendingAfterPlay = WasmEntry.getPendingFetchCount();
        assertEquals(0, pendingAfterPlay, () -> "unexpected pending fetches after play: " + pendingAfterPlay);

        // Frame 1 startup can queue authored late fetches. Reproduce the browser path
        // across several startup ticks so any deferred cast/thread initialization runs.
        for (int i = 0; i < 6; i++) {
            WasmEntry.tick();
            drainQueuedFetches(fuseClientBytes, emptyCastBytes, stringBuffer);
        }

        WasmPlayer entryPlayer = getStaticWasmPlayer();
        LingoVM vm = entryPlayer.getPlayer().getVM();
        invokePrivateNoArg(entryPlayer.getPlayer(), "setupProviders");
        Datum resourceManager;
        Datum coreThreadMemNum;
        String cast2Name;
        String cast2FileName;
        Datum fuseClientCastRef;
        java.util.List<String> unresolvedThreadClasses;
        try {
            cast2Name = entryPlayer.getPlayer().getCastLibManager().getCastLib(2).getName();
            cast2FileName = entryPlayer.getPlayer().getCastLibManager().getCastLib(2).getFileName();
            fuseClientCastRef = vm.callBuiltin("castLib", List.of(Datum.of("fuse_client")));
            resourceManager = vm.callHandler("getResourceManager", List.of());
            coreThreadMemNum = vm.callHandler("getmemnum", List.of(Datum.of("Core Thread Class")));
            unresolvedThreadClasses = collectUnresolvedThreadClasses(entryPlayer, vm);
        } finally {
            invokePrivateNoArg(entryPlayer.getPlayer(), "clearProviders");
        }
        String error = entryPlayer.getPlayer().getRecentScriptErrorMessage(60_000);
        String stack = entryPlayer.getPlayer().getRecentScriptErrorStack(60_000);
        assertEquals("", error, () -> error
                + "\nstack:\n" + stack
                + "\nresourceManager=" + resourceManager
                + "\ncoreThreadMemNum=" + coreThreadMemNum
                + "\ncast2Name=" + cast2Name
                + "\ncast2FileName=" + cast2FileName
                + "\nfuseClientCastRef=" + fuseClientCastRef
                + "\ncast2LoadedBeforePlay=" + cast2LoadedBeforePlay
                + "\ncast2MemberCountBeforePlay=" + cast2MemberCountBeforePlay
                + "\ncoreThreadByNumberBeforePlay=" + coreThreadByNumberBeforePlay
                + "\nunresolvedThreadClasses=" + unresolvedThreadClasses);
        assertTrue(coreThreadMemNum.toInt() > 0, () -> "Core Thread Class was not reindexed: "
                + coreThreadMemNum
                + "\nresourceManager=" + resourceManager
                + "\ncast2Name=" + cast2Name
                + "\ncast2FileName=" + cast2FileName
                + "\nunresolvedThreadClasses=" + unresolvedThreadClasses);

        int lastErrorLen = WasmEntry.getLastError();
        String lastError = lastErrorLen > 0 ? new String(stringBuffer, 0, lastErrorLen) : "";
        assertEquals("", lastError);
    }

    private static void drainQueuedFetches(byte[] fuseClientBytes, byte[] emptyCastBytes, byte[] stringBuffer)
            throws Exception {
        while (true) {
            int pending = WasmEntry.getPendingFetchCount();
            if (pending <= 0) {
                return;
            }

            int[] taskIds = new int[pending];
            String[] urls = new String[pending];
            for (int i = 0; i < pending; i++) {
                taskIds[i] = WasmEntry.getPendingFetchTaskId(i);
                int urlLen = WasmEntry.getPendingFetchUrl(i);
                urls[i] = urlLen > 0 ? new String(stringBuffer, 0, urlLen) : "";
            }
            WasmEntry.drainPendingFetches();

            for (int i = 0; i < pending; i++) {
                byte[] data = urls[i].endsWith("fuse_client.cct") ? fuseClientBytes : emptyCastBytes;
                setStaticByteArray("netBuffer", data.clone());
                WasmEntry.deliverFetchResult(taskIds[i], data.length);
            }
        }
    }

    private static byte[] getStaticByteArray(String fieldName) throws Exception {
        Field field = WasmEntry.class.getDeclaredField(fieldName);
        field.setAccessible(true);
        return (byte[]) field.get(null);
    }

    private static void setStaticByteArray(String fieldName, byte[] value) throws Exception {
        Field field = WasmEntry.class.getDeclaredField(fieldName);
        field.setAccessible(true);
        field.set(null, value);
    }

    private static WasmPlayer getStaticWasmPlayer() throws Exception {
        Field field = WasmEntry.class.getDeclaredField("wasmPlayer");
        field.setAccessible(true);
        return (WasmPlayer) field.get(null);
    }

    private static void invokePrivateNoArg(Object target, String methodName) throws Exception {
        Method method = target.getClass().getDeclaredMethod(methodName);
        method.setAccessible(true);
        method.invoke(target);
    }

    private static java.util.List<String> collectUnresolvedThreadClasses(WasmPlayer player, LingoVM vm) {
        String systemProps = player.getPlayer().getCastLibManager().getFieldValue("System Props", 0);
        Map<String, String> dumpedSystemProps = emulateVariableContainerDump(systemProps);
        String threadIndexFieldName = dumpedSystemProps.getOrDefault("thread.index.field", "");
        java.util.List<String> unresolved = new java.util.ArrayList<>();
        if (threadIndexFieldName.isEmpty()) {
            return unresolved;
        }

        int castCount = player.getPlayer().getCastLibManager().getCastLibCount();
        for (int castNum = 1; castNum <= castCount; castNum++) {
            String threadIndexText = player.getPlayer().getCastLibManager().getFieldValue(threadIndexFieldName, castNum);
            if (threadIndexText.isEmpty()) {
                continue;
            }
            Map<String, String> threadProps = emulateVariableContainerDump(threadIndexText);
            for (var entry : threadProps.entrySet()) {
                if (!entry.getKey().endsWith(".class")) {
                    continue;
                }
                java.util.List<String> classNames = parseClassNames(vm, entry.getValue());
                for (String className : classNames) {
                    Datum memNum = vm.callHandler("getmemnum", List.of(Datum.of(className)));
                    if (memNum.toInt() < 1) {
                        unresolved.add("cast#" + castNum + ":" + entry.getKey() + "=" + className);
                    }
                }
            }
        }
        return unresolved;
    }

    private static java.util.List<String> parseClassNames(LingoVM vm, String rawValue) {
        if (rawValue == null || rawValue.isEmpty()) {
            return java.util.List.of();
        }
        if (!rawValue.startsWith("[")) {
            return java.util.List.of(rawValue);
        }

        Datum parsed = vm.callHandler("value", List.of(Datum.of(rawValue)));
        if (parsed instanceof Datum.List list) {
            java.util.List<String> classNames = new java.util.ArrayList<>();
            for (Datum datum : list.items()) {
                classNames.add(datum.toStr());
            }
            return classNames;
        }
        return java.util.List.of(rawValue);
    }

    private static Map<String, String> emulateVariableContainerDump(String systemProps) {
        MoviePropertyProvider.ItemDelimiterCache._char = '\r';
        Map<String, String> dumped = new LinkedHashMap<>();
        int pairCount = countChunk(systemProps, "item");
        for (int i = 1; i <= pairCount; i++) {
            String pair = getChunk(systemProps, "item", i, i);
            if (pair.isEmpty()) {
                continue;
            }
            String normalizedPair = normalizeWords(pair);
            if (normalizedPair.isEmpty()) {
                continue;
            }
            String firstChar = getChunk(getChunk(normalizedPair, "word", 1, 1), "char", 1, 1);
            if ("#".equals(firstChar)) {
                continue;
            }

            MoviePropertyProvider.ItemDelimiterCache._char = '=';
            String prop = normalizeWords(getChunk(normalizedPair, "item", 1, 1));
            String value = normalizeWords(getChunk(normalizedPair, "item", 2, countChunk(normalizedPair, "item")));
            dumped.put(prop, value);
            MoviePropertyProvider.ItemDelimiterCache._char = '\r';
        }
        return dumped;
    }

    private static int countChunk(String value, String chunkType) {
        return StringMethodDispatcher.dispatch(
                Datum.of(value),
                "count",
                List.of(Datum.symbol(chunkType))).toInt();
    }

    private static String getChunk(String value, String chunkType, int start, int end) {
        return StringMethodDispatcher.dispatch(
                Datum.of(value),
                "getProp",
                List.of(Datum.symbol(chunkType), Datum.of(start), Datum.of(end))).toStr();
    }

    private static String normalizeWords(String value) {
        int wordCount = countChunk(value, "word");
        if (wordCount <= 0) {
            return "";
        }
        return getChunk(value, "word", 1, wordCount);
    }
}
