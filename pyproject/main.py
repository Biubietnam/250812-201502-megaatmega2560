"""
Simple Prescription Builder (Tkinter UI)
- Enter Hospital name, pick Logo, type prescription lines
- Each line format:
    Name|Amount|time1|dosage1|time2|[dosage2]|time3|[dosage3] ...
  Rules:
    * time1 and dosage1 are REQUIRED
    * dosageN for N>=2 is OPTIONAL (if omitted, it defaults to dosage1 for the internal schedule)
    * You may have multiple lines (one medicine per line)
- Exports: PNG or PDF (with a QR code that encodes the exact multiline prescription text)
- Now supports table format and multi-page layout for many medications

Dependencies (install locally if missing):
    pip install pillow qrcode

Run:
    python prescription_builder.py
"""

import io
import os
import re
import sys
from datetime import datetime
from typing import List, Tuple, Dict
import math

try:
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
except Exception as e:
    print("Tkinter is required to run this UI app.")
    raise

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("This app needs Pillow. Install with: pip install pillow")
    raise

try:
    import qrcode
except ImportError:
    qrcode = None  # We'll check at export time and ask the user to install

# -----------------------------
# Parsing helpers
# -----------------------------
TIME_RE = re.compile(r"^([01]?\d|2[0-3]):[0-5]\d$")


def is_valid_time(t: str) -> bool:
    return TIME_RE.match(t) is not None


def parse_line(line: str) -> Dict:
    """Parse one prescription line.
    Expected at least: Name|Amount|time1|dosage1
    Then pairs of timeN with optional dosageN.

    Returns dict {name, amount, schedules: [(time, dosage_int), ...], normalized_line}
    normalized_line keeps EXACT optional dosage behavior for QR (don't add missing dosageN).
    """
    raw = line.strip()
    if not raw:
        raise ValueError("Empty line")
    # Keep trailing empties out but preserve intended empties between pipes
    tokens = [tok.strip() for tok in raw.split("|")]
    # Drop trailing empty tokens from a trailing pipe (visual convenience)
    while tokens and tokens[-1] == "":
        tokens.pop()

    if len(tokens) < 4:
        raise ValueError(f"Need at least Name|Amount|time1|dosage1 -- got: {raw}")

    name = tokens[0]
    if not name:
        raise ValueError("Name cannot be empty")

    try:
        amount = int(tokens[1])
    except ValueError:
        raise ValueError(f"Amount must be an integer for '{name}'")

    time1, dose1 = tokens[2], tokens[3]
    if not is_valid_time(time1):
        raise ValueError(f"Invalid time1 '{time1}' for '{name}' (HH:mm)")
    try:
        d1_int = int(dose1)
    except ValueError:
        raise ValueError(f"Invalid dosage1 '{dose1}' for '{name}' (int)")

    schedules: List[Tuple[str, int]] = [(time1, d1_int)]

    # Subsequent items: expect timeN, and optional dosageN (if missing -> default to dose1)
    i = 4
    while i < len(tokens):
        t = tokens[i].strip()
        if not is_valid_time(t):
            raise ValueError(f"Expected time at position {i} for '{name}', got '{t}'")
        d = d1_int
        if i + 1 < len(tokens) and tokens[i + 1].strip() != "":
            try:
                d = int(tokens[i + 1].strip())
                i += 2
            except ValueError:
                raise ValueError(f"Invalid dosage after time '{t}' for '{name}'")
        else:
            # no dosage provided -> default to dosage1
            i += 1
        schedules.append((t, d))

    # normalized_line for QR = reflect exactly the optional dosage behavior
    # We DO NOT inject defaulted dosage into the string; keep as original structure
    normalized_line = raw

    return {
        "name": name,
        "amount": amount,
        "schedules": schedules,
        "normalized_line": normalized_line,
    }


def parse_multiline(text: str) -> Tuple[List[Dict], str]:
    """Parse the textarea content into list of meds and the QR payload string (joined by \n).
    Raises ValueError with a human-friendly message on first error.
    """
    lines = [ln for ln in (text or "").replace("\r\n", "\n").replace("\r", "\n").split("\n") if ln.strip()]
    meds = []
    normalized_lines = []
    for idx, ln in enumerate(lines, start=1):
        try:
            rec = parse_line(ln)
            meds.append(rec)
            normalized_lines.append(rec["normalized_line"])  # keep exact input shape for QR
        except Exception as e:
            raise ValueError(f"Line {idx}: {e}")
    qr_payload = "\n".join(normalized_lines)
    return meds, qr_payload


