package com.libreshockwave.player.net;

import com.libreshockwave.util.FileUtil;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.builtin.net.NetBuiltins;

import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.Map;
import java.util.concurrent.*;

/**
 * Manages asynchronous network requests for Lingo scripts.
 * Similar to dirplayer-rs net_manager.rs.
 *
 * Provides implementations for:
 * - preloadNetThing(util) - Start async GET request
 * - postNetText(util, postData) - Start async POST request
 * - netDone(taskId) - Check if request completed
 * - netTextResult(taskId) - Get result text
 * - netError(taskId) - Get error code
 * - getStreamStatus(taskId) - Get request status
 */
public class NetManager implements NetBuiltins.NetProvider {

    private final Map<Integer, NetTask> tasks = new ConcurrentHashMap<>();
    private final Map<String, byte[]> urlCache = new ConcurrentHashMap<>();  // Cache for loaded URLs
    private final Map<String, CompletableFuture<byte[]>> inFlightLoads = new ConcurrentHashMap<>();  // Dedup in-flight requests
    // Lazy-initialized to avoid creating threads/HTTP clients in environments that don't support them (e.g. TeaVM)
    private ExecutorService executor;
    private HttpClient httpClient;

    private int nextTaskId = 1;
    private int lastTaskId = 0;  // Track the most recent task for netDone() with no args
    private String basePath;
    private String localHttpRoot;  // For resolving localhost HTTP URLs to local filesystem

    // Callback for when a fetch completes (used to integrate with CastLibManager)
    private NetCompletionCallback completionCallback;

    public NetManager() {
        // Fields are lazy-initialized on first use via getExecutor() and getHttpClient()
    }

    private ExecutorService getExecutor() {
        if (executor == null) {
            executor = Executors.newCachedThreadPool(r -> {
                Thread t = new Thread(r, "NetManager-worker");
                t.setDaemon(true);
                return t;
            });
        }
        return executor;
    }

    private HttpClient getHttpClient() {
        if (httpClient == null) {
            httpClient = HttpClient.newBuilder()
                .connectTimeout(Duration.ofSeconds(2))
                .followRedirects(HttpClient.Redirect.NORMAL)
                .build();
        }
        return httpClient;
    }

    /**
     * Set the base path for resolving relative URLs.
     */
    public void setBasePath(String basePath) {
        this.basePath = basePath;
    }

    public String getBasePath() {
        return basePath;
    }

    /**
     * Set a local filesystem root for resolving localhost HTTP URLs.
     * When set, URLs like http://localhost/path/file.txt will be resolved
     * to localHttpRoot/path/file.txt on the filesystem.
     */
    public void setLocalHttpRoot(String root) {
        this.localHttpRoot = root;
    }

    /**
     * Callback interface for when a network fetch completes.
     */
    @FunctionalInterface
    public interface NetCompletionCallback {
        void onComplete(String url, byte[] data);
    }

    /**
     * Set a callback to be notified when network requests complete.
     * Used by Player to integrate with CastLibManager for external casts.
     */
    public void setCompletionCallback(NetCompletionCallback callback) {
        this.completionCallback = callback;
    }

    /**
     * Start an async GET request (preloadNetThing).
     * @param url The URL to fetch
     * @return The task ID for tracking the request
     */
    public int preloadNetThing(String url) {
        int taskId = nextTaskId++;
        lastTaskId = taskId;
        NetTask task = NetTask.get(taskId, url, resolveUrl(url));
        tasks.put(taskId, task);

        // Check cache synchronously - if already loaded, complete immediately
        String cacheKey = FileUtil.getFileName(url);
        byte[] cached = urlCache.get(cacheKey);
        if (cached != null) {
            System.out.println("[NetManager] Using cached: " + cacheKey + " (" + cached.length + " bytes)");
            task.markInProgress();
            task.complete(cached);
            notifyCompletion(url, cached);
            return taskId;
        }

        executeTask(task);
        return taskId;
    }

    /**
     * Start an async POST request (postNetText).
     * @param url The URL to post to
     * @param postData The form data to send
     * @return The task ID for tracking the request
     */
    public int postNetText(String url, String postData) {
        int taskId = nextTaskId++;
        lastTaskId = taskId;
        NetTask task = NetTask.post(taskId, url, resolveUrl(url), postData);
        tasks.put(taskId, task);
        executePostTask(task);
        return taskId;
    }

    /**
     * Check if a task is done.
     * @param taskId The task ID (or null to check latest)
     * @return true if the task completed or failed
     */
    @Override
    public boolean netDone(Integer taskId) {
        NetTask task = getTask(taskId);
        return task != null && task.isDone();
    }

