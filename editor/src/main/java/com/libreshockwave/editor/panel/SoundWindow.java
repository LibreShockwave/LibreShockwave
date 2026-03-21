package com.libreshockwave.editor.panel;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.audio.SoundConverter;
import com.libreshockwave.chunks.SoundChunk;
import com.libreshockwave.editor.EditorContext;
import com.libreshockwave.editor.model.CastMemberInfo;
import com.libreshockwave.editor.scanning.MemberResolver;

import javax.sound.sampled.*;
import javax.swing.*;
import java.awt.*;
import java.io.ByteArrayInputStream;

/**
 * Sound window - audio player for sound cast members.
 * Displays sound info and provides playback controls.
 */
public class SoundWindow extends EditorPanel {

    private final JTextArea infoArea;
    private final JLabel statusLabel;
    private final JButton playBtn;
    private final JButton stopBtn;
    private final JProgressBar progressBar;
    private final JLabel timeLabel;

    private Clip currentClip;
    private Timer playbackTimer;

    public SoundWindow(EditorContext context) {
        super("sound", "Sound", context, true, true, true, true);

        JPanel panel = new JPanel(new BorderLayout());

        // Playback controls toolbar
        JToolBar toolbar = new JToolBar();
        toolbar.setFloatable(false);

        playBtn = new JButton("Play");
        stopBtn = new JButton("Stop");
        stopBtn.setEnabled(false);
        progressBar = new JProgressBar(0, 100);
        progressBar.setPreferredSize(new Dimension(200, 20));
        timeLabel = new JLabel("0.0s / 0.0s");

        playBtn.setEnabled(false);
        playBtn.addActionListener(e -> play());
        stopBtn.addActionListener(e -> stop());

        toolbar.add(playBtn);
        toolbar.add(stopBtn);
        toolbar.addSeparator();
        toolbar.add(progressBar);
        toolbar.add(Box.createHorizontalStrut(8));
        toolbar.add(timeLabel);

        // Sound info
        infoArea = new JTextArea();
        infoArea.setEditable(false);
        infoArea.setFont(new Font("Monospaced", Font.PLAIN, 13));
        infoArea.setText("No sound member selected");

        statusLabel = new JLabel(" Ready");
        statusLabel.setBorder(BorderFactory.createLoweredBevelBorder());

        panel.add(toolbar, BorderLayout.NORTH);
        panel.add(new JScrollPane(infoArea), BorderLayout.CENTER);
        panel.add(statusLabel, BorderLayout.SOUTH);

        setContentPane(panel);
        setSize(400, 250);
    }

    private CastMemberInfo currentMember;

    public void loadMember(CastMemberInfo info) {
        stopPlayback();
        this.currentMember = info;

        DirectorFile dirFile = info.member().file();
        if (dirFile == null) dirFile = context.getFile();
        if (dirFile == null) return;

        SoundChunk soundChunk = MemberResolver.findSoundForMember(dirFile, info.member());
        String name = info.name() != null && !info.name().isEmpty() ? info.name() : "#" + info.memberNum();
        setTitle("Sound: " + name);
        if (soundChunk != null) {
            StringBuilder sb = new StringBuilder();
            sb.append("Codec: ").append(soundChunk.isMp3() ? "MP3" : "PCM (16-bit)").append("\n");
            sb.append("Sample Rate: ").append(soundChunk.sampleRate()).append(" Hz\n");
            sb.append("Bits Per Sample: ").append(soundChunk.bitsPerSample()).append("\n");
            sb.append("Channels: ").append(soundChunk.channelCount() == 1 ? "Mono" : "Stereo").append("\n");
            sb.append("Duration: ").append(String.format("%.2f", soundChunk.durationSeconds())).append(" seconds\n");
            sb.append("Audio Data Size: ").append(soundChunk.audioData().length).append(" bytes\n");
            infoArea.setText(sb.toString());
            playBtn.setEnabled(true);
            timeLabel.setText("0.0s / " + String.format("%.1fs", soundChunk.durationSeconds()));
            statusLabel.setText(" " + name);
        } else {
            infoArea.setText("[Sound data not found]");
            playBtn.setEnabled(false);
            statusLabel.setText(" No data");
        }
    }

    private void play() {
        stopPlayback();
        if (currentMember == null) return;

        DirectorFile dirFile = currentMember.member().file();
        if (dirFile == null) dirFile = context.getFile();
        if (dirFile == null) return;

        SoundChunk soundChunk = MemberResolver.findSoundForMember(dirFile, currentMember.member());
        if (soundChunk == null) return;

        try {
            byte[] audioData;
            if (soundChunk.isMp3()) {
                audioData = SoundConverter.extractMp3(soundChunk);
            } else {
                audioData = SoundConverter.toWav(soundChunk);
            }

            if (audioData == null || audioData.length <= 44) return;

            AudioInputStream audioStream = AudioSystem.getAudioInputStream(
                    new ByteArrayInputStream(audioData));

            AudioFormat baseFormat = audioStream.getFormat();
            if (baseFormat.getEncoding() != AudioFormat.Encoding.PCM_SIGNED) {
                AudioFormat decodedFormat = new AudioFormat(
                        AudioFormat.Encoding.PCM_SIGNED,
                        baseFormat.getSampleRate(), 16,
                        baseFormat.getChannels(),
                        baseFormat.getChannels() * 2,
                        baseFormat.getSampleRate(), false);
                audioStream = AudioSystem.getAudioInputStream(decodedFormat, audioStream);
            }

            currentClip = AudioSystem.getClip();
            currentClip.open(audioStream);
            currentClip.addLineListener(event -> {
                if (event.getType() == LineEvent.Type.STOP) {
                    SwingUtilities.invokeLater(this::onPlaybackFinished);
                }
            });

            playBtn.setEnabled(false);
            stopBtn.setEnabled(true);
            currentClip.start();
            statusLabel.setText(" Playing...");

            playbackTimer = new Timer(100, e -> updateProgress());
            playbackTimer.start();

        } catch (Exception ex) {
            statusLabel.setText(" Playback error: " + ex.getMessage());
        }
    }

    private void stop() {
        stopPlayback();
        statusLabel.setText(" Stopped");
    }

    private void onPlaybackFinished() {
        if (playbackTimer != null) playbackTimer.stop();
        playBtn.setEnabled(true);
        stopBtn.setEnabled(false);
        progressBar.setValue(0);
        statusLabel.setText(" Ready");
    }

    private void stopPlayback() {
        if (playbackTimer != null) {
            playbackTimer.stop();
            playbackTimer = null;
        }
        if (currentClip != null) {
            currentClip.stop();
            currentClip.close();
            currentClip = null;
        }
        playBtn.setEnabled(currentMember != null);
        stopBtn.setEnabled(false);
        progressBar.setValue(0);
    }

    private void updateProgress() {
        if (currentClip != null && currentClip.isRunning()) {
            long pos = currentClip.getMicrosecondPosition();
            long len = currentClip.getMicrosecondLength();
            progressBar.setValue((int) ((pos * 100.0) / len));
            timeLabel.setText(String.format("%.1fs / %.1fs", pos / 1_000_000.0, len / 1_000_000.0));
        }
    }

    @Override
    protected void onFileClosed() {
        stopPlayback();
        currentMember = null;
        infoArea.setText("No sound member selected");
        playBtn.setEnabled(false);
        progressBar.setValue(0);
        timeLabel.setText("0.0s / 0.0s");
        statusLabel.setText(" Ready");
    }

    @Override
    public void dispose() {
        stopPlayback();
        super.dispose();
    }
}
