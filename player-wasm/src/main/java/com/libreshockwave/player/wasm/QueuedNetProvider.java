package com.libreshockwave.player.wasm;

import com.libreshockwave.util.FileUtil;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.builtin.net.NetBuiltins;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Polling-based network provider for WASM.
 * No @Import annotations — all communication is JS → WASM via @Export methods.
 *
 * When Lingo calls preloadNetThing(url), the request is queued. After each tick(),
 * JS polls for pending requests via WasmEntry, does fetch(), and delivers results
 * back via deliverFetchResult/deliverFetchError exports.
 */
public class QueuedNetProvider implements NetBuiltins.NetProvider {

    private final String basePath;
    private final Map<Integer, NetTask> tasks = new HashMap<>();
    private final List<PendingRequest> pendingRequests = new ArrayList<>();
    private int nextTaskId = 1;
    private int lastTaskId = 0;

    /** Called when a fetch completes with data, allowing Player to cache cast files. */
    private java.util.function.BiConsumer<String, byte[]> fetchCompleteCallback;

    public QueuedNetProvider(String basePath) {
        this.basePath = basePath;
    }

    public void setFetchCompleteCallback(java.util.function.BiConsumer<String, byte[]> callback) {
        this.fetchCompleteCallback = callback;
    }

    @Override
    public int preloadNetThing(String url) {
        int taskId = nextTaskId++;
        lastTaskId = taskId;

        String resolvedUrl = resolveUrl(url);
        String[] fallbacks = FileUtil.getUrlsWithFallbacks(resolvedUrl);

        NetTask task = new NetTask(taskId, resolvedUrl);
        task.fallbackUrls = fallbacks;
        tasks.put(taskId, task);

        pendingRequests.add(new PendingRequest(taskId, fallbacks[0], "GET", null, fallbacks));
        return taskId;
    }

    @Override
    public int postNetText(String url, String postData) {
        int taskId = nextTaskId++;
        lastTaskId = taskId;

        String resolvedUrl = resolveUrl(url);
        NetTask task = new NetTask(taskId, resolvedUrl);
        tasks.put(taskId, task);

        pendingRequests.add(new PendingRequest(taskId, resolvedUrl, "POST", postData, null));
        return taskId;
    }

    @Override
    public boolean netDone(Integer taskId) {
        NetTask task = getTask(taskId);
        return task != null && task.done;
    }

    @Override
    public String netTextResult(Integer taskId) {
        NetTask task = getTask(taskId);
        if (task != null && task.done && task.data != null) {
            return new String(task.data);
        }
        return "";
    }

    @Override
    public int netError(Integer taskId) {
        NetTask task = getTask(taskId);
        return task != null ? task.errorCode : 0;
    }

    @Override
    public String getStreamStatus(Integer taskId) {
        NetTask task = getTask(taskId);
        if (task == null) return "Error";
        if (task.done) return task.errorCode == 0 ? "Complete" : "Error";
        return "Loading";
    }

    /**
     * Returns stream status as a PropList with real bytesSoFar so that the
     * Download Instance's check (tStreamStatus[#bytesSoFar] > 0) passes.
     */
    @Override
    public Datum getStreamStatusDatum(Integer taskId) {
        NetTask task = getTask(taskId);
        java.util.LinkedHashMap<String, Datum> props = new java.util.LinkedHashMap<>();
        if (task == null) {
            props.put("URL",        Datum.EMPTY_STRING);
            props.put("state",      Datum.of("Error"));
            props.put("bytesSoFar", Datum.ZERO);
            props.put("bytesTotal", Datum.ZERO);
            props.put("error",      Datum.of("OK"));
            return Datum.propList(props);
        }
        int byteCount;
        if (task.done) {
            byteCount = task.byteCount;
        } else {
            // Report incrementing bytesSoFar while loading to prevent
            // Lingo CastLoad Instance from thinking the download stalled.
            // JS fetch() doesn't provide intermediate progress, but the
            // Director plugin would report bytes as they stream in.
            task.pollCount++;
            byteCount = task.pollCount;
        }
        String state  = task.done ? (task.errorCode == 0 ? "Complete" : "Error") : "Loading";
        props.put("URL",        Datum.EMPTY_STRING);
        props.put("state",      Datum.of(state));
        props.put("bytesSoFar", Datum.of(byteCount));
        props.put("bytesTotal", Datum.of(task.done ? task.byteCount : 0));
        props.put("error",      task.errorCode == 0 ? Datum.of("OK") : Datum.of(String.valueOf(task.errorCode)));
        return Datum.propList(props);
    }

