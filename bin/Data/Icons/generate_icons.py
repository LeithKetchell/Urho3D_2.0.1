#!/usr/bin/env python3
"""
Generate the stone-age suite icons for Urho3D tools.

Produces 1024x1024 master PNGs using strict 5-colour palette and bold
silhouette discipline per BRIEF.md. Each icon has a distinct motif:
- 60_TerrainNode: hunter + campfire
- AuthServer: carved stone tablet
- ModelViewer: standing totem
- Claudette: oracular eye
- WorkboardManager: tablet cairn
- WorkboardClient: smaller cairn + smoke signal

Usage: python3 generate_icons.py
Output: <AppName>.png in the same directory as this script.

Not intended as final art — first-pass placeholder that respects the
brief. Real hand-drawn or AI-generated art replaces these in-place.
"""

import os
from PIL import Image, ImageDraw

# ── Palette (strict, see BRIEF.md) ───────────────────────────────────────
CHARCOAL   = (0x1A, 0x14, 0x10, 255)
OCHRE      = (0xB8, 0x5C, 0x2B, 255)
CREAM      = (0xE8, 0xD9, 0xB0, 255)
SANDSTONE  = (0xC4, 0x9A, 0x6C, 255)
FIRE       = (0xE8, 0x76, 0x1D, 255)

MASTER_SIZE = 1024  # coordinates authored at this size
SIZE = 64           # actual output size — window icons, not posters
SCALE = SIZE / MASTER_SIZE
OUT_DIR = os.path.dirname(os.path.abspath(__file__))


def new_canvas(bg=CREAM):
    img = Image.new('RGBA', (MASTER_SIZE, MASTER_SIZE), bg)
    return img, ImageDraw.Draw(img)


def border(draw, thickness=24, colour=CHARCOAL):
    """Draw a heavy border frame so every icon shares a silhouette edge."""
    draw.rectangle([0, 0, MASTER_SIZE - 1, thickness], fill=colour)
    draw.rectangle([0, MASTER_SIZE - 1 - thickness, MASTER_SIZE - 1, MASTER_SIZE - 1], fill=colour)
    draw.rectangle([0, 0, thickness, MASTER_SIZE - 1], fill=colour)
    draw.rectangle([MASTER_SIZE - 1 - thickness, 0, MASTER_SIZE - 1, MASTER_SIZE - 1], fill=colour)


# ── 60_TerrainNode — Hunter + Campfire ──────────────────────────────────
def icon_terrain_node():
    img, d = new_canvas(CREAM)

    # Ground line
    d.rectangle([0, 820, SIZE, 1024], fill=SANDSTONE)

    # Campfire — triangular flame shape, fire orange accent
    # Base logs (charcoal crossed sticks)
    d.polygon([(340, 840), (480, 840), (460, 880), (360, 880)], fill=CHARCOAL)
    d.polygon([(330, 855), (490, 855), (470, 895), (350, 895)], fill=CHARCOAL)
    # Inner flame (ochre)
    d.polygon([(410, 820), (350, 700), (390, 720), (400, 640), (430, 710), (470, 680), (450, 820)], fill=OCHRE)
    # Core fire (fire orange accent — small area, high impact)
    d.polygon([(410, 810), (380, 740), (410, 760), (420, 700), (435, 760), (445, 760), (440, 810)], fill=FIRE)

    # Hunter silhouette — stands to the right of the fire
    # Torso
    d.polygon([(640, 420), (720, 420), (740, 700), (620, 700)], fill=CHARCOAL)
    # Head (round)
    d.ellipse([(640, 320), (730, 410)], fill=CHARCOAL)
    # Legs
    d.polygon([(625, 700), (690, 700), (690, 860), (640, 860)], fill=CHARCOAL)
    d.polygon([(680, 700), (745, 700), (740, 860), (690, 860)], fill=CHARCOAL)
    # Arm holding spear
    d.polygon([(720, 460), (800, 440), (820, 480), (740, 500)], fill=CHARCOAL)
    # Spear shaft (diagonal)
    d.polygon([(790, 440), (820, 460), (920, 200), (890, 180)], fill=CHARCOAL)
    # Spear head
    d.polygon([(880, 200), (920, 180), (940, 130), (910, 150)], fill=CHARCOAL)

    border(d)
    return img


