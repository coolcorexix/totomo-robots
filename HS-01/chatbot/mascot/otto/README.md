# Otto eyes

The six 240×240 animated eye GIFs the chatbot shows on the HS-01 screen, from
[txp666/otto-emoji-gif-component](https://github.com/txp666/otto-emoji-gif-component)
(MIT, see `LICENSE`). Extracted from `../states.html`, which maps each face to a
firmware state.

| GIF | Shown when |
|---|---|
| `neutral.gif` | standby (connected, waiting for a tap) |
| `surprised.gif` | listening |
| `thinking.gif` | thinking (transcript in, reply not started) |
| `happy.gif` | speaking |
| `sad.gif` | offline / reconnecting |
| `confused.gif` | server `alert` |

After changing a GIF, regenerate the firmware arrays:

    python3 mascot/otto/gen_otto_gifs.py      # from HS-01/chatbot
