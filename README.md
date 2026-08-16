# reMarkable AI Chat

Chat with any **OpenAI-compatible** model from a reMarkable 2 — **by
handwriting**, in your own messy handwriting.

Your ink is rendered to a cropped grayscale PNG and sent to the model as an
image. There is no on-device handwriting recognition, and none is needed:
vision models read bad handwriting far better than classical HWR. The model is
asked to begin each reply with `Read: "…"`, so your message bubble is replaced
by *what the model actually read* — immediate feedback on whether it
understood you — while the conversation history stays text-only, keeping
follow-ups cheap.

There is also a built-in on-screen keyboard for typed input and for entering
credentials, since the tablet has no system keyboard available to third-party
apps.

## Requires the loader

**This app does not start itself.** It has no icon, because reMarkable's UI
has no concept of third-party apps — only one process can own the e-paper
display, so an app replaces the stock UI rather than living inside it.

Install **[remarkable-loader](https://github.com/x3r081/remarkable-loader)**
first — it is what lets you reach this app at all.

> **How to open it:** put one finger in the **top-left** corner and another in
> the **bottom-right** corner at the same time, and hold ~1.2 s. The **Apps**
> page appears; tap **AI Chat**. The same gesture inside the app takes you
> back to the tablet.

Then:

1. Add this app to the loader's `switcher/apps.json`:

```json
{ "name": "AI Chat", "description": "Handwrite to any OpenAI-compatible model.", "exec": "systemctl start rm-chat.service" }
```

2. **Add it to `KNOWN_APPS` in the loader's `switcher/mode-toggle.sh`**
   (`rm_chat:rm-chat.service`). Skipping this makes the gesture start a second
   process that fights for the display lock, which crash-loops the stock UI
   and **reboots the device**.
3. Redeploy the loader: `./tools/switcher.py deploy`

This repo also vendors `common/penreader.*` and `common/penmouse.*` from the
loader (pen input), so it builds standalone.

## Build and deploy

Needs reMarkable's **Codex SDK** (see the loader's README) and Python 3 with
`pexpect`. No credentials live in this repo — the tools read `RM_PASSWORD`
from the environment or prompt for it.

```bash
./tools/build.sh rm-chat && ./tools/chatapp.py deploy
```

Then open it on the tablet: hold the **top-left and bottom-right corners**
together for ~1.2 s → **AI Chat**.

## Configuration — done on the tablet

First launch opens Settings. Enter with the on-screen keyboard:

| Field | Example |
|---|---|
| API base URL | `https://api.openai.com/v1` |
| API key | your key — stored only on the tablet, `0600` |
| Model | `gpt-4o-mini` (**must accept images** for handwriting) |

**Test** performs `GET /models` and reports the result before you commit.
The key field shows a live character count and a **Show key** button, because
a single mistyped character in a 164-character key produces an
indistinguishable "incorrect API key" error.

Typing a long key on e-paper is genuinely error-prone, so there is an
alternative that never puts the key in this repo or in a shell history:

```bash
./tools/chatapp.py setkey      # hidden prompt, writes straight to the tablet
./tools/chatapp.py selftest    # calls your real API from the tablet
```

`selftest` reports the stored key's **length and character makeup** — never
the key itself — plus the exact HTTP result, which distinguishes a mistyped
key from a wrong URL, an unverified model, or a billing problem.

Verified against OpenAI (`gpt-4o-mini`); the wire format is the portable
`image_url` base64 data-URI shape supported by OpenRouter, Groq, Together,
Gemini's OpenAI-compatible endpoint, llama.cpp and Ollama.

## Using it

Write in the pad with the pen and tap **Send**. **Clear** empties the pad,
**Keys** switches to typed input, **New** starts a fresh conversation
(history otherwise persists across restarts). API errors appear as bubbles in
the conversation. Holding the **top-left and bottom-right corners** exits to
the tablet — the app implements this itself as a backstop, so it can always
get home even if the loader's daemon is not running.

## Testing without hands or credits

```bash
./tools/chatapp.py test
```

Runs a fake OpenAI server on your PC (reached from the tablet through an SSH
reverse tunnel), creates a **virtual pen digitizer** on the device, scribbles
on it, auto-sends, and checks the reply lands in the conversation — the whole
pipeline without spending anything.

## Two e-paper details worth knowing

**Erasing.** The panel's fast partial waveform writes white→black but cannot
drive black back to white, and the backend only repaints where node *geometry*
changes — so erasing a painted item's contents leaves the old strokes on the
glass. Clearing therefore removes the canvas from the scene for a frame and
re-adds it empty, which is real damage the backend acts on. Do not "fix" this
by painting a black flash first: black sticks, leaving a box you cannot write
on. `common/epaper.*` can also force a true full refresh via reMarkable's own
`EPFramebuffer::ghostControl(BlinkNow)`, resolved lazily with `dlsym`; it is
**off by default** (`epaper_blink` in `config.json`).

**Pen.** The platform plugin does not deliver Marker events to third-party
apps, and the touch controller suppresses fingers while the pen is near the
glass — so a touch-only app looks frozen when you use the pen. This app reads
the Wacom digitizer directly and synthesizes mouse events.

## Privacy

Your API key is entered on the tablet (or pushed over USB with `setkey`) and
stored only at `/home/root/rm-chat/config.json`, owner-readable. It is never
logged: diagnostics report its length and character classes, never its value.
Handwriting images and conversation text go to whichever API endpoint you
configure, and nowhere else.

## Disclaimer

Not affiliated with reMarkable AS or OpenAI. Modifying your device is at your
own risk and may void warranty coverage.