# ── AuthServer — Stone Tablet with Carved Rules ─────────────────────────
def icon_authserver():
    img, d = new_canvas(CREAM)

    # Tablet body (sandstone, arched top)
    d.rectangle([(180, 280), (844, 920)], fill=SANDSTONE)
    d.ellipse([(180, 140), (844, 420)], fill=SANDSTONE)

    # Tablet outline
    d.rectangle([(180, 280), (844, 920)], outline=CHARCOAL, width=12)
    d.ellipse([(180, 140), (844, 420)], outline=CHARCOAL, width=12)

    # Cover the outline seam
    d.rectangle([(192, 270), (832, 290)], fill=SANDSTONE)

    # Carved lines of "law" — horizontal strokes
    for y in range(440, 880, 60):
        length = 500 if (y // 60) % 2 == 0 else 420
        x0 = 512 - length // 2
        d.rectangle([(x0, y), (x0 + length, y + 14)], fill=CHARCOAL)

    # Fire orange accent — a single emphasis mark (seal of authority) at top
    d.polygon([(512, 200), (560, 260), (540, 300), (484, 300), (464, 260)], fill=FIRE)
    d.polygon([(512, 230), (540, 270), (525, 290), (499, 290), (484, 270)], fill=CHARCOAL)

    border(d)
    return img


# ── ModelViewer — Standing Totem ────────────────────────────────────────
def icon_modelviewer():
    img, d = new_canvas(CREAM)

    # Base pedestal (sandstone)
    d.rectangle([(220, 860), (804, 960)], fill=SANDSTONE)
    d.rectangle([(220, 860), (804, 960)], outline=CHARCOAL, width=10)

    # Totem pole — three stacked head sections
    # Bottom head (widest)
    d.rectangle([(300, 600), (724, 860)], fill=OCHRE)
    d.rectangle([(300, 600), (724, 860)], outline=CHARCOAL, width=10)
    # Mouth/features
    d.rectangle([(380, 720), (644, 760)], fill=CHARCOAL)
    d.ellipse([(360, 640), (460, 700)], fill=CHARCOAL)
    d.ellipse([(564, 640), (664, 700)], fill=CHARCOAL)

    # Middle head
    d.rectangle([(340, 360), (684, 600)], fill=SANDSTONE)
    d.rectangle([(340, 360), (684, 600)], outline=CHARCOAL, width=10)
    # Eyes (wide)
    d.ellipse([(384, 420), (464, 500)], fill=CHARCOAL)
    d.ellipse([(560, 420), (640, 500)], fill=CHARCOAL)

    # Top head (smaller)
    d.rectangle([(380, 160), (644, 360)], fill=OCHRE)
    d.rectangle([(380, 160), (644, 360)], outline=CHARCOAL, width=10)
    # Mouth line
    d.rectangle([(440, 270), (584, 300)], fill=CHARCOAL)
    # Fire orange crest — accent
    d.polygon([(512, 160), (440, 100), (484, 130), (512, 60), (540, 130), (584, 100)], fill=FIRE)

    border(d)
    return img


# ── Claudette — Oracular Eye / Carved Face ──────────────────────────────
def _claudette_face(d, glyph_fn=None, face_fill=SANDSTONE):
    """Shared carved-face + eye + rays. Role variants supply a glyph below.

    The generic Claudette icon passes glyph_fn=None to render the classic
    two-stroke carved mouth. Role variants replace that area with a role-
    specific motif: tablet (Planner), crossed tools (Coder), open hand
    (Catch). The face, eye, and ray cadence stay identical so the whole
    set reads as one family.

    face_fill drives the dominant colour so role variants are
    distinguishable at 16-32px taskbar sizes where the sub-motif becomes
    a smudge. Planner = CREAM (pale), Coder = OCHRE (hot), generic and
    Catch = SANDSTONE (neutral). All within the approved 5-palette.
    """
    # Face outline (carved square)
    d.rectangle([(180, 180), (844, 844)], fill=face_fill)
    d.rectangle([(180, 180), (844, 844)], outline=CHARCOAL, width=14)

    # Large central eye — the oracle
    # Eyelid frame (elongated almond)
    d.ellipse([(260, 380), (764, 644)], fill=CREAM)
    d.ellipse([(260, 380), (764, 644)], outline=CHARCOAL, width=14)

    # Iris — ochre
    d.ellipse([(410, 400), (614, 604)], fill=OCHRE)
    # Pupil — charcoal
    d.ellipse([(460, 450), (564, 554)], fill=CHARCOAL)
    # Fire orange glint — small accent (the spark of speech)
    d.ellipse([(480, 470), (512, 502)], fill=FIRE)

    # Carved rays emanating from the face (ochre strokes)
    import math
    for angle_deg in range(0, 360, 30):
        rad = math.radians(angle_deg)
        cx, cy = 512, 512
        x1 = cx + math.cos(rad) * 720
        y1 = cy + math.sin(rad) * 720
        x0 = cx + math.cos(rad) * 640
        y0 = cy + math.sin(rad) * 640
        # Skip rays that would cross the eye horizontally
        if 340 < angle_deg < 380 or 160 < angle_deg < 200:
            continue
        d.line([(x0, y0), (x1, y1)], fill=CHARCOAL, width=18)

    if glyph_fn is None:
        # Default: two carved marks below the eye (carved mouth hint)
        d.rectangle([(380, 720), (644, 740)], fill=CHARCOAL)
        d.rectangle([(420, 770), (604, 790)], fill=CHARCOAL)
    else:
        glyph_fn(d)


def icon_claudette():
    img, d = new_canvas(CREAM)
    _claudette_face(d)
    border(d)
    return img


# ── Claudette/Planner — Eye over Carved Tablet ──────────────────────────
def icon_claudette_planner():
    """Planner: the oracle writes plans on stone. Tablet below the eye."""
    img, d = new_canvas(CREAM)

    def glyph(d):
        # Arched stone tablet centred below eye
        d.rectangle([(384, 720), (640, 820)], fill=CREAM)
        d.ellipse([(384, 690), (640, 760)], fill=CREAM)
        d.rectangle([(384, 720), (640, 820)], outline=CHARCOAL, width=10)
        d.ellipse([(384, 690), (640, 760)], outline=CHARCOAL, width=10)
        # Cover outline seam at tablet top
        d.rectangle([(394, 712), (630, 730)], fill=CREAM)
        # Three carved rules
        for y in (748, 774, 800):
            d.rectangle([(410, y), (614, y + 8)], fill=CHARCOAL)

    _claudette_face(d, glyph, face_fill=CREAM)
    border(d)
    return img


# ── Claudette/Coder — Eye over Crossed Chisel + Hammer ──────────────────
def icon_claudette_coder():
    """Coder: the oracle chips stone. Crossed tools below the eye."""
    img, d = new_canvas(CREAM)

    def glyph(d):
        # Chisel — diagonal charcoal bar running top-left to bottom-right
        d.polygon([(372, 700), (408, 688), (628, 804), (604, 830)], fill=CHARCOAL)
        # Chisel tip (cream stone bit — contrasts with ochre face)
        d.polygon([(598, 820), (628, 804), (648, 824), (622, 846)], fill=CREAM)
        # Hammer — diagonal charcoal bar top-right to bottom-left, behind chisel line
        d.polygon([(648, 692), (616, 680), (396, 808), (416, 836)], fill=CHARCOAL)
        # Hammer head (sandstone block at top-right end)
        d.rectangle([(620, 676), (700, 720)], fill=SANDSTONE)
        d.rectangle([(620, 676), (700, 720)], outline=CHARCOAL, width=8)
        # Chisel tip re-emphasised so the crossing reads
        d.polygon([(406, 824), (416, 836), (448, 826), (436, 812)], fill=CREAM)

    _claudette_face(d, glyph, face_fill=OCHRE)
    border(d)
    return img


# ── Claudette/Catch — Eye over Open Hand ────────────────────────────────
def icon_claudette_catch():
    """Catch mode: the oracle catches another's voice. Open hand below."""
    img, d = new_canvas(CREAM)

    def glyph(d):
        # Palm — wide charcoal arc
        d.pieslice([(372, 740), (652, 900)], 180, 360, fill=CHARCOAL)
        # Fingers — four vertical charcoal bars rising from the palm
        for x0 in (388, 454, 520, 586):
            d.rectangle([(x0, 700), (x0 + 40, 760)], fill=CHARCOAL)
            d.ellipse([(x0, 688), (x0 + 40, 720)], fill=CHARCOAL)
        # Thumb — offset on the right
        d.rectangle([(632, 716), (668, 770)], fill=CHARCOAL)
        d.ellipse([(624, 700), (676, 736)], fill=CHARCOAL)
        # Fire orange spark landing in the palm — the caught voice
        d.ellipse([(494, 790), (534, 830)], fill=FIRE)

    _claudette_face(d, glyph)
    border(d)
    return img


# ── WorkboardManager — Cairn of Tablets ─────────────────────────────────
def icon_workboard_manager():
    img, d = new_canvas(CREAM)

    # Ground
    d.rectangle([0, 860, SIZE, 1024], fill=SANDSTONE)

    # Stack of tablets — staggered cairn
    # Bottom (widest, most visible)
    d.rectangle([(140, 720), (884, 900)], fill=SANDSTONE)
    d.rectangle([(140, 720), (884, 900)], outline=CHARCOAL, width=10)
    # Carved lines (3 horizontal)
    for y in range(760, 870, 36):
        d.rectangle([(200, y), (820, y + 10)], fill=CHARCOAL)

    # Middle tablet (offset left)
    d.rectangle([(100, 500), (800, 720)], fill=OCHRE)
    d.rectangle([(100, 500), (800, 720)], outline=CHARCOAL, width=10)
    for y in range(540, 700, 36):
        d.rectangle([(160, y), (740, y + 10)], fill=CHARCOAL)

    # Upper tablet (offset right, smaller)
    d.rectangle([(250, 280), (840, 500)], fill=SANDSTONE)
    d.rectangle([(250, 280), (840, 500)], outline=CHARCOAL, width=10)
    for y in range(320, 480, 36):
        d.rectangle([(300, y), (790, y + 10)], fill=CHARCOAL)

    # Top tablet (smallest)
    d.rectangle([(360, 80), (720, 280)], fill=OCHRE)
    d.rectangle([(360, 80), (720, 280)], outline=CHARCOAL, width=10)
    # Single fire orange mark at top — the priority stamp
    d.ellipse([(500, 140), (584, 224)], fill=FIRE)
    d.ellipse([(524, 164), (560, 200)], fill=CHARCOAL)

    border(d)
    return img


# ── WorkboardClient — Small Cairn + Smoke Signal ────────────────────────
def icon_workboard_client():
    img, d = new_canvas(CREAM)

    # Ground (lower horizon — more sky for smoke)
    d.rectangle([0, 820, SIZE, 1024], fill=SANDSTONE)

    # Smaller cairn — bottom-right corner (the distant runner's camp)
    d.rectangle([(540, 680), (900, 860)], fill=SANDSTONE)
    d.rectangle([(540, 680), (900, 860)], outline=CHARCOAL, width=10)
    for y in range(720, 840, 34):
        d.rectangle([(580, y), (860, y + 10)], fill=CHARCOAL)

    d.rectangle([(600, 560), (860, 680)], fill=OCHRE)
    d.rectangle([(600, 560), (860, 680)], outline=CHARCOAL, width=10)

    # Smoke signal — rising column of puffs on the left
    # Fire at base
    d.polygon([(180, 720), (270, 720), (225, 650), (200, 690), (240, 680)], fill=FIRE)
    d.rectangle([(170, 720), (280, 780)], fill=CHARCOAL)

    # Smoke puffs — staggered ellipses rising
    puff_colour = CHARCOAL
    for i, (cx, cy, r) in enumerate([
        (225, 580, 50),
        (260, 480, 60),
        (220, 380, 70),
        (280, 280, 75),
        (240, 180, 80),
    ]):
        d.ellipse([(cx - r, cy - r), (cx + r, cy + r)], fill=puff_colour)

    # Horizon carved line — suggesting distance
    d.rectangle([(340, 600), (540, 612)], fill=CHARCOAL)

    border(d)
    return img


# ── Generator driver ─────────────────────────────────────────────────────
ICONS = [
    ('60_TerrainNode',     icon_terrain_node),
    ('AuthServer',         icon_authserver),
    ('ModelTool',          icon_modelviewer),
    ('Claudette_Planner',  icon_claudette_planner),
    ('Claudette_Coder',    icon_claudette_coder),
    ('WorkboardManager',   icon_workboard_manager),
    ('WorkboardClient',    icon_workboard_client),
]


def main():
    for name, fn in ICONS:
        img = fn()
        # Draw at master size for coordinate clarity, then downscale
        img = img.resize((SIZE, SIZE), Image.LANCZOS)
        out_path = os.path.join(OUT_DIR, f'{name}.png')
        img.save(out_path, 'PNG')
        print(f'Wrote {out_path} ({SIZE}x{SIZE})')


if __name__ == '__main__':
    main()
