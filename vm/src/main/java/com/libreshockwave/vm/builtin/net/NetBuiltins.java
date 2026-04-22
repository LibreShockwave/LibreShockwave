package com.libreshockwave.vm.builtin.net;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.builtin.movie.MoviePropertyProvider;

import java.util.List;
import java.util.Map;
import java.util.function.BiFunction;

/**
 * Network builtin functions for Lingo.
 * Similar to dirplayer-rs player/handlers/net.rs.
 *
 * These require a NetManager to be registered via the VM context.
 * The actual implementation is delegated to the registered NetManager.
 */
public final class NetBuiltins {

    private NetBuiltins() {}

    /**
     * Interface for network operations.
     * Implemented by NetManager in player-core.
     */
    public interface NetProvider {
        int preloadNetThing(String url);
        int postNetText(String url, String postData);
        boolean netDone(Integer taskId);
        String netTextResult(Integer taskId);
        int netError(Integer taskId);
        String getStreamStatus(Integer taskId);

        /**
         * Get stream status as a PropList with #bytesSoFar, #bytesTotal, #state, #error keys.
         * Director's getStreamStatus() returns a PropList, not a string.
         */
        default Datum getStreamStatusDatum(Integer taskId) {
            String status = getStreamStatus(taskId);
            var props = new java.util.LinkedHashMap<String, Datum>();
            props.put("URL", Datum.EMPTY_STRING);
            props.put("state", Datum.of(status));
            props.put("bytesSoFar", Datum.ZERO);
            props.put("bytesTotal", Datum.ZERO);
            props.put("error", Datum.of("OK"));
            return Datum.propList(props);
        }
    }

    // Thread-local provider for VM access
    private static final ThreadLocal<NetProvider> currentProvider = new ThreadLocal<>();

    /**
     * Set the network provider for the current thread.
     * Call this before executing scripts that use network functions.
     */
    public static void setProvider(NetProvider provider) {
        currentProvider.set(provider);
    }

    /**
     * Clear the network provider for the current thread.
     */
    public static void clearProvider() {
        currentProvider.remove();
    }

    public static void register(Map<String, BiFunction<LingoVM, List<Datum>, Datum>> builtins) {
        builtins.put("preloadnetthing", NetBuiltins::preloadNetThing);
        builtins.put("getnetthing", NetBuiltins::preloadNetThing);  // Alias
        builtins.put("getnettext", NetBuiltins::preloadNetThing);   // Alias - same async download
        builtins.put("postnetthing", NetBuiltins::postNetText);
        builtins.put("postnettext", NetBuiltins::postNetText);
        builtins.put("netdone", NetBuiltins::netDone);
        builtins.put("nettextresult", NetBuiltins::netTextResult);
        builtins.put("neterror", NetBuiltins::netError);
        builtins.put("getstreamstatus", NetBuiltins::getStreamStatus);
        builtins.put("gotonetpage", NetBuiltins::gotoNetPage);
        builtins.put("gotonetmovie", NetBuiltins::gotoNetMovie);
    }

    /**
     * preloadNetThing(util)
     * Starts an async network request and returns a task ID.
     */
    private static Datum preloadNetThing(LingoVM vm, List<Datum> args) {
        NetProvider provider = currentProvider.get();
        if (provider == null) {
            System.err.println("[NetBuiltins] No NetProvider registered");
            return Datum.of(-1);
        }

        if (args.isEmpty()) {
            return Datum.of(-1);
        }

        String url = args.get(0).toStr();
        int taskId = provider.preloadNetThing(url);
        return Datum.of(taskId);
    }

    /**
     * postNetText(util, postData)
     * Starts an async POST request and returns a task ID.
     */
    private static Datum postNetText(LingoVM vm, List<Datum> args) {
        NetProvider provider = currentProvider.get();
        if (provider == null) {
            System.err.println("[NetBuiltins] No NetProvider registered");
            return Datum.of(-1);
        }

        if (args.isEmpty()) {
            return Datum.of(-1);
        }

        String url = args.get(0).toStr();
        String postData = args.size() > 1 ? args.get(1).toStr() : "";
        int taskId = provider.postNetText(url, postData);
        return Datum.of(taskId);
    }

    private static Datum gotoNetPage(LingoVM vm, List<Datum> args) {
        MoviePropertyProvider provider = MoviePropertyProvider.getProvider();
        if (provider == null) {
            return Datum.FALSE;
        }

        if (args.isEmpty()) {
            return Datum.FALSE;
        }

        String url = args.get(0).toStr();
        String target = args.size() > 1 ? args.get(1).toStr() : "";
        provider.gotoNetPage(url, target);
        return Datum.TRUE;
    }

    private static Datum gotoNetMovie(LingoVM vm, List<Datum> args) {
        MoviePropertyProvider provider = MoviePropertyProvider.getProvider();
        if (provider == null || args.isEmpty()) {
            return Datum.of(-1);
        }

        String url = args.get(0).toStr();
        int requestId = provider.gotoNetMovie(url);
        return Datum.of(requestId);
    }

    /**
     * netDone(taskId)
     * Returns 1 if the network request is complete, 0 otherwise.
     */
    private static Datum netDone(LingoVM vm, List<Datum> args) {
        NetProvider provider = currentProvider.get();
        if (provider == null) {
            return Datum.TRUE;  // No provider = pretend done
        }

        Integer taskId = args.isEmpty() ? null : args.get(0).toInt();
        boolean done = provider.netDone(taskId);
        return done ? Datum.TRUE : Datum.FALSE;
    }

    /**
     * netTextResult(taskId)
     * Returns the text result of a completed network request.
     */
    private static Datum netTextResult(LingoVM vm, List<Datum> args) {
        NetProvider provider = currentProvider.get();
        if (provider == null) {
            return Datum.EMPTY_STRING;
        }

        Integer taskId = args.isEmpty() ? null : args.get(0).toInt();
        String result = provider.netTextResult(taskId);
        return Datum.of(result);
    }

    /**
     * netError(taskId)
     * Director returns "OK" for success, error code as string for errors.
     * Lingo scripts compare: netError(id) = "OK"
     */
    private static Datum netError(LingoVM vm, List<Datum> args) {
        NetProvider provider = currentProvider.get();
        if (provider == null) {
            return Datum.of("OK");
        }

        Integer taskId = args.isEmpty() ? null : args.get(0).toInt();
        int error = provider.netError(taskId);
        return error == 0 ? Datum.of("OK") : Datum.of(String.valueOf(error));
    }

    /**
     * getStreamStatus(taskId)
     * Returns a PropList: [#URL: url, #state: status, #bytesSoFar: n, #bytesTotal: n, #error: "OK"]
     * Director returns a PropList, not a string. Scripts access tStreamStatus[#bytesSoFar].
     */
    private static Datum getStreamStatus(LingoVM vm, List<Datum> args) {
        NetProvider provider = currentProvider.get();
        if (provider == null) {
            var props = new java.util.LinkedHashMap<String, Datum>();
            props.put("URL", Datum.EMPTY_STRING);
            props.put("state", Datum.of("Error"));
            props.put("bytesSoFar", Datum.ZERO);
            props.put("bytesTotal", Datum.ZERO);
            props.put("error", Datum.of("OK"));
            return Datum.propList(props);
        }

        Integer taskId = args.isEmpty() ? null : args.get(0).toInt();
        return provider.getStreamStatusDatum(taskId);
    }
}