    /**
     * Get the URL for a task (used to identify external cast loads).
     */
    public String getTaskUrl(int taskId) {
        NetTask task = tasks.get(taskId);
        return task != null ? task.url : null;
    }

    /**
     * Get all pending requests for JS to read.
     */
    public List<PendingRequest> getPendingRequests() {
        return pendingRequests;
    }

    /**
     * Get a pending request by index for indexed WASM export access.
     */
    public PendingRequest getRequest(int index) {
        return (index >= 0 && index < pendingRequests.size())
                ? pendingRequests.get(index) : null;
    }

    /**
     * Clear pending requests after JS has read them.
     */
    public void drainPendingRequests() {
        pendingRequests.clear();
    }

    /**
     * Called when JS delivers a successful fetch result.
     */
    public void onFetchComplete(int taskId, byte[] data) {
        NetTask task = tasks.get(taskId);
        if (task != null) {
            task.data = data;
            task.byteCount = data != null ? data.length : 0;
            task.done = true;

            if (fetchCompleteCallback != null && task.url != null && data != null) {
                fetchCompleteCallback.accept(task.url, data);
            }
        }
    }

    /**
     * Mark a fetch task as done with only the byte count (no data stored).
     * Used for cast files where bytes stay in JS memory.
     * bytesSoFar/bytesTotal report correctly for Lingo's download check.
     */
    public void onFetchStatusComplete(int taskId, int byteCount) {
        NetTask task = tasks.get(taskId);
        if (task != null) {
            task.data = null;
            task.byteCount = byteCount;
            task.done = true;
        }
    }

    /**
     * Called when JS delivers a fetch error.
     */
    public void onFetchError(int taskId, int status) {
        NetTask task = tasks.get(taskId);
        if (task != null) {
            task.errorCode = status != 0 ? status : -1;
            task.done = true;
        }
    }

    private NetTask getTask(Integer taskId) {
        if (taskId == null || taskId == 0) {
            return lastTaskId > 0 ? tasks.get(lastTaskId) : null;
        }
        return tasks.get(taskId);
    }

    private String resolveUrl(String url) {
        if (url == null || url.isEmpty()) return url;

        // If already absolute, use as-is
        if (url.startsWith("http://") || url.startsWith("https://")) {
            return url;
        }

        // Extract just the filename (strip any path from the author's machine)
        // Handles /, \, and : (Mac-style Director paths like "Sulake:...:file.cct")
        String fileName = FileUtil.getFileName(url);

        // Resolve against basePath
        if (basePath != null && !basePath.isEmpty()) {
            String base = basePath;
            // Remove trailing filename from basePath if it looks like a file
            int baseSlash = base.lastIndexOf('/');
            if (baseSlash >= 0 && base.lastIndexOf('.') > baseSlash) {
                base = base.substring(0, baseSlash + 1);
            } else if (!base.endsWith("/")) {
                base = base + "/";
            }
            return base + fileName;
        }

        return fileName;
    }

    /**
     * A pending network request for JS to execute.
     * Uses a plain class instead of a record for TeaVM compatibility
     * (TeaVM may not correctly handle String[] fields in records).
     */
    public static class PendingRequest {
        public final int taskId;
        public final String url;
        public final String method;
        public final String postData;
        public final String[] fallbacks;

        public PendingRequest(int taskId, String url, String method, String postData, String[] fallbacks) {
            this.taskId = taskId;
            this.url = url;
            this.method = method;
            this.postData = postData;
            this.fallbacks = fallbacks;
        }
    }

    // Simple task data holder
    static class NetTask {
        final int id;
        final String url;
        byte[] data;
        int byteCount;
        int errorCode;
        boolean done;
        String[] fallbackUrls;
        int pollCount;

        NetTask(int id, String url) {
            this.id = id;
            this.url = url;
        }
    }
}
