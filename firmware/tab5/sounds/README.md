# Your own alert sounds

A WAV file here replaces one of the Tab5's alert sounds in **your** build; any sound without a file stays synthesised. The step-by-step instructions (getting and converting the sound, building, checking it) are in the main README, under [Theme sounds](../../../README.md#theme-sounds).

| File | Plays when |
|---|---|
| `classic-attention.wav`, `anime-attention.wav`, `lcars-attention.wav`, `galactic-attention.wav` | a tile asks for a permission in that theme, and when you switch to that theme |
| `classic-failure.wav`, `anime-failure.wav`, `lcars-failure.wav`, `galactic-failure.wav` | a tile shows an error in that theme |

Uncompressed PCM, 8 or 16 bits, mono or stereo, 4 to 192 kHz; mono at 32 kHz is plenty:

```
ffmpeg -i red-alert.mp3 -t 3 -ac 1 -ar 32000 -c:a pcm_s16le lcars-attention.wav
```

Everything here but this README is gitignored (the WAV files and whatever you converted them from). Recordings of films and TV shows belong to their owners: use them on your own device, and keep them out of the repository, the releases and anything you publish. CI and the published images are built without them.
