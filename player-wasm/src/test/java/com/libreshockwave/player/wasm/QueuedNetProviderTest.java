package com.libreshockwave.player.wasm;

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
}