    /**
     * Get the text result of a completed task.
     * @param taskId The task ID
     * @return The result text, or empty string if not done/failed
     */
    @Override
    public String netTextResult(Integer taskId) {
        NetTask task = getTask(taskId);
        if (task != null && task.getState() == NetTask.State.COMPLETED) {
            return task.getResultAsString();
        }
        return "";
    }

    /**
     * Get the error code of a task.
     * @param taskId The task ID
     * @return 0 for success/pending, non-zero for errors
     */
    @Override
    public int netError(Integer taskId) {
        NetTask task = getTask(taskId);
        return task != null ? task.getErrorCode() : 0;
    }

    /**
     * Get the stream status of a task.
     * @param taskId The task ID
     * @return Status string like "Connecting", "Loading", "Complete", "Error"
     */
    @Override
    public String getStreamStatus(Integer taskId) {
        NetTask task = getTask(taskId);
        return task != null ? task.getStreamStatus() : "Error";
    }

    /**
     * Get stream status as a PropList with #URL, #state, #bytesSoFar, #bytesTotal, #error keys.
     * Director's getStreamStatus() returns a PropList that scripts access via tStreamStatus[#bytesSoFar].
     */
    @Override
    public Datum getStreamStatusDatum(Integer taskId) {
        NetTask task = getTask(taskId);
        var props = new java.util.LinkedHashMap<String, Datum>();

        if (task == null) {
            props.put("URL", Datum.EMPTY_STRING);
            props.put("state", Datum.of("Error"));
            props.put("bytesSoFar", Datum.ZERO);
            props.put("bytesTotal", Datum.ZERO);
            props.put("error", Datum.of("OK"));
            return Datum.propList(props);
        }

        int bytesSoFar = task.getResult() != null ? task.getResult().length : 0;
        int bytesTotal = bytesSoFar; // For completed tasks, soFar == total

        props.put("URL", Datum.of(task.getOriginalUrl() != null ? task.getOriginalUrl() : ""));
        props.put("state", Datum.of(task.getStreamStatus()));
        props.put("bytesSoFar", Datum.of(bytesSoFar));
        props.put("bytesTotal", Datum.of(bytesTotal));
        props.put("error", task.getErrorCode() == 0 ? Datum.of("OK") : Datum.of(String.valueOf(task.getErrorCode())));
        return Datum.propList(props);
    }

    /**
     * Get the raw bytes of a completed task.
     */
    public byte[] getNetBytes(Integer taskId) {
        NetTask task = getTask(taskId);
        if (task != null && task.getState() == NetTask.State.COMPLETED) {
            return task.getResult();
        }
        return null;
    }

    /**
     * Get a task by ID.
     * @param taskId The task ID, or null to get the most recent task
     */
    public NetTask getTask(Integer taskId) {
        if (taskId == null || taskId == 0) {
            // Return the most recent task
            return lastTaskId > 0 ? tasks.get(lastTaskId) : null;
        }
        return tasks.get(taskId);
    }

    /**
     * Look up cached download data by URL.
     * Used by the castDataRequestCallback to load cast data when Lingo sets castLib.fileName.
     * Tries both .cst and .cct extensions since preloadNetThing may cache under the original
     * extension while castLib.fileName may use a different one (e.g. .cst vs .cct).
     */
    public byte[] getCachedData(String url) {
        String cacheKey = FileUtil.getFileName(url);
        byte[] data = urlCache.get(cacheKey);
        if (data != null) return data;

        // Try alternate cast extension (.cst <-> .cct) since Director uses both interchangeably
        String lower = cacheKey.toLowerCase();
        if (lower.endsWith(".cct")) {
            data = urlCache.get(cacheKey.substring(0, cacheKey.length() - 4) + ".cst");
        } else if (lower.endsWith(".cst")) {
            data = urlCache.get(cacheKey.substring(0, cacheKey.length() - 4) + ".cct");
        }
        return data;
    }

    /**
     * Shutdown the network manager.
     */
    public void shutdown() {
        if (executor != null) {
            executor.shutdownNow();
        }
    }