# -----------------------------
# Rendering helpers (PNG/PDF)
# -----------------------------
PAGE_W = 1240  # ~A4 @ 150 dpi width
PAGE_H = 1754  # ~A4 @ 150 dpi height
MARGIN = 64
LINE_H = 36
TABLE_ROW_H = 32
HEADER_H = 240
QR_BLOCK_H = 280

MAX_QR_SIZE = 200  # Maximum QR code size to prevent overflow
MEDS_PER_PAGE = 15  # Maximum medications per page


def load_logo(path: str, max_w: int = 200, max_h: int = 200) -> Image.Image:
    im = Image.open(path).convert("RGBA")
    im.thumbnail((max_w, max_h), Image.LANCZOS)
    return im


def draw_text(draw: ImageDraw.ImageDraw, xy, text, font, fill=(0, 0, 0)):
    draw.text(xy, text, font=font, fill=fill)


def draw_table_header(draw: ImageDraw.ImageDraw, x: int, y: int, font, page_w: int, margin: int):
    """Draw table header with columns for Medicine, Amount, and Schedule"""
    col_widths = [400, 120, 400]  # Medicine, Amount, Schedule
    col_x = [x, x + col_widths[0], x + col_widths[0] + col_widths[1]]
    
    # Draw header background
    draw.rectangle([x, y, page_w - margin, y + TABLE_ROW_H], fill=(240, 240, 240), outline=(0, 0, 0))
    
    # Draw column separators
    for i in range(1, len(col_x)):
        draw.line([col_x[i], y, col_x[i], y + TABLE_ROW_H], fill=(0, 0, 0), width=1)
    
    # Draw header text
    draw_text(draw, (col_x[0] + 8, y + 6), "Medicine", font, fill=(0, 0, 0))
    draw_text(draw, (col_x[1] + 8, y + 6), "Amount", font, fill=(0, 0, 0))
    draw_text(draw, (col_x[2] + 8, y + 6), "Schedule", font, fill=(0, 0, 0))
    
    return y + TABLE_ROW_H


def draw_table_row(draw: ImageDraw.ImageDraw, x: int, y: int, med_data: Dict, font, page_w: int, margin: int):
    """Draw a single table row for a medication"""
    col_widths = [400, 120, 400]
    col_x = [x, x + col_widths[0], x + col_widths[0] + col_widths[1]]
    
    # Draw row background and borders
    draw.rectangle([x, y, page_w - margin, y + TABLE_ROW_H], fill=(255, 255, 255), outline=(0, 0, 0))
    
    # Draw column separators
    for i in range(1, len(col_x)):
        draw.line([col_x[i], y, col_x[i], y + TABLE_ROW_H], fill=(0, 0, 0), width=1)
    
    # Draw cell content
    name = med_data["name"]
    amount = str(med_data["amount"])
    schedules = med_data["schedules"]
    schedule_text = ", ".join([f"{t}×{d}" for (t, d) in schedules])
    
    # Truncate text if too long
    if len(name) > 35:
        name = name[:32] + "..."
    if len(schedule_text) > 40:
        schedule_text = schedule_text[:37] + "..."
    
    draw_text(draw, (col_x[0] + 8, y + 6), name, font)
    draw_text(draw, (col_x[1] + 8, y + 6), amount, font)
    draw_text(draw, (col_x[2] + 8, y + 6), schedule_text, font)
    
    return y + TABLE_ROW_H


def calculate_qr_size(qr_payload: str) -> int:
    """Calculate appropriate QR code size based on data length"""
    if qrcode is None:
        return MAX_QR_SIZE
    
    data_length = len(qr_payload)
    if data_length < 100:
        return min(120, MAX_QR_SIZE)
    elif data_length < 300:
        return min(160, MAX_QR_SIZE)
    else:
        return MAX_QR_SIZE


