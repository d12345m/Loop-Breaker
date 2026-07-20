#!/usr/bin/env python3
"""
Generate the Loop Breaker User Manual as a PDF.
Uses fpdf2 for PDF creation.
"""

from fpdf import FPDF
import os
import datetime

OUTPUT_PATH = os.path.join(
    os.path.dirname(__file__), "docs", "user-guide", "Loop_Breaker_User_Manual.pdf"
)

_VERSION_FILE = os.path.join(os.path.dirname(__file__), "VERSION")

def _read_version() -> str:
    """Return the release version from the VERSION file, falling back to '1.0.0'."""
    try:
        with open(_VERSION_FILE) as _f:
            return _f.read().strip()
    except OSError:
        return "1.0.0"


os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)


class ManualPDF(FPDF):
    """Custom FPDF subclass for the Loop Breaker manual."""

    def __init__(self):
        super().__init__()
        self.set_auto_page_break(auto=True, margin=25)
        self.set_left_margin(25)
        self.set_right_margin(25)
        self.toc_entries = []  # (level, title, page_number)
        self._chapter_num = 0

    # ── Header / Footer ──────────────────────────────────────────────────

    def header(self):
        if self.page_no() <= 1:
            return  # cover page has no header
        self.set_font("Helvetica", "I", 8)
        self.set_text_color(120, 120, 120)
        self.cell(0, 10, "Loop Breaker User Manual", align="L")
        self.cell(0, 10, "Glow Machine, LLC", align="R", new_x="LMARGIN", new_y="NEXT")
        self.set_draw_color(200, 200, 200)
        self.line(self.l_margin, self.get_y(), self.w - self.r_margin, self.get_y())
        self.ln(4)

    def footer(self):
        if self.page_no() <= 1:
            return
        self.set_y(-20)
        self.set_font("Helvetica", "I", 8)
        self.set_text_color(150, 150, 150)
        self.cell(0, 10, f"Page {self.page_no()}", align="C")

    # ── Helpers ───────────────────────────────────────────────────────────

    def chapter_title(self, title, level=1):
        """Print a chapter/section heading and register it in the TOC."""
        if level == 1:
            self._chapter_num += 1
            numbered = f"{self._chapter_num}. {title.upper()}"
            self.set_font("Helvetica", "B", 18)
            self.ln(6)
        elif level == 2:
            numbered = title
            self.set_font("Helvetica", "B", 14)
            self.ln(4)
        else:
            numbered = title
            self.set_font("Helvetica", "B", 12)
            self.ln(2)

        self.toc_entries.append((level, numbered if level == 1 else title, self.page_no()))
        self.set_text_color(30, 30, 30)
        self.multi_cell(0, 8, numbered, new_x="LMARGIN", new_y="NEXT")

        if level == 1:
            self.set_draw_color(80, 80, 80)
            self.line(self.l_margin, self.get_y(), self.w - self.r_margin, self.get_y())
            self.ln(4)
        else:
            self.ln(2)

    def body_text(self, text):
        """Print body text."""
        self.set_font("Helvetica", "", 10.5)
        self.set_text_color(50, 50, 50)
        self.multi_cell(0, 5.5, text, new_x="LMARGIN", new_y="NEXT")
        self.ln(2)

    def bullet(self, text, indent=5):
        """Print a bullet point with a filled circle."""
        self.set_font("Helvetica", "", 10.5)
        self.set_text_color(50, 50, 50)
        lm = self.l_margin
        bullet_x = lm + indent
        bullet_y = self.get_y() + 2.2  # vertically centre in first line
        # draw small filled circle as bullet
        self.set_fill_color(50, 50, 50)
        self.ellipse(bullet_x, bullet_y, 1.5, 1.5, style="F")
        # text starts after bullet
        text_x = bullet_x + 5
        self.set_x(text_x)
        text_w = self.w - self.r_margin - text_x
        self.multi_cell(text_w, 5.5, text, new_x="LMARGIN", new_y="NEXT")
        self.ln(1)

    def key_value_row(self, key, value):
        """Print a key-value row for tables."""
        self.set_font("Helvetica", "B", 10)
        self.set_text_color(50, 50, 50)
        col_w = 55
        x_start = self.get_x()
        y_start = self.get_y()
        self.multi_cell(col_w, 5.5, key, new_x="RIGHT", new_y="TOP")
        y_after_key = self.get_y()
        self.set_xy(x_start + col_w + 4, y_start)
        self.set_font("Helvetica", "", 10)
        self.multi_cell(0, 5.5, value, new_x="LMARGIN", new_y="NEXT")
        y_after_val = self.get_y()
        self.set_y(max(y_after_key, y_after_val))
        self.ln(1)

    def note_box(self, text):
        """Print a note/tip box."""
        self.ln(2)
        w = self.w - self.l_margin - self.r_margin
        pad = 6
        self.set_font("Helvetica", "I", 10)
        # Measure the height the text will need
        full_text = f"NOTE: {text}"
        line_h = 5
        h = self.multi_cell(w - pad * 2, line_h, full_text, dry_run=True, output="HEIGHT") + pad * 2
        y = self.get_y()
        # Draw a single filled + bordered rectangle
        self.set_fill_color(240, 245, 255)
        self.set_draw_color(100, 140, 200)
        self.rect(self.l_margin, y, w, h, style="DF")
        # Write the text inside the box
        self.set_xy(self.l_margin + pad, y + pad)
        self.set_text_color(40, 60, 100)
        self.multi_cell(w - pad * 2, line_h, full_text, new_x="LMARGIN", new_y="NEXT")
        self.set_y(y + h + 4)

    def tip_box(self, text):
        """Print a tip box."""
        self.ln(2)
        w = self.w - self.l_margin - self.r_margin
        pad = 6
        self.set_font("Helvetica", "I", 10)
        full_text = f"TIP: {text}"
        line_h = 5
        h = self.multi_cell(w - pad * 2, line_h, full_text, dry_run=True, output="HEIGHT") + pad * 2
        y = self.get_y()
        # Draw a single filled + bordered rectangle
        self.set_fill_color(240, 255, 240)
        self.set_draw_color(80, 160, 80)
        self.rect(self.l_margin, y, w, h, style="DF")
        # Write the text inside the box
        self.set_xy(self.l_margin + pad, y + pad)
        self.set_text_color(30, 80, 30)
        self.multi_cell(w - pad * 2, line_h, full_text, new_x="LMARGIN", new_y="NEXT")
        self.set_y(y + h + 4)

    def table_header(self, cols, widths):
        """Print a table header row."""
        self.set_font("Helvetica", "B", 10)
        self.set_fill_color(230, 230, 230)
        self.set_text_color(30, 30, 30)
        for i, col in enumerate(cols):
            self.cell(widths[i], 7, col, border=1, fill=True, align="C")
        self.ln()

    def table_row(self, cols, widths, alt=False):
        """Print a table data row."""
        self.set_font("Helvetica", "", 9.5)
        self.set_text_color(50, 50, 50)
        if alt:
            self.set_fill_color(248, 248, 248)
        else:
            self.set_fill_color(255, 255, 255)
        for i, col in enumerate(cols):
            self.cell(widths[i], 6.5, col, border=1, fill=True)
        self.ln()