    private String resolveUrl(String url) {
        if (url == null || url.isEmpty()) {
            return url;
        }

        // Strip query parameters before resolving as file path
        // (URLs like "file.txt?random=123" are invalid file paths on Windows)
        String cleanUrl = url;
        int queryIdx = cleanUrl.indexOf('?');
        if (queryIdx >= 0) {
            cleanUrl = cleanUrl.substring(0, queryIdx);
        }

        // For HTTP(S) URLs, extract just the filename from the URL path
        // (Paths.get() fails on Windows for URLs like "http://localhost/path/file.txt")
        if (cleanUrl.startsWith("http://") || cleanUrl.startsWith("https://")) {
            int lastSlash = cleanUrl.lastIndexOf('/');
            if (lastSlash >= 0 && lastSlash < cleanUrl.length() - 1) {
                return cleanUrl.substring(lastSlash + 1);
            }
            return cleanUrl;
        }

        try {
            return Paths.get(cleanUrl).getFileName().toString();
        } catch (Exception e) {
            // Fallback: extract filename from last path separator
            int lastSlash = cleanUrl.lastIndexOf('/');
            int lastBackslash = cleanUrl.lastIndexOf('\\');
            int lastSep = Math.max(lastSlash, lastBackslash);
            if (lastSep >= 0 && lastSep < cleanUrl.length() - 1) {
                return cleanUrl.substring(lastSep + 1);
            }
            return cleanUrl;
        }
    }

    private void executeTask(NetTask task) {
        getExecutor().submit(() -> {
            task.markInProgress();

            String url = task.getOriginalUrl();
            String cacheKey = FileUtil.getFileName(url);

            // Use inFlightLoads to deduplicate: only one actual load per cacheKey
            CompletableFuture<byte[]> future = inFlightLoads.computeIfAbsent(cacheKey, k -> {
                CompletableFuture<byte[]> f = new CompletableFuture<>();
                getExecutor().submit(() -> doLoad(url, cacheKey, f));
                return f;
            });

            try {
                byte[] data = future.get();
                if (data != null) {
                    task.complete(data);
                    notifyCompletion(task.getOriginalUrl(), data);
                } else {
                    task.fail(404, "Load returned no data");
                }
            } catch (Exception e) {
                Throwable cause = e.getCause() != null ? e.getCause() : e;
                task.fail(-1, cause.getMessage());
            }
        });
    }

    /**
     * Perform the actual load for a cache key. Called only once per unique URL.
     */
    private void doLoad(String url, String cacheKey, CompletableFuture<byte[]> future) {
        try {
            // Check cache first
            byte[] cached = urlCache.get(cacheKey);
            if (cached != null) {
                System.out.println("[NetManager] Using cached: " + cacheKey + " (" + cached.length + " bytes)");
                future.complete(cached);
                return;
            }

            boolean isHttpUrl = url.startsWith("http://") || url.startsWith("https://");

            // HTTP URLs: fetch via HTTP directly (don't resolve against basePath)
            if (isHttpUrl) {
                // For localhost URLs with a localHttpRoot, try local filesystem first
                if (localHttpRoot != null && !localHttpRoot.isEmpty()
                        && (url.startsWith("http://localhost") || url.startsWith("http://127.0.0.1"))) {
                    String urlPath = extractUrlPath(url);
                    if (urlPath != null) {
                        Path localPath = Path.of(localHttpRoot).resolve(urlPath.startsWith("/") ? urlPath.substring(1) : urlPath);
                        byte[] data = tryLoadFromFile(localPath, cacheKey);
                        if (data != null) {
                            future.complete(data);
                            return;
                        }
                    }
                }
                byte[] data = loadFromHttpAndCache(url, cacheKey);
                future.complete(data);
                return;
            }

            // Non-HTTP URL: resolve relative path against basePath (the DCR/DIR location)
            String fileName = FileUtil.getFileName(url);

            if (basePath != null && !basePath.isEmpty()
                    && !basePath.startsWith("http://") && !basePath.startsWith("https://")) {
                Path base = Path.of(basePath);
                if (Files.isRegularFile(base)) {
                    base = base.getParent();
                }

                // Try loading from file with fallbacks
                byte[] data = tryLoadFromFile(base.resolve(fileName), cacheKey);
                if (data != null) {
                    future.complete(data);
                    return;
                }
            }

            // basePath is HTTP - resolve relative filename against it
            if (basePath != null && (basePath.startsWith("http://") || basePath.startsWith("https://"))) {
                String fullUrl = basePath + fileName;
                byte[] data = loadFromHttpAndCache(fullUrl, cacheKey);
                future.complete(data);
            } else {
                // No HTTP fallback available
                future.completeExceptionally(new RuntimeException("File not found: " + fileName));
            }
        } catch (Exception e) {
            future.completeExceptionally(e);
        } finally {
            inFlightLoads.remove(cacheKey);
        }
    }

