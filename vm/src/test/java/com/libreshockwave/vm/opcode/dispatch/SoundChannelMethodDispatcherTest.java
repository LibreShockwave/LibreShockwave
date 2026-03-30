package com.libreshockwave.vm.opcode.dispatch;

import com.libreshockwave.vm.builtin.media.SoundProvider;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class SoundChannelMethodDispatcherTest {

    @Test
    void volumePropertyRoundTripsThroughSoundProvider() {
        RecordingSoundProvider provider = new RecordingSoundProvider();
        SoundProvider.setProvider(provider);
        try {
            Datum.SoundChannel channel = new Datum.SoundChannel(3);

            assertTrue(SoundChannelMethodDispatcher.setProperty(channel, "volume", Datum.of(77)));
            assertEquals(3, provider.lastSetChannel);
            assertEquals(77, provider.lastSetVolume);
            assertEquals(77, SoundChannelMethodDispatcher.getProperty(channel, "volume").toInt());
        } finally {
            SoundProvider.clearProvider();
        }
    }

    private static final class RecordingSoundProvider implements SoundProvider {
        private int lastSetChannel = -1;
        private int lastSetVolume = -1;

        @Override
        public void play(int channelNum, Datum args) {
        }

        @Override
        public void stop(int channelNum) {
        }

        @Override
        public void stopAll() {
        }

        @Override
        public void setVolume(int channelNum, int volume) {
            lastSetChannel = channelNum;
            lastSetVolume = volume;
        }

        @Override
        public int getVolume(int channelNum) {
            return channelNum == lastSetChannel ? lastSetVolume : 255;
        }

        @Override
        public boolean isPlaying(int channelNum) {
            return false;
        }

        @Override
        public int getElapsedTime(int channelNum) {
            return 0;
        }
    }
}