def build_manual():
    pdf = ManualPDF()
    effective_width = pdf.w - pdf.l_margin - pdf.r_margin

    # ═══════════════════════════════════════════════════════════════════════
    # COVER PAGE
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.ln(50)
    pdf.set_font("Helvetica", "B", 36)
    pdf.set_text_color(20, 20, 20)
    pdf.cell(0, 15, "Loop Breaker", align="C", new_x="LMARGIN", new_y="NEXT")
    pdf.ln(4)
    pdf.set_font("Helvetica", "", 16)
    pdf.set_text_color(100, 100, 100)
    pdf.cell(0, 10, "User Manual", align="C", new_x="LMARGIN", new_y="NEXT")
    pdf.ln(8)
    pdf.set_font("Helvetica", "", 12)
    pdf.cell(0, 8, "VST3 & AU Audio Plugin", align="C", new_x="LMARGIN", new_y="NEXT")
    pdf.ln(30)
    pdf.set_draw_color(180, 180, 180)
    x_center = pdf.w / 2
    pdf.line(x_center - 40, pdf.get_y(), x_center + 40, pdf.get_y())
    pdf.ln(10)
    pdf.set_font("Helvetica", "", 11)
    pdf.set_text_color(80, 80, 80)
    pdf.cell(0, 7, "Glow Machine, LLC", align="C", new_x="LMARGIN", new_y="NEXT")
    pdf.cell(0, 7, "www.glowmachineaudio.com", align="C", new_x="LMARGIN", new_y="NEXT")
    pdf.ln(30)
    pdf.set_font("Helvetica", "I", 9)
    pdf.set_text_color(150, 150, 150)
    today = datetime.date.today().strftime("%B %Y")
    _ver = _read_version()
    pdf.cell(0, 6, f"v{_ver}  |  {today}", align="C", new_x="LMARGIN", new_y="NEXT")
    pdf.cell(0, 6, "Copyright (c) 2025-2026 Glow Machine, LLC. All rights reserved.",
             align="C", new_x="LMARGIN", new_y="NEXT")

    # ═══════════════════════════════════════════════════════════════════════
    # TABLE OF CONTENTS (placeholder pages — filled in after all content)
    # ═══════════════════════════════════════════════════════════════════════
    # Reserve 3 full pages for the TOC so it never bleeds into chapter 1.
    toc_pages = []
    for tp in range(3):
        pdf.add_page()
        toc_pages.append(pdf.page_no())
    toc_start_page = toc_pages[0]

    # ═══════════════════════════════════════════════════════════════════════
    # 1. INTRODUCTION
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("Introduction")

    pdf.body_text(
        "Welcome to Loop Breaker, a creative loop-mangling audio plugin from Glow Machine, LLC. "
        "Loop Breaker combines an MPC-style sampler pad bank with a game-like modifier system that "
        "automatically transforms your loops in musically interesting ways."
    )

    pdf.body_text(
        "Load up to 8 audio loops onto the pad grid, start your DAW's transport, and watch as the "
        "modifier engine randomly applies effects, slices, pitch shifts, speed changes, and more to "
        "your selected pads. The result is an ever-evolving, generative remix of your source material "
        "that is perfect for live performance, sound design, or creative exploration."
    )

    pdf.body_text(
        "To get the most out of Loop Breaker, we recommend reading this manual in its entirety. "
        "However, if you're eager to dive in, jump straight to the Quick Start section."
    )

    pdf.chapter_title("Key Features", level=2)
    features = [
        "8 stereo audio buffer pads with waveform display and loop progress indicators",
        "Automatic modifier system that randomly transforms loops at configurable bar intervals",
        "24 modifier types spanning buffer transforms, channel effects, master effects, and special operations",
        "Per-modifier probability sliders with DAW automation and MIDI CC control",
        "Per-pad target probability to weight which pads receive modifiers",
        "Global probability presets: save and recall named probability configurations across projects",
        "8 modifier preset snapshot slots (A-H) for instant recall of complex states",
        "Multi-output routing: 8 independent stereo output buses plus a master mix bus",
        "Full MIDI control: pad selection, modifier toggle, and preset recall via MIDI notes",
        "Drag-and-drop sample loading from your OS file browser",
        "Parts system: divide loops into 1-4 equal sections for structured arrangements",
        "Multiple visual themes",
        "Complete DAW state save and restore",
    ]
    for f in features:
        pdf.bullet(f)

    # ═══════════════════════════════════════════════════════════════════════
    # 2. SYSTEM REQUIREMENTS
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("System Requirements")

    pdf.chapter_title("Supported Formats", level=2)
    pdf.body_text("Loop Breaker is distributed as a VST3 plugin and an Audio Unit (AU) plugin on macOS.")

    pdf.chapter_title("Platform", level=2)
    pdf.bullet("macOS 10.13 or later (Intel or Apple Silicon) - VST3 and AU")
    pdf.bullet("A DAW that supports VST3 or AU instruments (Ableton Live, Logic Pro, Reaper, FL Studio, Bitwig, GarageBand, etc.)")

    pdf.chapter_title("Audio File Formats", level=2)
    pdf.body_text(
        "Loop Breaker can load the following sample formats via drag-and-drop or the file chooser:"
    )
    pdf.bullet("WAV (.wav)")
    pdf.bullet("AIFF (.aif, .aiff)")
    pdf.bullet("FLAC (.flac)")
    pdf.bullet("MP3 (.mp3)")

    pdf.chapter_title("Important: DAW Buffer Size", level=2)
    pdf.note_box(
        "Loop Breaker performs real-time time-stretching and pitch-shifting on the audio thread. "
        "These DSP operations require large, contiguous blocks of samples. Set your DAW's audio "
        "buffer size to the LARGEST available value (typically 2048 or 4096 samples). Using a "
        "small buffer (e.g. 64 or 128 samples) will cause audible glitching, clicking, and "
        "tearing when speed or pitch modifiers are active. In most DAWs this setting is found under: "
        "Preferences > Audio > Buffer Size (or Block Size / Device Buffer)."
    )

    # ═══════════════════════════════════════════════════════════════════════
    # 3. INSTALLATION
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("Installation")

    # ── macOS Installer ──────────────────────────────────────────────────
    pdf.chapter_title("macOS", level=2)
    pdf.body_text(
        "Run the Loop Breaker .pkg installer. The installer places the VST3 and AU bundles into "
        "the standard system plugin folders:"
    )
    pdf.set_font("Courier", "", 10)
    pdf.set_text_color(50, 50, 50)
    pdf.cell(0, 6, "    /Library/Audio/Plug-Ins/VST3/LoopBreaker.vst3", new_x="LMARGIN", new_y="NEXT")
    pdf.cell(0, 6, "    /Library/Audio/Plug-Ins/Components/LoopBreaker.component", new_x="LMARGIN", new_y="NEXT")
    pdf.ln(3)
    pdf.body_text(
        "After installation, relaunch your DAW and scan for new plugins. Loop Breaker will appear "
        "in the Instrument category under \"Glow Machine Audio\". The AU version is available natively "
        "in Logic Pro and GarageBand without any additional configuration."
    )

    # ── macOS Gatekeeper ─────────────────────────────────────────────────
    pdf.chapter_title("Allowing an Unsigned Plugin on macOS", level=2)
    pdf.body_text(
        "Because Loop Breaker is not currently code-signed or notarized by Apple, "
        "macOS Gatekeeper will block it the first time you try to use it."
    )
    pdf.body_text(
        "macOS Sequoia 15 and later / Ventura 13 / Sonoma 14: When your DAW fails to load the "
        "plugin (or a dialog appears saying the file \"can't be opened\"), dismiss the dialog. "
        "Open System Settings > Privacy & Security. Scroll down and look for a message like: "
        "\"LoopBreaker was blocked from use because it is not from an identified developer.\" "
        "Click \"Allow Anyway\" and authenticate with your password or Touch ID. Re-open your DAW "
        "or rescan plugins and click \"Open\" when prompted. You may need to repeat this for both "
        "the .vst3 and .component bundles."
    )
    pdf.body_text(
        "macOS Monterey 12 and earlier: Open System Preferences > Security & Privacy > General tab. "
        "You should see \"LoopBreaker was blocked...\" Click \"Allow Anyway\" and authenticate. "
        "Re-open your DAW and confirm when prompted."
    )
    pdf.body_text(
        "If the above does not work, you can remove the quarantine flag manually by running the "
        "following command in Terminal:"
    )
    pdf.set_font("Courier", "", 8)
    pdf.set_text_color(50, 50, 50)
    pdf.cell(0, 6, "  xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/LoopBreaker.vst3", new_x="LMARGIN", new_y="NEXT")
    pdf.cell(0, 6, "  xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/Components/LoopBreaker.component", new_x="LMARGIN", new_y="NEXT")
    pdf.ln(3)
    pdf.body_text(
        "Adjust the paths if you installed to ~/Library/... instead."
    )

    # ── Windows ──────────────────────────────────────────────────────────
    pdf.chapter_title("Windows", level=2)
    pdf.body_text(
        "Copy the LoopBreaker.vst3 file to the standard VST3 folder:"
    )
    pdf.set_font("Courier", "", 10)
    pdf.set_text_color(50, 50, 50)
    pdf.cell(0, 6, "    C:\\Program Files\\Common Files\\VST3\\", new_x="LMARGIN", new_y="NEXT")
    pdf.ln(3)
    pdf.body_text(
        "You may need administrator privileges to copy files into this folder. "
        "After copying, open your DAW and rescan plugins if necessary. Loop Breaker will appear "
        "in the plugin list under the manufacturer \"Glow Machine\"."
    )
    pdf.body_text(
        "Note: Some DAWs also support a per-user VST3 folder. Check your DAW's documentation "
        "if the system-wide folder does not work for your setup."
    )

    # ── Uninstallation ───────────────────────────────────────────────────
    pdf.chapter_title("Uninstallation", level=2)
    pdf.body_text(
        "macOS: Delete the LoopBreaker.vst3 bundle from /Library/Audio/Plug-Ins/VST3/ "
        "and the LoopBreaker.component bundle from /Library/Audio/Plug-Ins/Components/ "
        "(or the ~/Library/... equivalents if installed per-user). Alternatively, run the "
        "included uninstall.sh script."
    )
    pdf.body_text(
        "Windows: Delete the LoopBreaker.vst3 file from "
        "C:\\Program Files\\Common Files\\VST3\\."
    )

    # ═══════════════════════════════════════════════════════════════════════
    # 4. QUICK START
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("Quick Start")

    pdf.body_text(
        "Follow these steps to get up and running quickly:"
    )

    steps = [
        ("Load Samples", "Drag WAV, AIFF, FLAC, or MP3 files from Finder onto any pad in the 2x4 grid. "
         "Alternatively, right-click an empty pad to open a file chooser dialog."),
        ("Start the DAW Transport", "Loop Breaker follows the host timeline and tempo. Set the BPM and press Play in your DAW and "
         "all loaded buffers will begin looping. Each pad displays a waveform with a moving playhead."),
        ("Select Target Pads", "Click pads to toggle their selection on or off. Selected pads glow and will "
         "be targeted by the next modifier. If no pads are selected when the modifier timer fires, "
         "1-4 pads are chosen automatically."),
        ("Watch the Modifier Display", "The upcoming modifier name and variant details appear in the "
         "Session tab header. A progress bar and countdown show when the next modifier will fire."),
        ("Adjust Probabilities", "Switch to the Probability tab to control how likely each modifier type "
         "is to appear. Set a slider to 0 to disable that modifier entirely."),
    ]
    for i, (title, desc) in enumerate(steps, 1):
        pdf.set_font("Helvetica", "B", 11)
        pdf.set_text_color(30, 30, 30)
        pdf.cell(0, 7, f"Step {i}: {title}", new_x="LMARGIN", new_y="NEXT")
        pdf.body_text(desc)

    pdf.tip_box(
        "Use the Master Volume knob in the Session tab to compensate if the default per-pad "
        "headroom reduction (-12 dB) makes the mix too quiet."
    )

    # ═══════════════════════════════════════════════════════════════════════
    # 5. USER INTERFACE OVERVIEW
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("User Interface Overview")

    pdf.body_text(
        "The Loop Breaker plugin window is organized into a tabbed layout with four main tabs:"
    )

    tabs = [
        ("Session", "The primary performance view. Contains the pad grid, upcoming modifier display, "
         "modifier toggle, master volume knob, and preset buttons (A-H)."),
        ("Probability", "Per-modifier probability sliders grouped by category (Buffer, Channel Effect, "
         "Master Effect, Special). Also includes per-pad target probability sliders. All sliders are "
         "exposed as DAW automation parameters."),
        ("Settings", "Appearance theme selection, Parts configuration, and Bars-per-Modifier timing control."),
        ("Help", "Built-in reference documentation for all plugin features, controls, and modifier types."),
    ]
    for name, desc in tabs:
        pdf.set_font("Helvetica", "B", 11)
        pdf.set_text_color(30, 30, 30)
        pdf.multi_cell(0, 7, name, new_x="LMARGIN", new_y="NEXT")
        pdf.body_text(desc)

    # ═══════════════════════════════════════════════════════════════════════
    # 6. SESSION TAB
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("Session Tab")

    pdf.body_text(
        "The Session tab is the main performance view and the default tab when the plugin opens."
    )

    # -- Pad Grid --
    pdf.chapter_title("Pad Grid", level=2)
    pdf.body_text(
        "The 2x4 pad grid represents the 8 audio buffers. Each pad displays the loaded "
        "sample's filename, a waveform thumbnail, and a playhead indicator showing the "
        "current loop position. Pads change color to indicate their state:"
    )
    pdf.bullet("Empty: dark/unlit - no sample loaded")
    pdf.bullet("Loaded: standard color - sample loaded, not selected")
    pdf.bullet("Selected: highlighted/glowing - will be targeted by next modifier")
    pdf.bullet("Playing: animated indicator when the transport is running")

    pdf.chapter_title("Pad Interactions", level=3)
    interactions = [
        ("Click", "Toggle pad selection on / off"),
        ("Right-click", "Open context menu (Load Sample, Remove Sample, MIDI Learn, Clear MIDI Note)"),
        ("Drag & drop file onto pad", "Load audio file into that pad"),
        ("Shift + Click", "Enter MIDI learn mode for this pad; play a MIDI note to assign it; click again to cancel"),
        ("Cmd + Click (macOS)", "Clear the MIDI note assignment for this pad"),
        ("Alt + Click", "Clear the MIDI note assignment for this pad (alias)"),
        ("Shift + Cmd + Click (macOS)", "Remove the loaded sample from this pad"),
    ]
    for key, val in interactions:
        pdf.key_value_row(key, val)

    # -- Upcoming Modifier Display --
    pdf.chapter_title("Upcoming Modifier Display", level=2)
    pdf.body_text(
        "Located in the session header area, this display shows the name and variant details of "
        "the next modifier that will be applied. A progress bar and countdown (in bars/seconds) "
        "indicate how much time remains before the modifier fires. When the modifier triggers, "
        "the targeted pads flash to provide visual feedback."
    )

    # -- Modifiers Toggle --
    pdf.chapter_title("Modifiers Toggle", level=2)
    pdf.body_text(
        "This button enables or disables the modifier scheduling system entirely. When disabled, "
        "no modifiers will be applied and the countdown pauses. The toggle supports MIDI note "
        "assignment via right-click context menu or Shift+Click (MIDI Learn) / Cmd+Click (clear)."
    )

    # -- Master Volume --
    pdf.chapter_title("Master Volume Knob", level=2)
    pdf.body_text(
        "A rotary knob that controls the overall output level from -12 dB to +12 dB. "
        "This gain is applied equally to all pads and all output buses. The parameter is "
        "exposed for DAW automation."
    )

    # -- Preset Buttons --
    pdf.chapter_title("Modifier Preset Buttons (A-H)", level=2)
    pdf.body_text(
        "Eight preset slots allow you to snapshot and recall the complete modifier state across "
        "all 8 pads. This captures buffer transform parameters (speed, stretch, pitch, slicing, "
        "ping-pong) and all channel effects settings (reverb, delay, filter, tremolo, chorus, "
        "auto-pan, volume ramp)."
    )
    preset_interactions = [
        ("Click (empty slot)", "Save the current modifier states into this preset"),
        ("Click (filled slot)", "Recall the saved modifier states from this preset"),
        ("Double-click", "Force-save (overwrite) the current states into this preset"),
        ("Right-click", "Open context menu: Save, Recall, Clear, MIDI Learn, Clear MIDI"),
        ("Shift + Click", "Enter MIDI learn mode for this preset slot"),
        ("Cmd + Click (macOS)", "Clear the MIDI note assignment for this preset"),
    ]
    for key, val in preset_interactions:
        pdf.key_value_row(key, val)

    pdf.note_box(
        "Presets do NOT save or restore Parts settings, pad file assignments, probability weights, "
        "or timing configuration. Presets are saved with your DAW session and restored on reload."
    )

    # ═══════════════════════════════════════════════════════════════════════
    # 7. PROBABILITY TAB
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("Probability Tab")

    pdf.body_text(
        "The Probability tab gives you fine-grained control over which modifiers appear and "
        "which pads are targeted."
    )

    pdf.chapter_title("Modifier Probability Sliders", level=2)
    pdf.body_text(
        "Each modifier type has a probability slider ranging from 0.0 to 1.0. Set a slider to "
        "0 to completely disable that modifier. All enabled sliders are normalized automatically: "
        "a modifier set to 2.0 is twice as likely to appear as one set to 1.0."
    )
    pdf.body_text(
        "Sliders are grouped by category: Buffer, Channel Effect, Master Effect, and Special. "
        "Each slider is exposed as a DAW automation parameter."
    )

    pdf.chapter_title("MIDI CC Control", level=2)
    pdf.body_text(
        "Each probability slider can be mapped to a MIDI CC for hands-on hardware control. "
        "Right-click any slider to open a context menu with two options:"
    )
    pdf.bullet("MIDI CC Learn - enters learn mode (the label beside the slider shows \"LEARN\" "
               "in the accent color). Move any CC knob or fader on your controller and the "
               "assignment is made instantly.")
    pdf.bullet("Clear CC - removes the current CC assignment (shows the current CC number "
               "if one is assigned).")
    pdf.body_text(
        "Once mapped, the CC number appears beside the slider (e.g. \"CC74\"). "
        "Assignments are saved with the DAW session. This works for both the modifier "
        "probability sliders and the pad target probability sliders, allowing you to drive "
        "any probability value from external hardware or automation lanes."
    )

    pdf.chapter_title("Pad Target Probability", level=2)
    pdf.body_text(
        "At the bottom of the Probability tab, a section of 8 sliders (Pad 1 through Pad 8) "
        "controls the likelihood that each pad will be automatically selected as a modifier "
        "target. A value of 1.0 means the pad is always eligible; 0.0 means it will never be "
        "auto-selected. These sliders also support DAW automation and MIDI CC learn."
    )

    pdf.chapter_title("Reset All to 100%", level=2)
    pdf.body_text(
        "A button at the bottom of the panel resets all probability sliders to their default "
        "value of 1.0 (100%)."
    )

    pdf.chapter_title("Probability Presets", level=2)
    pdf.body_text(
        "Probability Presets let you save and recall entire probability configurations "
        "(all modifier weights and pad target probabilities) as named presets that persist "
        "globally across all projects and DAW sessions. Presets are stored as individual JSON "
        "files in your user Application Support folder, so they are available regardless of "
        "which DAW project is open."
    )
    pdf.body_text(
        "A preset bar at the top of the Probability tab provides quick access:"
    )
    pdf.bullet("Preset dropdown -- Select a saved preset to load it instantly. All modifier "
               "probability sliders and pad target probability sliders are updated to match "
               "the saved values.")
    pdf.bullet("Save -- Overwrite the currently selected preset with the current probability "
               "settings. If no preset is selected, this behaves like Save As.")
    pdf.bullet("Save As -- Save the current settings under a new name. A dialog prompts you "
               "to enter a name. If a preset with that name already exists, you are asked to "
               "confirm the overwrite.")
    pdf.bullet("Delete -- Delete the currently selected preset. A confirmation dialog is "
               "shown before the preset is permanently removed.")
    pdf.body_text(
        "Probability Presets do NOT include MIDI CC mappings, timing settings, or any other "
        "session-level configuration -- only the modifier and pad probability slider values."
    )

    # ═══════════════════════════════════════════════════════════════════════
    # 8. SETTINGS TAB
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("Settings Tab")

    pdf.chapter_title("Theme", level=2)
    pdf.body_text(
        "Choose from 11 built-in visual themes. The theme change crossfades smoothly over 500ms. "
        "Available themes:"
    )
    themes = [
        "Arctic Sky", "Daylight", "Gruvbox", "Ivory", "Neon Rave (Dark)",
        "Pixel Grid", "Silver", "Studio Clean", "Ultraviolet", "Vintage Ember", "Warm Paper"
    ]
    for t in themes:
        pdf.bullet(t)

    pdf.chapter_title("Parts", level=2)
    pdf.body_text(
        "Split each buffer into 1 to 4 equal sections (labeled A through D). This allows "
        "you to work with structured musical arrangements. The Part change takes effect on "
        "the next modifier trigger when the transport is running. The \"Switch Part\" modifier "
        "type will automatically move between parts when enabled."
    )

    pdf.chapter_title("Bars / Modifier", level=2)
    pdf.body_text(
        "This slider (range: 1 to 16 bars) controls how many bars elapse between each "
        "modifier application. At 1 bar, modifiers fire rapidly for chaotic, fast-evolving "
        "textures. At 16 bars, changes are slow and gradual. The default is 4 bars."
    )

    # ═══════════════════════════════════════════════════════════════════════
    # 9. MODIFIERS REFERENCE
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("Modifiers Reference")

    pdf.body_text(
        "Loop Breaker's modifier system is at the heart of its creative engine. Every N bars "
        "(configured in the Settings tab), a randomly chosen modifier is applied to the "
        "user-selected pads. If no pads are selected, 1-4 pads are chosen automatically."
    )

    pdf.body_text(
        "Modifiers fall into four categories:"
    )

    # -- Buffer Transforms --
    pdf.chapter_title("Buffer Transforms", level=2)
    pdf.body_text(
        "These modifiers directly alter how the audio buffer is played back."
    )

    buffer_mods = [
        ("Reverse", "Flips the playback direction of the buffer. If the buffer is already playing in reverse, "
         "it returns to forward playback."),
        ("Speed", "Changes the playback rate to one of four discrete values: 0.25x, 0.5x, 1.0x, or 2.0x. "
         "Speed changes also affect the buffer's pitch."),
        ("Stretch", "A time-stretching variant that changes tempo without affecting pitch. "
         "Available ratios include 0.25x, 0.5x, 1.0x, and 2.0x."),
        ("Pitch Up Octave", "Raises the pitch of the buffer by one octave (+12 semitones)."),
        ("Pitch Down Octave", "Lowers the pitch of the buffer by one octave (-12 semitones)."),
        ("Beat Slice", "Subdivides the buffer into note-length slices (1/4, 1/8, 1/8T, 1/16, 1/32, 1/64) "
         "and plays them in a randomized order, creating glitchy, rearranged patterns."),
        ("Arp Slice", "Divides the buffer into a variable number of slices and plays short, "
         "repeating arpeggio-like sequences from those slices. The arpeggio changes every few bars."),
        ("Slice Repeater", "Selects a single slice from the buffer and stutters/repeats it "
         "a variable number of times before choosing a new slice to repeat."),
        ("Ping Pong", "Alternates playback direction on each loop cycle at a musical note division "
         "(whole note, half note, quarter note, eighth note, or sixteenth note)."),
    ]
    for name, desc in buffer_mods:
        pdf.set_font("Helvetica", "B", 10.5)
        pdf.set_text_color(30, 30, 30)
        pdf.multi_cell(0, 6, name, new_x="LMARGIN", new_y="NEXT")
        pdf.body_text(desc)

    # -- Channel Effects --
    pdf.chapter_title("Channel Effects", level=2)
    pdf.body_text(
        "These modifiers enable or adjust effects on individual buffer channel strips."
    )

    channel_mods = [
        ("Delay", "Enables the delay effect on the buffer's channel strip. Variants include "
         "different delay time divisions (1/4, 1/8, 1/8D, 1/8T, 1/16), wet mix levels, "
         "feedback amounts, optional ping-pong mode, and wow/flutter modulation."),
        ("Delay Dub Burst", "A special delay variant that creates an intense burst of delay feedback "
         "for a short duration, then fades back."),
        ("Reverb", "Enables reverb on the channel strip with a varaiable wet/dry mix "
         "(25%, 50%, 75%, or 100%) and a fade-in duration (instant, 1 bar, 2 bars, etc.)."),
        ("Low-Pass Filter", "Enables a low-pass filter that gradually closes over the configured "
         "duration, darkening the sound."),
        ("High-Pass Filter", "Enables a high-pass filter that gradually opens over the configured "
         "duration, thinning the low end."),
        ("Volume Ramp Down", "Gradually fades the buffer volume down over a number of bars before returning to normal."),
        ("Tremolo", "Applies volume modulation (LFO) to the buffer, creating a rhythmic "
         "pulsing effect."),
        ("Chorus", "Enables a chorus effect with configurable depth, rate, and wet/dry mix "
         "for a thicker, wider sound."),
        ("Auto-Pan", "Modulates the stereo panning of the buffer with a configurable LFO rate "
         "and depth, creating movement in the stereo field."),
        ("S&H Low-Pass", "A persistent low-pass filter with sample-and-hold modulation. "
         "Unlike the standard low-pass filter which ramps up and back down, this filter stays "
         "active until removed by the Reset modifier. The cutoff frequency and resonance (Q) "
         "are randomly re-triggered at a musical rate -- every 16th note, 8th note, or quarter "
         "note -- creating a rhythmic, stepped filtering effect. The cutoff range is 200 Hz to "
         "8000 Hz and the Q range is 0.5 to 4.0."),
        ("S&H High-Pass", "A persistent high-pass filter with sample-and-hold modulation. "
         "Like the S&H Low-Pass, this filter stays active until removed by the Reset modifier. "
         "The cutoff and Q are randomly re-triggered at a musical rate (16th, 8th, or quarter "
         "note), producing a rhythmic, stepped thinning effect. The cutoff range is 60 Hz to "
         "800 Hz and the Q range is 0.5 to 4.0."),
        ("Granular", "A Clouds-inspired granular texture effect. Captures incoming audio into a "
         "short buffer and re-synthesizes it as overlapping grains with randomized position, "
         "pitch variance, and stereo spread. Parameters include grain density (2-24 grains/sec), "
         "grain size (15-200ms), pitch spread (0-12 semitones), wet/dry mix, and texture "
         "(smooth Hann to sharp rectangular window). This is a permanent effect that remains "
         "active until Reset is triggered."),
        ("Granular Burst", "A temporary version of the Granular effect that fades in over half "
         "the configured duration, then fades back out. Duration options are 2, 4, 8, or 16 bars. "
         "All other granular parameters (density, size, pitch spread, mix, texture) are randomized "
         "independently."),
    ]
    for name, desc in channel_mods:
        pdf.set_font("Helvetica", "B", 10.5)
        pdf.set_text_color(30, 30, 30)
        pdf.multi_cell(0, 6, name, new_x="LMARGIN", new_y="NEXT")
        pdf.body_text(desc)

    # -- Master Effects --
    pdf.chapter_title("Master Effects", level=2)
    pdf.body_text(
        "These modifiers affect the master output and are applied regardless of pad selection."
    )

    master_mods = [
        ("Master High-Pass", "Applies a high-pass filter to the master output, thinning the overall mix. "
         "Can ramp up then back down, or jump immediately and ramp back."),
        ("Master Low-Pass", "Applies a low-pass filter to the master output, darkening the overall mix. "
         "Behaves similarly to the Master High-Pass with configurable ramp behavior."),
    ]
    for name, desc in master_mods:
        pdf.set_font("Helvetica", "B", 10.5)
        pdf.set_text_color(30, 30, 30)
        pdf.multi_cell(0, 6, name, new_x="LMARGIN", new_y="NEXT")
        pdf.body_text(desc)

    # -- Special --
    pdf.chapter_title("Special Modifiers", level=2)

    special_mods = [
        ("Switch Part", "Moves all targeted buffers to a different Part (A-D). Only available "
         "when Parts is set to more than 1 in the Settings tab."),
        ("Quarter-Note Burst", "Triggers rapid-fire modifier applications at quarter-note intervals "
         "for a variable number of bars (1, 2, or 4), creating intense bursts of change."),
        ("Swap Modifier Stack", "Swaps the entire modifier stack (speed, pitch, effects, slicing, etc.) "
         "between two or more targeted buffers. When two buffers are selected they swap directly; "
         "with three or more the stacks rotate so each buffer receives another's settings. "
         "If no buffers are selected, 2-4 are chosen at random."),
        ("Reset All", "Removes all active modifiers, turns off all effects, and returns playback "
         "speed, pitch, and direction to their default state."),
    ]
    for name, desc in special_mods:
        pdf.set_font("Helvetica", "B", 10.5)
        pdf.set_text_color(30, 30, 30)
        pdf.multi_cell(0, 6, name, new_x="LMARGIN", new_y="NEXT")
        pdf.body_text(desc)

    # ═══════════════════════════════════════════════════════════════════════
    # 10. MIDI CONTROL
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("MIDI Control")

    pdf.body_text(
        "Loop Breaker accepts MIDI note input for pad selection, modifier toggling, and preset "
        "recall. MIDI CC input is used for probability slider control."
    )

    pdf.chapter_title("Default MIDI Note Map", level=2)
    pdf.body_text(
        "By default, pads are mapped to MIDI notes matching the General MIDI drum layout:"
    )

    widths = [30, 30, 50, 50]
    pdf.table_header(["Note #", "Name", "Pad", "Position"], widths)
    midi_rows = [
        ["36", "C1", "Pad 1", "Bottom-left"],
        ["37", "C#1", "Pad 2", "Bottom row"],
        ["38", "D1", "Pad 3", "Bottom row"],
        ["39", "D#1", "Pad 4", "Bottom-right"],
        ["40", "E1", "Pad 5", "Top-left"],
        ["41", "F1", "Pad 6", "Top row"],
        ["42", "F#1", "Pad 7", "Top row"],
        ["43", "G1", "Pad 8", "Top-right"],
    ]
    for i, row in enumerate(midi_rows):
        pdf.table_row(row, widths, alt=(i % 2 == 1))

    pdf.ln(4)
    pdf.body_text(
        "Note behavior: Note-on toggles the pad selection. Note-off is ignored. Velocity is ignored."
    )

    pdf.chapter_title("MIDI Learn", level=2)
    pdf.body_text(
        "To reassign a pad to a different MIDI note: Shift+Click the pad (or right-click and "
        "choose \"MIDI Learn\"), then play the desired note on your controller. The new mapping "
        "is saved with the DAW session."
    )
    pdf.body_text(
        "To clear a MIDI note assignment: Cmd+Click (macOS) or Alt+Click, or right-click and "
        "choose \"Clear MIDI Note\"."
    )

    pdf.chapter_title("Modifier Toggle via MIDI", level=2)
    pdf.body_text(
        "The Modifiers on/off toggle button can also be assigned to a MIDI note. Right-click "
        "the toggle and choose MIDI Learn, or Shift+Click it. Use Cmd+Click to clear the assignment."
    )

    pdf.chapter_title("Preset Recall via MIDI", level=2)
    pdf.body_text(
        "Each preset slot (A-H) can be assigned to a MIDI note for instant recall from a "
        "hardware controller. Use the right-click context menu or Shift+Click on a preset button "
        "to enter MIDI Learn mode."
    )

    pdf.chapter_title("MIDI CC for Probabilities", level=2)
    pdf.body_text(
        "In the Probability tab, click the CC button next to any slider to enter MIDI CC learn "
        "mode. Move a knob or fader on your controller to assign it. This lets you drive modifier "
        "probabilities with hardware faders, LFOs, or DAW MIDI automation."
    )

    pdf.tip_box(
        "Loop Breaker is compatible with most drum pad controllers (Akai MPD, NI Maschine, "
        "Novation Launchpad, etc.) and any standard MIDI keyboard."
    )

    # ═══════════════════════════════════════════════════════════════════════
    # 11. MULTI-OUTPUT ROUTING
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("Multi-Output Routing")

    pdf.body_text(
        "Loop Breaker exposes 8 independent stereo output buses (plus a master mix bus) "
        "for a total of up to 18 output channels."
    )

    pdf.chapter_title("Output Bus Layout", level=2)
    widths_out = [50, 60, 50]
    pdf.table_header(["Bus", "Channels", "Source"], widths_out)
    out_rows = [
        ["Master Mix", "1-2", "Sum of all pads"],
        ["Output 1", "3-4", "Pad 1"],
        ["Output 2", "5-6", "Pad 2"],
        ["Output 3", "7-8", "Pad 3"],
        ["Output 4", "9-10", "Pad 4"],
        ["Output 5", "11-12", "Pad 5"],
        ["Output 6", "13-14", "Pad 6"],
        ["Output 7", "15-16", "Pad 7"],
        ["Output 8", "17-18", "Pad 8"],
    ]
    for i, row in enumerate(out_rows):
        pdf.table_row(row, widths_out, alt=(i % 2 == 1))

    pdf.ln(4)
    pdf.body_text(
        "To use multi-output routing, enable the additional output buses on the plugin instance "
        "in your DAW. Each pad's audio is then routed to its own mixer channel, allowing "
        "independent processing, effects, and mixing within the DAW."
    )

    pdf.body_text(
        "If the DAW only activates a subset of buses, or does not support multi-output, "
        "the remaining buffers are automatically folded into the master mix output."
    )

    # ═══════════════════════════════════════════════════════════════════════
    # 12. DAW AUTOMATION
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("DAW Automation")

    pdf.body_text(
        "Loop Breaker exposes the following parameters for DAW automation:"
    )

    pdf.chapter_title("Automatable Parameters", level=2)
    pdf.bullet("Master Volume (-12 dB to +12 dB)")
    pdf.bullet("Per-modifier probability sliders (one for each of the 24 modifier types)")
    pdf.bullet("Per-pad target probability sliders (Pad 1 through Pad 8)")
    pdf.ln(2)

    pdf.body_text(
        "These parameters appear in your DAW's automation lane selector under the Loop Breaker "
        "plugin. You can draw automation curves, use LFOs, or link them to MIDI CC via the "
        "plugin's built-in learn feature."
    )

    pdf.chapter_title("State Save & Restore", level=2)
    pdf.body_text(
        "All plugin state is saved and restored with the DAW session, including:"
    )
    save_items = [
        "Loaded sample file paths (samples are reloaded from disk on session open)",
        "Pad MIDI note assignments",
        "Modifier toggle MIDI note",
        "Preset MIDI note assignments",
        "All probability slider positions and MIDI CC mappings",
        "Pad target probability positions and MIDI CC mappings",
        "Modifier preset snapshots (A-H)",
        "Parts configuration (number of parts, active part)",
        "Bars-between-modifiers setting",
        "Theme selection",
        "Modifiers enabled/disabled state",
    ]
    for item in save_items:
        pdf.bullet(item)

    # ═══════════════════════════════════════════════════════════════════════
    # 13. SAMPLE LOADING
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("Sample Loading")

    pdf.chapter_title("Drag & Drop", level=2)
    pdf.body_text(
        "Drag one or more audio files from Finder directly onto the pad grid. If you drag "
        "multiple files at once, they will be loaded sequentially starting from the pad you "
        "drop onto."
    )

    pdf.chapter_title("File Chooser", level=2)
    pdf.body_text(
        "Click an empty pad to open a file chooser dialog. Navigate to your sample and click Open. "
        "You can also right-click any pad and select \"Load Sample\" from the context menu."
    )

    pdf.chapter_title("Removing a Sample", level=2)
    pdf.body_text(
        "Right-click the pad and choose \"Remove Sample\", or use Shift+Cmd+Click (macOS)."
    )

    pdf.note_box(
        "Sample loading is performed off the audio thread. You can load new files at any time "
        "without interrupting playback. If a loaded file goes missing (e.g. sample drive unmounted), "
        "the pad shows an error state and outputs silence."
    )

    # ═══════════════════════════════════════════════════════════════════════
    # 14. PARTS SYSTEM
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("Parts System")

    pdf.body_text(
        "The Parts system allows you to divide each audio buffer into 1 to 4 equal-length "
        "sections, labeled A through D. This is useful when your loops have distinct musical "
        "sections (e.g. verse, chorus, bridge, outro) that you want to switch between."
    )

    pdf.chapter_title("Configuring Parts", level=2)
    pdf.body_text(
        "In the Settings tab, use the Parts dropdown to select 1 part (default), 2 parts, 3 parts, "
        "or 4 parts. The change takes effect on the next modifier trigger when the transport is running or immediately if the transport is stopped."
    )

    pdf.chapter_title("Switching Parts", level=2)
    pdf.body_text(
        "The \"Switch Part\" modifier (visible in the Probability tab under the Special category) "
        "will automatically move buffers to a different part when it fires. This modifier is only "
        "available when Parts is set to more than 1."
    )

    # ═══════════════════════════════════════════════════════════════════════
    # 15. THEMES
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("Themes")

    pdf.body_text(
        "Loop Breaker includes 11 built-in visual themes that change the entire color palette "
        "of the plugin interface. Themes crossfade smoothly over 500 milliseconds when switched."
    )

    pdf.chapter_title("Available Themes", level=2)
    theme_descs = [
        ("Neon Rave (Dark)", "The default theme. Dark background with vibrant neon accent colors. Ideal for dimly-lit studios."),
        ("Arctic Sky", "Cool blue tones with a clean, icy aesthetic."),
        ("Daylight", "Bright, light theme with warm neutral tones for well-lit environments."),
        ("Gruvbox", "Inspired by the popular Gruvbox color scheme. Warm, retro earth tones."),
        ("Ivory", "Minimal, elegant off-white palette."),
        ("Pixel Grid", "Retro digital aesthetic with pixel-art-inspired colors."),
        ("Silver", "Sleek metallic gray palette."),
        ("Studio Clean", "Professional, neutral tones designed for mixing sessions."),
        ("Ultraviolet", "Deep purples and electric blues for a futuristic vibe."),
        ("Vintage Ember", "Warm amber and brown tones with a vintage analog feel."),
        ("Warm Paper", "Soft, paper-like tones for a gentle reading experience."),
    ]
    for name, desc in theme_descs:
        pdf.set_font("Helvetica", "B", 10.5)
        pdf.set_text_color(30, 30, 30)
        pdf.multi_cell(0, 6, name, new_x="LMARGIN", new_y="NEXT")
        pdf.body_text(desc)

    # ═══════════════════════════════════════════════════════════════════════
    # 16. TIPS & KNOWN ISSUES
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("Tips & Known Issues")

    pdf.chapter_title("Tips", level=2)
    tips = [
        "Start with the Bars / Modifier slider at 4 bars, then experiment with shorter intervals for more chaos or longer intervals for subtle evolution.",
        "Use the Reset All modifier (ensure its probability is non-zero) as a natural recovery mechanism that periodically returns your loops to their original state.",
        "Assign modifier probability sliders to MIDI CC on your controller to perform live probability adjustments during a set.",
        "Use multi-output routing to apply DAW-side effects (like EQ or compression) to individual pads independently.",
        "Save modifier presets (A-H) for different \"scenes\" during a performance and trigger them via MIDI for instant transitions.",
        "Lower the probability of extreme modifiers (Pitch Up/Down, Beat Slice) if you want more subtle, musical evolution.",
    ]
    for t in tips:
        pdf.bullet(t)

    pdf.chapter_title("Known Issues", level=2)
    issues = [
        "Octave pitch-shifting may produce audible artifacts at extreme settings. Staying within +/-1 octave typically sounds cleanest.",
        "Small DAW buffer sizes (< 512 samples) may cause tearing or glitching with time-stretch effects. Use 2048+ for best results.",
    ]
    for issue in issues:
        pdf.bullet(issue)

    # ═══════════════════════════════════════════════════════════════════════
    # 17. TECHNICAL INFORMATION
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("Technical Information")

    pdf.chapter_title("Plugin Format", level=2)
    widths_tech = [50, effective_width - 50]
    pdf.table_header(["Property", "Value"], widths_tech)
    tech_rows = [
        ["Format", "VST3"],
        ["Category", "Instrument (Synth)"],
        ["MIDI Input", "Yes"],
        ["MIDI Output", "No"],
        ["Manufacturer", "Glow Machine Audio"],
        ["Plugin Code", "TsPl"],
        ["Manufacturer Code", "GloM"],
        ["AU Export Prefix", "LoopBreakerAU"],
        ["AAX Identifier", "com.glowmachineaudio.LoopBreaker"],
    ]
    for i, row in enumerate(tech_rows):
        pdf.table_row(row, widths_tech, alt=(i % 2 == 1))

    # ═══════════════════════════════════════════════════════════════════════
    # 18. CREDITS & CONTACT
    # ═══════════════════════════════════════════════════════════════════════
    pdf.add_page()
    pdf.chapter_title("Credits & Contact Information")

    pdf.chapter_title("Credits", level=2)
    pdf.body_text("Loop Breaker is developed by Glow Machine, LLC.")
    pdf.ln(2)
    pdf.body_text("Third-party libraries:")
    pdf.bullet("JUCE Framework - JUCE is copyright Raw Material Software Limited.")
    pdf.bullet("SoundTouch Audio Processing Library - used for real-time time-stretching and pitch-shifting. SoundTouch is copyright Olli Parviainen.")

    pdf.chapter_title("Contact", level=2)
    pdf.body_text("Glow Machine, LLC")
    pdf.body_text("Website: www.glowmachineaudio.com")
    pdf.body_text("For support, bug reports, and feature requests, please visit our website.")
    pdf.ln(4)

    pdf.set_font("Helvetica", "I", 9)
    pdf.set_text_color(130, 130, 130)
    pdf.multi_cell(0, 5,
        "Loop Breaker User Manual. This manual is copyright (c) 2025-2026 Glow Machine, LLC. "
        "All rights reserved. All reproduction, digital or printed, without written authorization "
        "is strictly prohibited. The information in this manual may change without notice. "
        "JUCE is a trademark of Raw Material Software Limited. All other brand or product names are "
        "trademarks or registered trademarks of their respective holders.",
        new_x="LMARGIN", new_y="NEXT"
    )

    # ═══════════════════════════════════════════════════════════════════════
    # NOW BUILD THE TABLE OF CONTENTS (overwrite the reserved pages)
    # ═══════════════════════════════════════════════════════════════════════
    # We reserved 3 blank pages for the TOC earlier.  Now go back and draw
    # the heading + entries onto those pages, carefully advancing from one
    # reserved page to the next when the cursor nears the bottom margin.

    toc_page_idx = 0                       # index into toc_pages[]
    pdf.page = toc_pages[toc_page_idx]
    header_h = 30                          # approximate header height written by header()

    # Draw TOC heading on the first TOC page
    pdf.set_y(header_h)
    pdf.set_font("Helvetica", "B", 20)
    pdf.set_text_color(30, 30, 30)
    pdf.cell(0, 12, "TABLE OF CONTENTS", align="L", new_x="LMARGIN", new_y="NEXT")
    pdf.set_draw_color(80, 80, 80)
    pdf.line(pdf.l_margin, pdf.get_y(), pdf.w - pdf.r_margin, pdf.get_y())
    pdf.ln(8)

    bottom_limit = pdf.h - 30             # leave room for footer

    for level, title, page_num in pdf.toc_entries:
        # If we're running out of room on the current TOC page, jump to next
        if pdf.get_y() + 10 > bottom_limit:
            toc_page_idx += 1
            if toc_page_idx < len(toc_pages):
                pdf.page = toc_pages[toc_page_idx]
                pdf.set_y(header_h)
            else:
                break  # safety: ran out of reserved pages

        indent = 0 if level == 1 else (10 if level == 2 else 20)
        if level == 1:
            pdf.set_font("Helvetica", "B", 11)
        elif level == 2:
            pdf.set_font("Helvetica", "", 10)
        else:
            pdf.set_font("Helvetica", "", 9.5)

        pdf.set_text_color(50, 50, 50)
        x = pdf.l_margin + indent
        pdf.set_x(x)
        available_w = pdf.w - pdf.r_margin - x
        page_str = str(page_num)

        # Title
        title_w = pdf.get_string_width(title)
        page_w = pdf.get_string_width(page_str)
        dot_w = pdf.get_string_width(" . ")

        pdf.cell(title_w + 2, 6, title)

        # Dots
        dots_space = available_w - title_w - page_w - 6
        if dots_space > 0:
            num_dots = max(1, int(dots_space / dot_w))
            pdf.set_text_color(180, 180, 180)
            pdf.cell(dots_space, 6, " " + ". " * num_dots)

        # Page number
        pdf.set_text_color(50, 50, 50)
        pdf.cell(page_w + 2, 6, page_str, new_x="LMARGIN", new_y="NEXT")

        if level == 1:
            pdf.ln(1)

    # ═══════════════════════════════════════════════════════════════════════
    # OUTPUT
    # ═══════════════════════════════════════════════════════════════════════
    pdf.output(OUTPUT_PATH)
    print(f"Manual generated: {OUTPUT_PATH}")
    print(f"Total pages: {pdf.pages_count}")


if __name__ == "__main__":
    build_manual()