    /**
     * Try to load a file with extension fallbacks. Returns data if found, null otherwise.
     */
    private byte[] tryLoadFromFile(Path path, String cacheKey) {
        try {
            Path resolvedPath = resolvePathWithFallbacks(path);
            if (resolvedPath == null) {
                return null;
            }

            byte[] data = Files.readAllBytes(resolvedPath);
            urlCache.put(cacheKey, data);  // Cache the result
            System.out.println("[NetManager] Loaded file: " + resolvedPath + " (" + data.length + " bytes)");
            return data;
        } catch (Exception e) {
            System.out.println("[NetManager] File load failed: " + path + " - " + e.getMessage());
            return null;
        }
    }

    /**
     * Resolve a file path with extension fallbacks.
     * For cast files (.cst, .cct): try requested, then .cst, then .cct
     * For movie files (.dcr, .dxr, .dir): try requested, then .dir, then .dcr
     */
    private Path resolvePathWithFallbacks(Path path) {
        // If the file exists, return it directly
        if (Files.exists(path)) {
            return path;
        }

        String fileName = path.getFileName().toString().toLowerCase();

        // Cast file extensions: try .cst first, then .cct
        if (fileName.endsWith(".cst") || fileName.endsWith(".cct")) {
            String baseName = getFileBaseName(path);
            Path parent = path.getParent();

            // Try .cst first
            Path cstPath = parent != null ? parent.resolve(baseName + ".cst") : Path.of(baseName + ".cst");
            if (Files.exists(cstPath)) {
                return cstPath;
            }

            // Try .cct as fallback
            Path cctPath = parent != null ? parent.resolve(baseName + ".cct") : Path.of(baseName + ".cct");
            if (Files.exists(cctPath)) {
                return cctPath;
            }
        }

        // Movie file extensions: try .dir first, then .dcr, then .dxr
        if (fileName.endsWith(".dcr") || fileName.endsWith(".dxr") || fileName.endsWith(".dir")) {
            String baseName = getFileBaseName(path);
            Path parent = path.getParent();

            // Try .dir first
            Path dirPath = parent != null ? parent.resolve(baseName + ".dir") : Path.of(baseName + ".dir");
            if (Files.exists(dirPath)) {
                return dirPath;
            }

            // Try .dcr as fallback
            Path dcrPath = parent != null ? parent.resolve(baseName + ".dcr") : Path.of(baseName + ".dcr");
            if (Files.exists(dcrPath)) {
                return dcrPath;
            }

            // Try .dxr as last fallback
            Path dxrPath = parent != null ? parent.resolve(baseName + ".dxr") : Path.of(baseName + ".dxr");
            if (Files.exists(dxrPath)) {
                return dxrPath;
            }
        }

        // No extension (e.g. "hosts") — try .cct and .cst as cast fallbacks
        if (!fileName.contains(".")) {
            String baseName = path.getFileName().toString();
            Path parent = path.getParent();

            Path cctPath = parent != null ? parent.resolve(baseName + ".cct") : Path.of(baseName + ".cct");
            if (Files.exists(cctPath)) {
                return cctPath;
            }

            Path cstPath = parent != null ? parent.resolve(baseName + ".cst") : Path.of(baseName + ".cst");
            if (Files.exists(cstPath)) {
                return cstPath;
            }
        }

        // File not found with any extension
        return null;
    }

    /**
     * Get the base name of a file (without extension).
     */
    private String getFileBaseName(Path path) {
        String fileName = path.getFileName().toString();
        int dotIndex = fileName.lastIndexOf('.');
        return dotIndex > 0 ? fileName.substring(0, dotIndex) : fileName;
    }

    /**
     * Execute a POST task directly (no caching/dedup).
     */
    private void executePostTask(NetTask task) {
        getExecutor().submit(() -> {
            task.markInProgress();
            String url = task.getOriginalUrl();
            String fileName = FileUtil.getFileName(url);
            String cacheKey = fileName;
            try {
                if (url.startsWith("http://") || url.startsWith("https://")) {
                    loadFromHttp(url, task, cacheKey);
                } else if (basePath != null && (basePath.startsWith("http://") || basePath.startsWith("https://"))) {
                    String fullUrl = basePath + fileName;
                    loadFromHttp(fullUrl, task, cacheKey);
                } else {
                    task.fail(404, "No HTTP URL for POST");
                }
            } catch (Exception e) {
                task.fail(-1, e.getMessage());
            }
        });
    }

