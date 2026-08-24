# DodgerType

A distraction-free Markdown writing app for classic 68k Macintosh computers, built to run on any Mac capable of System 6.0.8 or newer — from a Mac Plus or SE up through later 68k machines — and to run from a [BlueSCSI](https://bluescsi.com) device where available.

![DodgerType running in Writer mode](screenshot1.png)

## About this fork

DodgerType began as [ActionRetro](https://www.youtube.com/c/ActionRetro)'s original ArtfulType, forked by Pascal Harris ([45RPM Software](https://www.45rpmsoftware.com)) as ArtfullerType for continued development, with implementation work done together with Claude (Anthropic).

The name has changed to DodgerType not to obscure that history, but because the codebase has diverged substantially from the original — printing support, a full preferences system, an expanded Markdown feature set, and a long list of stability fixes, detailed below. It will stay DodgerType unless ActionRetro would like these changes folded back into the original branch, in which case that's a conversation worth having.

## What's new since the fork

The original ArtfulType already had the core writing experience — Writer/Markdown modes, live formatting, links, undo/redo, zoom, save/open. Since forking, the following has been added or fixed:

**Printing** — Page Setup and Print, both fully working: correct pagination shared between print and the rest of the app, proper multi-page output, and text drawn at the printer's own resolution rather than the screen's.

**A real preferences system** — a plain-text, hand-editable preferences file (readable and editable in any text editor, deliberately not a binary resource format), covering default window/view mode, independent Writer and Markdown zoom levels, a configurable Markdown-mode font (chosen from a native font popup listing every installed font), line-ending translation, and optional syntax-color mode with its own colors for headings, links, emphasis, and code.

**Expanded Markdown support** — beyond the original bold/italic/code/headings/links, Writer mode now also renders:
- Underline
- Headings 1 through 6 (previously capped at 3)
- Blockquotes, including nested formatting inside them (a blockquote containing bold or a link renders correctly, not as plain text)
- Fenced code blocks, rendered in monospace with formatting inside them deliberately left unstyled — code shouldn't have its own contents reinterpreted as Markdown

The Style menu is reorganized into submenus (Text Format, Heading) to keep this from becoming an unwieldy flat list.

**Multi-document and window handling** — proper support for multiple open windows, a working title-bar zoom box and resizable windows with a working grow icon, a scrollbar that behaves correctly in both document and distraction-free views, and the app now tolerates having zero windows open rather than forcing a document to exist.

**A long list of stability fixes**, largely surfaced by testing on real hardware rather than an emulator: printing that produced blank pages, screen corruption and a stuck text cursor after printing, a save operation that could hang on System 6 (traced to a single large disk write — fixed by writing in smaller chunks), and several parsing hangs in the newer Markdown features (a blank line inside a fenced code block, most notably) that are now fixed.

**Icons and branding** — a proper application icon plus distinct icons for `.md` documents and the preferences file, and the DodgerType name itself.

## How this was built

Most of this work used specification-based development: a written specification (what needed to change and why) was turned into a sequence of milestone prompts, each reviewed before being handed to Claude to implement and verify against the actual toolchain and source, one milestone at a time. This moved faster than a purely back-and-forth conversational approach would have, though there was real conversation too — especially for debugging issues that could only be diagnosed by testing on actual hardware, where the cycle was closer to: describe the symptom, get a fix, test on real hardware, report back precisely what happened, repeat.

Neither of us could compile or run this code directly — every change was verified by hand against the real Retro68 toolchain's own headers and definitions, but the actual test was always Pascal building and running it on real Macintosh hardware.

## Getting Started

If your Mac can use [BlueSCSI](https://bluescsi.com), use the BlueSCSI image. If it can't (or you just want a physical floppy), use the 800K floppy image instead.

### Real hardware with BlueSCSI

1. Copy `HD1_DodgerType.hda` onto your BlueSCSI SD card — the `HD1_` prefix is BlueSCSI's naming convention for assigning an image to SCSI ID 1, so no renaming is needed. (See [BlueSCSI](https://bluescsi.com) for how to set up and image an SD card for your specific BlueSCSI hardware.)
2. Boot the Mac. The Finder will appear as usual — double-click DodgerType to launch it.
3. To also write a physical 800K floppy: open `Utilities/Disk Copy 4.2` (already on the disk image), and use it to write `DodgerType 800K` (also already on the disk image, in proper DiskCopy 4.2 format) to a blank floppy in your Mac's floppy drive.

### Real hardware without BlueSCSI

Write `DodgerType-800K.dsk` to a real 800K floppy disk and boot from it directly — no BlueSCSI required.

### In an emulator (Mini vMac)

For trying DodgerType without real hardware, use [Mini vMac](https://www.gryphel.com/c/minivmac/) configured for a Mac Plus, with either:
- `DodgerType-20MB.dsk` — the full HD setup (System 7.1, stripped down, with the app, Disk Copy, and the embedded floppy image)
- `DodgerType-800K.dsk` — a bootable 800K floppy (System 6.0.8) with just the app

## Usage

DodgerType has two views, toggled from the View menu:

- **Writer** (default) — markdown syntax is hidden; text is shown styled (bold, italic, headings, etc.)
- **Markdown** — the raw markdown source, unstyled (or syntax-colored, if color mode is enabled in Preferences)

Saved files are plain `.md` text, editable in any text editor.

### Keyboard shortcuts

| Action | Shortcut |
|---|---|
| New / Open / Save | ⌘N / ⌘O / ⌘S |
| Page Setup / Print | — / ⌘P |
| Quit | ⌘Q |
| Undo / Redo | ⌘Z / ⇧⌘Z |
| Cut / Copy / Paste | ⌘X / ⌘C / ⌘V |
| Bold / Italic / Underline / Code | ⌘B / ⌘I / ⌘U / ⌘K |
| Heading 1–6 | ⌘1 – ⌘6 |
| Link | ⌘L |
| Zoom In / Out / Default | ⌘= / ⌘- / ⌘0 |

Strikethrough, Blockquote, and Code Block are available from the Style menu but don't have keyboard shortcuts.

## Building

Built with [Retro68](https://github.com/autc04/Retro68), a GCC-based cross-compiler for classic Mac OS. See `app/CMakeLists.txt` for the build configuration, and `deploy.sh` / `build-bluescsi-image.sh` / `package-release.sh` for the build-to-disk-image pipeline.

## Credits

- **[ActionRetro](https://www.youtube.com/c/ActionRetro)** — original creator of ArtfulType.
- **Pascal Harris, [45RPM Software](https://www.45rpmsoftware.com)** — fork maintainer and ongoing development.
- **Claude (Anthropic)** — implementation work throughout this fork.

## License

Code: GPLv3 — see [LICENSE](LICENSE).

Creative assets (the DodgerType name/branding, icon, and artwork): all rights reserved — see [ASSETS_LICENSE](ASSETS_LICENSE).

## AI Disclaimer

Claude was used extensively in the development of this fork, both for implementation and for the specification-based development process described above. See [How this was built](#how-this-was-built).
