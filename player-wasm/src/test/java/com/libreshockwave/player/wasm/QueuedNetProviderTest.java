package com.libreshockwave.player.wasm;

import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class QueuedNetProviderTest {

    @Test
    void externalDataCacheMatchesBasenameAfterExtensionFetch() {
        QueuedNetProvider provider =
                new QueuedNetProvider("https://example.invalid/client/movie.dcr");

        int firstTask = provider.preloadNetThing("external_asset.cct");
        assertEquals(1, provider.getPendingRequests().size());
        provider.drainPendingRequests();

        byte[] bytes = new byte[] {1, 2, 3};
        provider.onFetchComplete(firstTask, bytes);

        int secondTask = provider.preloadNetThing("external_asset");
        assertTrue(provider.netDone(secondTask));
        assertEquals(0, provider.getPendingRequests().size());
        assertEquals("Complete", provider.getStreamStatus(secondTask));
    }

    @Test
    void emptyPreloadCompletesWithoutQueuingFetch() {
        QueuedNetProvider provider =
                new QueuedNetProvider("https://example.invalid/client/movie.dcr");

        int task = provider.preloadNetThing("");

        assertTrue(provider.netDone(task));
        assertEquals(0, provider.getPendingRequests().size());
        assertEquals("Complete", provider.getStreamStatus(task));
    }

    @Test
    void directoryPreloadCompletesWithoutQueuingFetch() {
        QueuedNetProvider provider =
                new QueuedNetProvider("https://example.invalid/client/movie.dcr");

        int task = provider.preloadNetThing("https://example.invalid/client/");

        assertTrue(provider.netDone(task));
        assertEquals(0, provider.getPendingRequests().size());
        assertEquals("Complete", provider.getStreamStatus(task));
    }

    @Test
    void satisfiedFetchPredicateCompletesWithoutQueuingFetch() {
        QueuedNetProvider provider =
                new QueuedNetProvider("https://example.invalid/client/movie.dcr");
        provider.setSatisfiedFetchPredicate(url -> url.endsWith("/embedded_cast.cct"));

        int task = provider.preloadNetThing("embedded_cast");

        assertTrue(provider.netDone(task));
        assertEquals(0, provider.getPendingRequests().size());
        assertEquals("Complete", provider.getStreamStatus(task));
    }

    @Test
    void streamStatusIncludesTaskUrl() {
        QueuedNetProvider provider =
                new QueuedNetProvider("https://example.invalid/client/movie.dcr");

        int task = provider.preloadNetThing("external_asset.cct");

        Datum.PropList status = (Datum.PropList) provider.getStreamStatusDatum(task);
        assertEquals("https://example.invalid/client/external_asset.cct", status.get("URL").toStr());
        assertEquals("Loading", status.get("state").toStr());
    }

    @Test
    void streamStatusCanBeLookedUpByUrl() {
        QueuedNetProvider provider =
                new QueuedNetProvider("https://example.invalid/client/movie.dcr");

        int task = provider.preloadNetThing("external_asset.cct");
        provider.onFetchComplete(task, new byte[] {1, 2, 3});

        Datum.PropList status = (Datum.PropList) provider.getStreamStatusDatum("https://example.invalid/client/external_asset.cct");
        assertEquals("Complete", status.get("state").toStr());
        assertEquals(3, status.get("bytesSoFar").toInt());
        assertEquals(3, status.get("bytesTotal").toInt());
    }

    @Test
    void directoryStreamStatusByUrlCompletesWithoutTask() {
        QueuedNetProvider provider =
                new QueuedNetProvider("https://example.invalid/client/movie.dcr");

        Datum.PropList status = (Datum.PropList) provider.getStreamStatusDatum("https://example.invalid/client/");

        assertEquals("Complete", status.get("state").toStr());
        assertEquals("https://example.invalid/client/", status.get("URL").toStr());
    }
}