    /**
     * Load from HTTP, cache the result, and return the bytes.
     * Used by doLoad for GET requests.
     */
    private byte[] loadFromHttpAndCache(String url, String cacheKey) throws Exception {
        String[] urlsToTry = getUrlsWithFallbacks(url);

        Exception lastException = null;
        int lastStatusCode = 0;

        for (String tryUrl : urlsToTry) {
            try {
                HttpRequest request = HttpRequest.newBuilder()
                    .uri(URI.create(tryUrl))
                    .timeout(Duration.ofSeconds(3))
                    .GET()
                    .build();

                HttpResponse<byte[]> response = getHttpClient().send(
                    request,
                    HttpResponse.BodyHandlers.ofByteArray()
                );

                int statusCode = response.statusCode();
                if (statusCode >= 200 && statusCode < 300) {
                    byte[] data = response.body();
                    urlCache.put(cacheKey, data);
                    System.out.println("[NetManager] Loaded URL: " + tryUrl + " (" + data.length + " bytes)");
                    return data;
                }
                lastStatusCode = statusCode;
            } catch (Exception e) {
                lastException = e;
            }
        }

        // All URLs failed
        String msg;
        if (lastStatusCode > 0) {
            msg = "HTTP " + lastStatusCode;
        } else if (lastException != null) {
            msg = lastException.getMessage();
        } else {
            msg = "Not found";
        }
        System.err.println("[NetManager] HTTP request failed: " + url + " - " + msg);
        throw new RuntimeException(msg);
    }

    private void loadFromHttp(String url, NetTask task, String cacheKey) throws Exception {
        // Try the URL with extension fallbacks
        String[] urlsToTry = getUrlsWithFallbacks(url);

        Exception lastException = null;
        int lastStatusCode = 0;

        for (String tryUrl : urlsToTry) {
            try {
                HttpRequest.Builder requestBuilder = HttpRequest.newBuilder()
                    .uri(URI.create(tryUrl))
                    .timeout(Duration.ofSeconds(3));

                if (task.getMethod() == NetTask.Method.POST) {
                    requestBuilder.header("Content-Type", "application/x-www-form-urlencoded")
                        .POST(HttpRequest.BodyPublishers.ofString(
                            task.getPostData() != null ? task.getPostData() : ""));
                } else {
                    requestBuilder.GET();
                }

                HttpResponse<byte[]> response = getHttpClient().send(
                    requestBuilder.build(),
                    HttpResponse.BodyHandlers.ofByteArray()
                );

                int statusCode = response.statusCode();
                if (statusCode >= 200 && statusCode < 300) {
                    byte[] data = response.body();
                    urlCache.put(cacheKey, data);  // Cache the result
                    System.out.println("[NetManager] Loaded URL: " + tryUrl + " (" + data.length + " bytes)");
                    task.complete(data);
                    notifyCompletion(task.getUrl(), data);
                    return;
                }
                lastStatusCode = statusCode;
            } catch (Exception e) {
                lastException = e;
            }
        }

        // All URLs failed
        if (lastStatusCode > 0) {
            System.err.println("[NetManager] HTTP request failed: " + url + " - HTTP " + lastStatusCode);
            task.fail(lastStatusCode, "HTTP " + lastStatusCode);
        } else if (lastException != null) {
            System.err.println("[NetManager] HTTP request failed: " + url + " - " + lastException.getMessage());
            task.fail(-1, lastException.getMessage());
        } else {
            System.err.println("[NetManager] HTTP request failed: " + url + " - Not found");
            task.fail(404, "Not found");
        }
    }

    private String[] getUrlsWithFallbacks(String url) {
        return FileUtil.getUrlsWithFallbacks(url);
    }

    /**
     * Extract the path component from an HTTP URL (stripping host, port, and query params).
     */
    private String extractUrlPath(String url) {
        try {
            // Strip query parameters
            String cleanUrl = url;
            int queryIdx = cleanUrl.indexOf('?');
            if (queryIdx >= 0) {
                cleanUrl = cleanUrl.substring(0, queryIdx);
            }
            // Find the path after host:port
            int schemeEnd = cleanUrl.indexOf("://");
            if (schemeEnd < 0) return null;
            int pathStart = cleanUrl.indexOf('/', schemeEnd + 3);
            if (pathStart < 0) return "/";
            return cleanUrl.substring(pathStart);
        } catch (Exception e) {
            return null;
        }
    }

    private void notifyCompletion(String fileName, byte[] data) {
        if (completionCallback != null && fileName != null && data != null) {
            try {
                completionCallback.onComplete(fileName, data);
            } catch (Exception e) {
                System.err.println("[NetManager] Completion callback error: " + e.getMessage());
            }
        }
    }
}