def generate_page(hospital_name: str, logo_path: str, meds: List[Dict], qr_payload: str, page_num: int, total_pages: int, doctor_name: str = "", patient_name: str = "", notes: str = "") -> Image.Image:
    """Generate a single page of the prescription"""
    try:
        font_title = ImageFont.truetype("arial.ttf", 42)
        font_sub = ImageFont.truetype("arial.ttf", 24)
        font_body = ImageFont.truetype("arial.ttf", 20)
        font_table = ImageFont.truetype("arial.ttf", 18)
    except Exception:
        font_title = ImageFont.load_default()
        font_sub = ImageFont.load_default()
        font_body = ImageFont.load_default()
        font_table = ImageFont.load_default()

    img = Image.new("RGB", (PAGE_W, PAGE_H), "white")
    draw = ImageDraw.Draw(img)

    x = MARGIN
    y = MARGIN

    # Header section
    if logo_path and os.path.isfile(logo_path):
        try:
            logo = load_logo(logo_path, 180, 180)
            img.paste(logo, (x, y), logo)
        except Exception:
            pass
    
    draw_text(draw, (x + 200, y + 10), hospital_name or "Hospital / Clinic", font_title)
    draw_text(draw, (x + 200, y + 60), f"Prescription", font_sub)
    draw_text(draw, (x + 200, y + 90), datetime.now().strftime("Date: %Y-%m-%d %H:%M"), font_sub)
    
    if doctor_name:
        draw_text(draw, (x + 200, y + 120), f"Doctor: {doctor_name}", font_sub)
    if patient_name:
        draw_text(draw, (x + 200, y + 150), f"Patient: {patient_name}", font_sub)
    
    # Page number if multiple pages
    if total_pages > 1:
        page_y = y + 180 if (doctor_name or patient_name) else y + 120
        draw_text(draw, (x + 200, page_y), f"Page {page_num} of {total_pages}", font_sub)

    y += HEADER_H

    # Table section
    draw.line((MARGIN, y, PAGE_W - MARGIN, y), fill=(0, 0, 0), width=2)
    y += 20

    # Draw table header
    y = draw_table_header(draw, x, y, font_table, PAGE_W, MARGIN)

    # Draw medication rows
    for med in meds:
        y = draw_table_row(draw, x, y, med, font_table, PAGE_W, MARGIN)

    y += 20
    draw.line((MARGIN, y, PAGE_W - MARGIN, y), fill=(0, 0, 0), width=2)
    y += 30

    if notes and notes.strip():
        draw_text(draw, (x, y), "Notes:", font_sub)
        y += LINE_H + 5
        
        # Split notes into multiple lines if too long
        note_lines = []
        words = notes.strip().split()
        current_line = ""
        max_chars_per_line = 80
        
        for word in words:
            if len(current_line + " " + word) <= max_chars_per_line:
                current_line += (" " + word) if current_line else word
            else:
                if current_line:
                    note_lines.append(current_line)
                current_line = word
        if current_line:
            note_lines.append(current_line)
        
        for note_line in note_lines:
            draw_text(draw, (x, y), note_line, font_body)
            y += LINE_H
        
        y += 20

    # QR code section (only on last page)
    if page_num == total_pages:
        if qrcode is None:
            draw_text(draw, (x, y), "[Install 'qrcode' to embed QR]", font_body, fill=(180, 0, 0))
        else:
            # Calculate appropriate QR size
            qr_size = calculate_qr_size(qr_payload)
            box_size = max(3, min(8, qr_size // 30))  # Adaptive box size
            
            qr = qrcode.QRCode(
                version=None,
                error_correction=qrcode.constants.ERROR_CORRECT_M,  # Medium error correction
                box_size=box_size,
                border=2,
            )
            qr.add_data(qr_payload)
            qr.make(fit=True)
            qr_img = qr.make_image(fill_color="black", back_color="white").convert("RGB")
            
            # Resize if still too large
            if qr_img.size[0] > MAX_QR_SIZE:
                qr_img = qr_img.resize((MAX_QR_SIZE, MAX_QR_SIZE), Image.LANCZOS)
            
            # Position QR code
            qr_x = PAGE_W - MARGIN - qr_img.size[0]
            qr_y = min(y, PAGE_H - MARGIN - qr_img.size[1])
            img.paste(qr_img, (qr_x, qr_y))

        # QR legend
        draw_text(draw, (x, y), "QR: Complete prescription data", font_body)
        y += LINE_H
        
        # # Show compact preview
        # preview = qr_payload.replace("\n", " | ")
        # if len(preview) > 100:
        #     preview = preview[:97] + "..."
        # draw_text(draw, (x, y), f"Data: {preview}", font_body)

    return img


def generate_images(hospital_name: str, logo_path: str, prescription_text: str, doctor_name: str = "", patient_name: str = "", notes: str = "") -> List[Image.Image]:
    """Generate all pages of the prescription as separate images"""
    meds, qr_payload = parse_multiline(prescription_text)
    
    if not meds:
        raise ValueError("No valid medications found")
    
    # Split medications into pages
    pages = []
    total_pages = math.ceil(len(meds) / MEDS_PER_PAGE)
    
    for page_num in range(1, total_pages + 1):
        start_idx = (page_num - 1) * MEDS_PER_PAGE
        end_idx = min(start_idx + MEDS_PER_PAGE, len(meds))
        page_meds = meds[start_idx:end_idx]
        
        page_img = generate_page(hospital_name, logo_path, page_meds, qr_payload, page_num, total_pages, doctor_name, patient_name, notes)
        pages.append(page_img)
    
    return pages


# Keep the old function for backward compatibility but redirect to new multi-page version
def generate_image(hospital_name: str, logo_path: str, prescription_text: str, doctor_name: str = "", patient_name: str = "", notes: str = "") -> Image.Image:
    """Generate prescription image (returns first page for preview)"""
    pages = generate_images(hospital_name, logo_path, prescription_text, doctor_name, patient_name, notes)
    return pages[0] if pages else None


# -----------------------------
# Tkinter UI
# -----------------------------
class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Prescription Builder - Table Format")
        self.geometry("1000x800")  # Increased height for new fields
        self.logo_path = ""

        # Styles
        try:
            self.tk.call("source", "sun-valley.tcl")
            self.tk.call("set_theme", "light")
        except Exception:
            pass

        container = ttk.Frame(self)
        container.pack(fill=tk.BOTH, expand=True, padx=16, pady=16)

        # Top form
        form = ttk.Frame(container)
        form.pack(fill=tk.X)

        ttk.Label(form, text="Hospital/Clinic name:").grid(row=0, column=0, sticky=tk.W, padx=4, pady=4)
        self.hospital_var = tk.StringVar()
        ttk.Entry(form, textvariable=self.hospital_var, width=40).grid(row=0, column=1, sticky=tk.W, padx=4, pady=4)

        ttk.Button(form, text="Choose Logo…", command=self.choose_logo).grid(row=0, column=2, padx=8, pady=4)
        self.logo_lbl = ttk.Label(form, text="No logo selected")
        self.logo_lbl.grid(row=0, column=3, padx=4, pady=4)

        ttk.Label(form, text="Doctor Name (MD):").grid(row=1, column=0, sticky=tk.W, padx=4, pady=4)
        self.doctor_var = tk.StringVar()
        ttk.Entry(form, textvariable=self.doctor_var, width=40).grid(row=1, column=1, sticky=tk.W, padx=4, pady=4)

        ttk.Label(form, text="Patient Name:").grid(row=2, column=0, sticky=tk.W, padx=4, pady=4)
        self.patient_var = tk.StringVar()
        ttk.Entry(form, textvariable=self.patient_var, width=40).grid(row=2, column=1, sticky=tk.W, padx=4, pady=4)

        # Text area for prescription lines
        ttk.Label(container, text="Prescription lines (one medicine per line) - Table format with multi-page support:\nName|Amount|time1|dosage1|time2|[dosage2]|time3|[dosage3] …").pack(anchor=tk.W, pady=(12, 4))

        self.text = tk.Text(container, height=12, wrap=tk.NONE)  # Reduced height to make room for notes
        self.text.pack(fill=tk.BOTH, expand=True)
        self.text.insert(
            "1.0",
            "Paracetamol|120|08:00|1|20:00|\nPanadol|70|12:00|2|18:30|1|\nAspirin|50|09:00|1|21:00|1\nIbuprofen|30|07:00|1|19:00|1\n"
        )

        ttk.Label(container, text="Notes (optional):").pack(anchor=tk.W, pady=(12, 4))
        self.notes_text = tk.Text(container, height=4, wrap=tk.WORD)
        self.notes_text.pack(fill=tk.X, pady=(0, 12))

        # Buttons
        btns = ttk.Frame(container)
        btns.pack(fill=tk.X, pady=12)

        ttk.Button(btns, text="Preview (Page 1)", command=self.preview).pack(side=tk.LEFT, padx=4)
        ttk.Button(btns, text="Export PNG", command=self.export_png).pack(side=tk.LEFT, padx=4)
        ttk.Button(btns, text="Export PDF", command=self.export_pdf).pack(side=tk.LEFT, padx=4)

        self.page_info_lbl = ttk.Label(btns, text="")
        self.page_info_lbl.pack(side=tk.LEFT, padx=12)

        self.preview_canvas = tk.Label(container)
        self.preview_canvas.pack(fill=tk.BOTH, expand=True)

    # --- actions ---
    def choose_logo(self):
        path = filedialog.askopenfilename(
            title="Select logo image",
            filetypes=[("Image files", "*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp"), ("All files", "*.*")],
        )
        if path:
            self.logo_path = path
            self.logo_lbl.config(text=os.path.basename(path))

    def _try_generate(self) -> List[Image.Image]:
        try:
            pages = generate_images(
                self.hospital_var.get().strip(), 
                self.logo_path, 
                self.text.get("1.0", tk.END),
                self.doctor_var.get().strip(),
                self.patient_var.get().strip(),
                self.notes_text.get("1.0", tk.END).strip()
            )
            return pages
        except Exception as e:
            messagebox.showerror("Error", str(e))
            raise

    def preview(self):
        try:
            pages = self._try_generate()
            if not pages:
                return
            
            self.page_info_lbl.config(text=f"Total pages: {len(pages)}")
            
            # Show first page
            img = pages[0]
        except Exception:
            return
        
        # Downscale for on-screen preview
        preview = img.copy()
        preview.thumbnail((900, 1200))
        bio = io.BytesIO()
        preview.save(bio, format="PNG")
        data = bio.getvalue()
        # Tk image
        self._tkimg = tk.PhotoImage(data=data)
        self.preview_canvas.config(image=self._tkimg)

    def export_png(self):
        # Ensure QR dependency
        if qrcode is None:
            messagebox.showerror("Missing dependency", "Please install 'qrcode' first: pip install qrcode")
            return
        try:
            pages = self._try_generate()
            if not pages:
                return
        except Exception:
            return
        
        if len(pages) == 1:
            path = filedialog.asksaveasfilename(
                title="Save PNG",
                defaultextension=".png",
                filetypes=[("PNG image", "*.png")],
            )
            if path:
                pages[0].save(path, format="PNG")
                messagebox.showinfo("Saved", f"PNG exported to:\n{path}")
        else:
            # Multiple pages - save with page numbers
            base_path = filedialog.asksaveasfilename(
                title="Save PNG (multiple pages)",
                defaultextension=".png",
                filetypes=[("PNG image", "*.png")],
            )
            if base_path:
                base_name = os.path.splitext(base_path)[0]
                for i, page in enumerate(pages, 1):
                    page_path = f"{base_name}_page_{i}.png"
                    page.save(page_path, format="PNG")
                messagebox.showinfo("Saved", f"{len(pages)} PNG files exported:\n{base_name}_page_*.png")

    def export_pdf(self):
        # Ensure QR dependency
        if qrcode is None:
            messagebox.showerror("Missing dependency", "Please install 'qrcode' first: pip install qrcode")
            return
        try:
            pages = self._try_generate()
            if not pages:
                return
        except Exception:
            return
        
        path = filedialog.asksaveasfilename(
            title="Save PDF",
            defaultextension=".pdf",
            filetypes=[("PDF document", "*.pdf")],
        )
        if path:
            rgb_pages = [page.convert("RGB") for page in pages]
            if len(rgb_pages) == 1:
                rgb_pages[0].save(path, "PDF", resolution=150.0)
            else:
                # Multi-page PDF
                rgb_pages[0].save(path, "PDF", resolution=150.0, save_all=True, append_images=rgb_pages[1:])
            messagebox.showinfo("Saved", f"PDF with {len(pages)} page(s) exported to:\n{path}")


if __name__ == "__main__":
    App().mainloop()
