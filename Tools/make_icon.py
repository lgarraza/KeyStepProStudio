"""Draws the KeyStepPro Studio icon: the white chassis, the black display strip and the four track blocks.

Colours copied from App/KspStyle.h (ksp::ui::palette). Run: python Tools/make_icon.py (needs Pillow).
Writes App/Resources/icon.png (1024 px, JUCE ICON_BIG), icon_small.png (64 px, ICON_SMALL) and icon.ico
(16-256 px). JUCE builds the exe's own .ico and .icns from the PNGs.
"""
import os
from PIL import Image, ImageDraw

CHASSIS, EDGE, PANEL, SEG = "#f3f4f5", "#8d959c", "#121214", "#f4f6f8"
TRACK = ["#12a67f", "#f25a1f", "#f7c40d", "#e3202e"]
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "App", "Resources")


def draw(size):
    s = 4 * size                       # supersampled, then reduced for smooth edges
    u = s / 64.0
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([1 * u, 1 * u, 63 * u, 63 * u], radius=12 * u, fill=CHASSIS, outline=EDGE, width=max(1, round(1.5 * u)))
    d.rounded_rectangle([7 * u, 7 * u, 57 * u, 22 * u], radius=3 * u, fill=PANEL)
    for i in range(4):                 # the lit digits of the four displays
        x = 10 * u + i * 12 * u
        d.rectangle([x, 12 * u, x + 8 * u, 17 * u], fill=SEG)
    for i, c in enumerate(TRACK):      # the track blocks, left to right like on the KSP
        x = 7 * u + i * 12.75 * u
        d.rounded_rectangle([x, 27 * u, x + 11 * u, 57 * u], radius=2.5 * u, fill=c)
    return img.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    draw(1024).save(os.path.join(OUT, "icon.png"))
    draw(64).save(os.path.join(OUT, "icon_small.png"))
    sizes = [16, 24, 32, 48, 64, 128, 256]
    draw(256).save(os.path.join(OUT, "icon.ico"), sizes=[(n, n) for n in sizes],
                   append_images=[draw(n) for n in sizes[:-1]])
