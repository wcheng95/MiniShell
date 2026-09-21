#!/usr/bin/env python3
"""Compile the actual ADV worker against threaded host transport/RTOS fakes."""
import pathlib
import subprocess
import tempfile
import sys
root = pathlib.Path(sys.argv[1]).resolve()
source = (root / 'platform/adv/adv_audio_speaker.cpp').read_text()
worker = source[source.index('/* T043:'):source.index('}  // namespace')]
# Keep the ordinary PCM implementation's direct-I2S call as a separate assertion.
pcm = source[source.index('mini_result_t speaker_write'):source.index('mini_result_t speaker_stop')]
assert 'adv_audio_tx_write(write_i2s, s_i2s_tx' in pcm and 'esp_codec_dev_write(' not in pcm
assert 'xTaskCreate(' in worker and 'xTaskCreatePinnedToCore' not in worker
assert 'dma_desc_num = 4;' in source and 'dma_frame_num = 120;' in source
with tempfile.TemporaryDirectory() as temp:
    directory = pathlib.Path(temp)
    (directory / 'adv_tone_worker.inc').write_text(worker)
    subprocess.run(['cc', '-std=c11', '-I' + str(root / 'include'), '-c',
                    str(root / 'platform/common/tone_stream.c'), '-o', str(directory / 'tone.o')], check=True)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
                    '-I' + str(directory), '-I' + str(root / 'include'), '-I' + str(root / 'platform/common'),
                    str(root / 'tests/adv_tone_worker_test.cpp'), str(directory / 'tone.o'), '-lm',
                    '-o', str(directory / 'test')], check=True)
    subprocess.run([str(directory / 'test')], check=True, timeout=20)
print('ADV tone worker: continuous writes/prime/lifecycle/failures/PCM separation PASS')
