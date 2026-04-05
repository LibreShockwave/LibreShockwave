package com.libreshockwave.editor.preview;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.chunks.SoundChunk;
import com.libreshockwave.editor.model.CastMemberInfo;
import com.libreshockwave.editor.model.FrameAppearance;
import com.libreshockwave.editor.scanning.MemberResolver;
import com.libreshockwave.editor.score.FrameAppearanceFinder;

import java.util.List;

/**
 * Generates sound preview content.
 */
public class SoundPreview {

    private final FrameAppearanceFinder appearanceFinder = new FrameAppearanceFinder();

    /**
     * Formats sound details for display.
     */
    public String format(DirectorFile dirFile, CastMemberInfo memberInfo) {
        SoundChunk soundChunk = MemberResolver.findSoundForMember(dirFile, memberInfo.member());

        StringBuilder sb = new StringBuilder();
        PreviewFormatUtils.appendMemberHeader(sb, "SOUND", memberInfo, true);

        if (soundChunk != null) {
            sb.append("--- Audio Properties ---\n");
            sb.append("Codec: ").append(soundChunk.isMp3() ? "MP3" : "PCM (16-bit)").append("\n");
            sb.append("Sample Rate: ").append(soundChunk.sampleRate()).append(" Hz\n");
            sb.append("Bits Per Sample: ").append(soundChunk.bitsPerSample()).append("\n");
            sb.append("Channels: ").append(soundChunk.channelCount() == 1 ? "Mono" : "Stereo").append("\n");
            sb.append("Duration: ").append(String.format("%.2f", soundChunk.durationSeconds())).append(" seconds\n");
            sb.append("Audio Data Size: ").append(soundChunk.audioData().length).append(" bytes\n");
        } else {
            sb.append("[Sound data not found]\n");
        }

        // Show frame appearances from score
        sb.append("\n--- Score Appearances ---\n");
        List<FrameAppearance> appearances = appearanceFinder.find(dirFile, memberInfo.memberNum());
        PreviewFormatUtils.appendScoreAppearances(sb, appearances, appearanceFinder, false);

        return sb.toString();
    }

    /**
     * Returns true if the sound chunk exists and is playable.
     */
    public boolean isPlayable(DirectorFile dirFile, CastMemberInfo memberInfo) {
        SoundChunk soundChunk = MemberResolver.findSoundForMember(dirFile, memberInfo.member());
        return soundChunk != null;
    }
}
