import ttkbootstrap as ttk
from ttkbootstrap.constants import *
from tkinter import filedialog, messagebox
import asyncio
from bleak import BleakScanner, BleakClient
import threading
import os
import time
from tkinter import Canvas
import math
import json
import tkinter as tk

try:
    from bleak import BleakScanner, BleakClient
    BLEAK_AVAILABLE = True
except ImportError as e:
    print(f"Bleak not available: {e}")
    BLEAK_AVAILABLE = False
except Exception as e:
    print(f"BLE initialization error: {e}")
    BLEAK_AVAILABLE = False

class ResponsiveAutoPillDispenserApp:
    def __init__(self):
        # Initialize main window with responsive settings
        self.app = ttk.Window(themename="flatly")
        self.app.title("💊 Auto Pill Dispenser Control")
        self.app.geometry("900x800")  # Larger initial size
        self.app.minsize(800, 600)    # Minimum size
        self.app.resizable(True, True)  # Allow resizing
        
        # Variables
        self.ble_var = ttk.StringVar()
        self.ble_devices = {}  # Store device name -> address mapping
        self.file_var = ttk.StringVar()
        self.upload_mode = ttk.StringVar(value="json")
        self.is_sending = False
        self.qr_data = ""
        self.medication_data = []
        
        self.manual_medications = []
        self.current_med_schedules = []
        self.formatted_manual_data = ""
        
        self.setup_responsive_ui()
        self.bind_resize_events()
        
    def setup_responsive_ui(self):
        """Setup responsive UI with scrollable main content"""
        # Create main scrollable frame
        self.main_canvas = Canvas(self.app, highlightthickness=0)
        self.main_scrollbar = ttk.Scrollbar(self.app, orient="vertical", command=self.main_canvas.yview)
        self.scrollable_frame = ttk.Frame(self.main_canvas)
        
        # Configure scrolling
        self.scrollable_frame.bind(
            "<Configure>",
            lambda e: self.main_canvas.configure(scrollregion=self.main_canvas.bbox("all"))
        )
        
        self.main_canvas.create_window((0, 0), window=self.scrollable_frame, anchor="nw")
        self.main_canvas.configure(yscrollcommand=self.main_scrollbar.set)
        
        # Pack scrollable components
        self.main_canvas.pack(side="left", fill="both", expand=True, padx=10, pady=10)
        self.main_scrollbar.pack(side="right", fill="y")
        
        # Add mouse wheel scrolling
        self.bind_mousewheel()
        
        # Create all sections in the scrollable frame
        self.create_responsive_layout()
        
    def bind_mousewheel(self):
        """Bind mouse wheel scrolling"""
        def _on_mousewheel(event):
            self.main_canvas.yview_scroll(int(-1*(event.delta/120)), "units")
            
        def _bind_to_mousewheel(event):
            self.main_canvas.bind_all("<MouseWheel>", _on_mousewheel)
            
        def _unbind_from_mousewheel(event):
            self.main_canvas.unbind_all("<MouseWheel>")
            
        self.main_canvas.bind('<Enter>', _bind_to_mousewheel)
        self.main_canvas.bind('<Leave>', _unbind_from_mousewheel)
        
    def bind_resize_events(self):
        """Bind window resize events for responsive behavior"""
        def on_canvas_configure(event):
            # Update scroll region when canvas size changes
            canvas_width = event.width
            self.main_canvas.itemconfig(self.main_canvas.find_all()[0], width=canvas_width-20)
            
        self.main_canvas.bind('<Configure>', on_canvas_configure)
        
    def create_responsive_layout(self):
        """Create the main responsive layout"""
        
        # Header
        self.create_responsive_header()
        
        # Mode selection
        self.create_responsive_mode_selection()
        
        # Connection section
        self.create_responsive_connection()
        
        # Upload section
        self.create_responsive_upload()
        
        # Manual input section
        self.create_manual_input_section()
        
        # Preview section (larger and properly scrollable)
        self.create_responsive_preview()
        
        # Status section
        self.create_responsive_status()
        
        # Submit section (always visible)
        self.create_responsive_submit()
        
        # Clear button section
        self.create_clear_section()

    def create_responsive_header(self):
        """Create responsive header"""
        header_frame = ttk.Frame(self.scrollable_frame)
        header_frame.pack(fill="x", pady=(0, 20))
        
        # Title
        self.title_label = ttk.Label(
            header_frame,
            text="💊 AUTO PILL DISPENSER",
            font=("Segoe UI", 24, "bold"),
            bootstyle="primary"
        )
        self.title_label.pack()
        
        # Subtitle
        subtitle = ttk.Label(
            header_frame,
            text="🏥 Smart Medication Management System",
            font=("Segoe UI", 12),
            bootstyle="info"
        )
        subtitle.pack(pady=(5, 0))
        
        # Animated separator
        self.create_animated_separator(header_frame)
        
    def create_animated_separator(self, parent):
        """Create animated separator"""
        sep_frame = ttk.Frame(parent, height=4)
        sep_frame.pack(fill="x", pady=15)
        
        self.sep_canvas = Canvas(sep_frame, height=4, highlightthickness=0)
        self.sep_canvas.pack(fill="x")
        
        self.animate_separator()
        
    def animate_separator(self):
        """Animate separator line"""
        self.sep_canvas.delete("all")
        width = self.sep_canvas.winfo_width()
        if width > 1:
            pulse = (math.sin(time.time() * 3) + 1) / 2
            for i in range(0, width, 3):
                alpha = (math.sin(time.time() * 2 + i * 0.05) + 1) / 2
                intensity = int(50 + alpha * 100 + pulse * 50)
                color = f"#{intensity:02x}{intensity+50:02x}{255:02x}"
                self.sep_canvas.create_line(i, 2, i+2, 2, fill=color, width=4)
        
        self.app.after(100, self.animate_separator)
        
    def create_responsive_mode_selection(self):
        """Create responsive mode selection"""
        mode_frame = ttk.LabelFrame(
            self.scrollable_frame,
            text="📁 Input Method",
            padding=15,
            bootstyle="info"
        )
        mode_frame.pack(fill="x", pady=(0, 15))
        
        # Buttons frame
        button_frame = ttk.Frame(mode_frame)
        button_frame.pack(fill="x")
        
        self.json_btn = ttk.Button(
            button_frame,
            text="📄 JSON File",
            bootstyle="primary",
            command=lambda: self.switch_mode("json")
        )
        self.json_btn.pack(side="left", padx=(0, 5), fill="x", expand=True)
        
        self.manual_btn = ttk.Button(
            button_frame,
            text="✏️ Manual Input",
            bootstyle="primary-outline",
            command=lambda: self.switch_mode("manual")
        )
        self.manual_btn.pack(side="left", padx=(5, 5), fill="x", expand=True)
        
        self.qr_btn = ttk.Button(
            button_frame,
            text="📱 QR Code",
            bootstyle="primary-outline",
            command=lambda: self.switch_mode("qr")
        )
        self.qr_btn.pack(side="left", padx=(5, 0), fill="x", expand=True)
        
        # Description
        self.mode_desc = ttk.Label(
            mode_frame,
            text="Upload medication schedule via JSON configuration file",
            font=("Segoe UI", 10),
            bootstyle="secondary"
        )
        self.mode_desc.pack(pady=(10, 0))
        
    def create_responsive_connection(self):
        """Create responsive connection section with BLE device list"""
        conn_frame = ttk.LabelFrame(
            self.scrollable_frame,
            text="🔗 Device Connection",
            padding=15,
            bootstyle="primary"
        )
        conn_frame.pack(fill="x", padx=20, pady=(0, 20))
        
        ttk.Label(
            conn_frame,
            text="Connect to your pill dispenser via Bluetooth Low Energy",
            font=("Segoe UI", 10),
            bootstyle="secondary"
        ).pack(pady=(0, 10))
        
        device_header_frame = ttk.Frame(conn_frame)
        device_header_frame.pack(fill="x", pady=(0, 10))
        
        ttk.Label(device_header_frame, text="📶 Available BLE Devices:", font=("Segoe UI", 11, "bold")).pack(side="left")
        
        self.refresh_btn = ttk.Button(
            device_header_frame,
            text="🔍 Scan",
            bootstyle="info-outline",
            command=self.refresh_devices
        )
        self.refresh_btn.pack(side="right")
        
        # Create device list frame
        list_frame = ttk.Frame(conn_frame)
        list_frame.pack(fill="both", expand=True, pady=(0, 10))
        
        # Create Treeview for device list
        columns = ("name", "address", "status")
        self.device_tree = ttk.Treeview(
            list_frame,
            columns=columns,
            show="headings",
            height=6,
            bootstyle="primary"
        )
        
        # Configure columns
        self.device_tree.heading("name", text="Device Name")
        self.device_tree.heading("address", text="MAC Address")
        self.device_tree.heading("status", text="Status")
        
        self.device_tree.column("name", width=200, minwidth=150)
        self.device_tree.column("address", width=150, minwidth=120)
        self.device_tree.column("status", width=100, minwidth=80)
        
        # Add scrollbar for device list
        device_scrollbar = ttk.Scrollbar(list_frame, orient="vertical", command=self.device_tree.yview)
        self.device_tree.configure(yscrollcommand=device_scrollbar.set)
        
        self.device_tree.pack(side="left", fill="both", expand=True)
        device_scrollbar.pack(side="right", fill="y")
        
        # Bind selection event
        self.device_tree.bind("<<TreeviewSelect>>", self.on_device_select)
        
        # Selected device info
        self.selected_device_var = tk.StringVar(value="No device selected")
        selected_frame = ttk.Frame(conn_frame)
        selected_frame.pack(fill="x")
        
        ttk.Label(selected_frame, text="Selected Device:", font=("Segoe UI", 10, "bold")).pack(side="left")
        ttk.Label(selected_frame, textvariable=self.selected_device_var, font=("Segoe UI", 10)).pack(side="left", padx=(10, 0))
        
        self.refresh_ble_devices()
        
    def create_responsive_upload(self):
        """Create responsive upload section"""
        upload_frame = ttk.LabelFrame(
            self.scrollable_frame,
            text="📤 Medication Data Upload",
            padding=15,
            bootstyle="warning"
        )
        upload_frame.pack(fill="x", pady=(0, 15))
        
        # Upload button
        self.file_btn = ttk.Button(
            upload_frame,
            text="📂 Choose JSON File",
            bootstyle="warning-outline",
            command=self.choose_file
        )
        self.file_btn.pack(fill="x", pady=(0, 10))
        
        # File info
        info_frame = ttk.Frame(upload_frame)
        info_frame.pack(fill="x")
        
        self.file_label = ttk.Label(
            info_frame,
            text="No medication data loaded",
            font=("Segoe UI", 10, "italic"),
            bootstyle="secondary"
        )
        self.file_label.pack(side="left")
        
        self.summary_label = ttk.Label(
            info_frame,
            text="",
            font=("Segoe UI", 9, "bold"),
            bootstyle="info"
        )
        self.summary_label.pack(side="right")
        
    def create_manual_input_section(self):
        """Create manual medication input section"""
        self.manual_frame = ttk.LabelFrame(
            self.scrollable_frame,
            text="✏️ Manual Medication Entry",
            padding=15,
            bootstyle="success"
        )
        # Initially hidden
        
        # Medication details frame
        med_details_frame = ttk.LabelFrame(
            self.manual_frame,
            text="💊 Medication Details",
            padding=10,
            bootstyle="primary"
        )
        med_details_frame.pack(fill="x", pady=(0, 10))
        
        # Tube selection
        tube_frame = ttk.Frame(med_details_frame)
        tube_frame.pack(fill="x", pady=5)
        ttk.Label(tube_frame, text="🏺 Tube:", width=12).pack(side="left")
        self.tube_var = ttk.StringVar()
        tube_combo = ttk.Combobox(
            tube_frame,
            textvariable=self.tube_var,
            values=["tube1", "tube2", "tube3", "tube4", "tube5", "tube6"],
            state="readonly",
            width=15
        )
        tube_combo.pack(side="left", padx=(5, 0))
        
        # Medication type
        type_frame = ttk.Frame(med_details_frame)
        type_frame.pack(fill="x", pady=5)
        ttk.Label(type_frame, text="💊 Type:", width=12).pack(side="left")
        self.med_type_var = ttk.StringVar()
        ttk.Entry(type_frame, textvariable=self.med_type_var, width=25).pack(side="left", padx=(5, 0))
        
        # Amount
        amount_frame = ttk.Frame(med_details_frame)
        amount_frame.pack(fill="x", pady=5)
        ttk.Label(amount_frame, text="📦 Amount:", width=12).pack(side="left")
        self.amount_var = ttk.StringVar()
        ttk.Entry(amount_frame, textvariable=self.amount_var, width=10).pack(side="left", padx=(5, 0))
        ttk.Label(amount_frame, text="tablets").pack(side="left", padx=(5, 0))
        
        # Schedule section
        schedule_frame = ttk.LabelFrame(
            self.manual_frame,
            text="⏰ Dosage Schedule",
            padding=10,
            bootstyle="warning"
        )
        schedule_frame.pack(fill="x", pady=(0, 10))
        
        # Add schedule controls
        add_schedule_frame = ttk.Frame(schedule_frame)
        add_schedule_frame.pack(fill="x", pady=5)
        
        ttk.Label(add_schedule_frame, text="🕐 Time:", width=8).pack(side="left")
        self.schedule_time_var = ttk.StringVar()
        time_entry = ttk.Entry(add_schedule_frame, textvariable=self.schedule_time_var, width=8)
        time_entry.pack(side="left", padx=(5, 10))
        time_entry.insert(0, "HH:MM")
        time_entry.bind("<FocusIn>", lambda e: time_entry.delete(0, "end") if time_entry.get() == "HH:MM" else None)
        time_entry.bind("<FocusOut>", lambda e: time_entry.insert(0, "HH:MM") if time_entry.get() == "" else None)
        
        ttk.Label(add_schedule_frame, text="💊 Dosage:", width=8).pack(side="left")
        self.schedule_dosage_var = ttk.StringVar()
        dosage_entry = ttk.Entry(add_schedule_frame, textvariable=self.schedule_dosage_var, width=15)
        dosage_entry.pack(side="left", padx=(5, 10))
        dosage_entry.insert(0, "1 tablet")
        dosage_entry.bind("<FocusIn>", lambda e: dosage_entry.delete(0, "end") if dosage_entry.get() == "1 tablet" else None)
        dosage_entry.bind("<FocusOut>", lambda e: dosage_entry.insert(0, "1 tablet") if dosage_entry.get() == "" else None)
        
        ttk.Button(
            add_schedule_frame,
            text="➕ Add Schedule",
            bootstyle="success-outline",
            command=self.add_schedule
        ).pack(side="left", padx=(10, 0))
        
        # Schedule list
        self.schedule_listbox = ttk.Treeview(
            schedule_frame,
            columns=("time", "dosage"),
            show="headings",
            height=4
        )
        self.schedule_listbox.heading("time", text="Time")
        self.schedule_listbox.heading("dosage", text="Dosage")
        self.schedule_listbox.column("time", width=100)
        self.schedule_listbox.column("dosage", width=150)
        self.schedule_listbox.pack(fill="x", pady=5)
        
        # Schedule controls
        schedule_controls = ttk.Frame(schedule_frame)
        schedule_controls.pack(fill="x", pady=5)
        
        ttk.Button(
            schedule_controls,
            text="🗑️ Remove Selected",
            bootstyle="danger-outline",
            command=self.remove_schedule
        ).pack(side="left")
        
        # Medication controls
        med_controls_frame = ttk.Frame(self.manual_frame)
        med_controls_frame.pack(fill="x", pady=10)
        
        ttk.Button(
            med_controls_frame,
            text="💾 Save Medication",
            bootstyle="success",
            command=self.save_medication
        ).pack(side="left", padx=(0, 10))
        
        ttk.Button(
            med_controls_frame,
            text="🧹 Clear Form",
            bootstyle="secondary-outline",
            command=self.clear_medication_form
        ).pack(side="left")
        
        # Manual medications list
        manual_list_frame = ttk.LabelFrame(
            self.manual_frame,
            text="📋 Added Medications",
            padding=10,
            bootstyle="info"
        )
        manual_list_frame.pack(fill="both", expand=True, pady=(10, 0))
        
        self.manual_listbox = ttk.Treeview(
            manual_list_frame,
            columns=("tube", "type", "amount", "schedules"),
            show="headings",
            height=6
        )
        self.manual_listbox.heading("tube", text="Tube")
        self.manual_listbox.heading("type", text="Type")
        self.manual_listbox.heading("amount", text="Amount")
        self.manual_listbox.heading("schedules", text="Schedules")
        self.manual_listbox.column("tube", width=80)
        self.manual_listbox.column("type", width=120)
        self.manual_listbox.column("amount", width=80)
        self.manual_listbox.column("schedules", width=200)
        self.manual_listbox.pack(fill="both", expand=True, pady=5)
        
        # Manual list controls
        manual_controls = ttk.Frame(manual_list_frame)
        manual_controls.pack(fill="x", pady=5)
        
        ttk.Button(
            manual_controls,
            text="🗑️ Remove Selected",
            bootstyle="danger-outline",
            command=self.remove_manual_medication
        ).pack(side="left", padx=(0, 10))
        
        ttk.Button(
            manual_controls,
            text="📄 Use Manual Data",
            bootstyle="primary",
            command=self.use_manual_data
        ).pack(side="right")
        
    def create_responsive_preview(self):
        """Create responsive and properly scrollable preview section"""
        preview_frame = ttk.LabelFrame(
            self.scrollable_frame,
            text="👁️ Medication Schedule Preview",
            padding=10,
            bootstyle="info"
        )
        preview_frame.pack(fill="both", expand=True, pady=(0, 15))
        
        # Create frame for canvas and scrollbar
        canvas_frame = ttk.Frame(preview_frame)
        canvas_frame.pack(fill="both", expand=True)
        
        # Create canvas and scrollbar for preview
        self.preview_canvas = Canvas(canvas_frame, highlightthickness=0, height=300)
        self.preview_scrollbar = ttk.Scrollbar(canvas_frame, orient="vertical", command=self.preview_canvas.yview)
        self.preview_content = ttk.Frame(self.preview_canvas)
        
        # Configure scrolling
        self.preview_content.bind(
            "<Configure>",
            lambda e: self.preview_canvas.configure(scrollregion=self.preview_canvas.bbox("all"))
        )
        
        self.preview_canvas.create_window((0, 0), window=self.preview_content, anchor="nw")
        self.preview_canvas.configure(yscrollcommand=self.preview_scrollbar.set)
        
        # Pack canvas and scrollbar
        self.preview_canvas.pack(side="left", fill="both", expand=True)
        self.preview_scrollbar.pack(side="right", fill="y")
        
        # Bind mouse wheel to preview canvas
        def preview_mousewheel(event):
            self.preview_canvas.yview_scroll(int(-1*(event.delta/120)), "units")
            
        self.preview_canvas.bind("<MouseWheel>", preview_mousewheel)
        
        # Configure canvas width
        def configure_preview_canvas(event):
            canvas_width = event.width
            self.preview_canvas.itemconfig(self.preview_canvas.find_all()[0], width=canvas_width-20)
            
        self.preview_canvas.bind('<Configure>', configure_preview_canvas)
        
        # Initial empty state
        self.show_empty_preview()
        
    def create_responsive_status(self):
        """Create responsive status section"""
        status_frame = ttk.LabelFrame(
            self.scrollable_frame,
            text="📊 Device Status",
            padding=15,
            bootstyle="secondary"
        )
        status_frame.pack(fill="x", pady=(0, 15))
        
        # Progress bar
        self.progress = ttk.Progressbar(
            status_frame,
            mode="indeterminate",
            bootstyle="success-striped"
        )
        self.progress.pack(fill="x", pady=(0, 10))
        
        # Status label
        self.status_label = ttk.Label(
            status_frame,
            text="✅ Ready for configuration",
            font=("Segoe UI", 11, "bold"),
            bootstyle="success"
        )
        self.status_label.pack()
        
        self.chunk_progress_label = ttk.Label(
            status_frame,
            text="",
            font=("Segoe UI", 9),
            bootstyle="info"
        )
        self.chunk_progress_label.pack(pady=(5, 0))
        
    def create_responsive_submit(self):
        """Create responsive submit section"""
        submit_frame = ttk.LabelFrame(
            self.scrollable_frame,
            text="🚀 Send Configuration",
            padding=20,
            bootstyle="danger"
        )
        submit_frame.pack(fill="x", pady=(0, 15))
        
        # Checklist
        checklist_frame = ttk.Frame(submit_frame)
        checklist_frame.pack(fill="x", pady=(0, 15))
        
        ttk.Label(
            checklist_frame,
            text="📋 Pre-Submit Checklist:",
            font=("Segoe UI", 12, "bold"),
            bootstyle="info"
        ).pack(anchor="w")
        
        checklist_items = [
            "📶 Device connected via BLE",
            "📄 Medication data loaded and verified",
            "🏺 Dispenser is empty and ready",
            "⚠️ Safety protocols acknowledged"
        ]
        
        for item in checklist_items:
            ttk.Label(
                checklist_frame,
                text=f"  {item}",
                font=("Segoe UI", 10),
                bootstyle="secondary"
            ).pack(anchor="w", padx=20)
        
        # Submit button
        self.submit_btn = ttk.Button(
            submit_frame,
            text="📡 SUBMIT TO DISPENSER",
            bootstyle="danger",
            command=self.submit_data
        )
        self.submit_btn.pack(fill="x", pady=10)
        
        # Warning
        ttk.Label(
            submit_frame,
            text="⚠️ WARNING: This will configure the pill dispenser with the loaded medication schedule.",
            font=("Segoe UI", 10, "bold"),
            bootstyle="warning"
        ).pack()
        
    def create_clear_section(self):
        """Create clear/reset section"""
        clear_frame = ttk.LabelFrame(
            self.scrollable_frame,
            text="🧹 Reset & Clear",
            padding=15,
            bootstyle="secondary"
        )
        clear_frame.pack(fill="x", pady=(0, 20))
        
        button_frame = ttk.Frame(clear_frame)
        button_frame.pack(fill="x")
        
        self.clear_data_btn = ttk.Button(
            button_frame,
            text="🗑️ Clear Data",
            bootstyle="secondary-outline",
            command=self.clear_data
        )
        self.clear_data_btn.pack(side="left", fill="x", expand=True, padx=(0, 5))
        
        self.reset_app_btn = ttk.Button(
            button_frame,
            text="🔄 Reset App",
            bootstyle="secondary-outline",
            command=self.reset_app
        )
        self.reset_app_btn.pack(side="right", fill="x", expand=True, padx=(5, 0))
        
    def switch_mode(self, mode):
        """Switch between JSON, Manual, and QR modes"""
        self.upload_mode.set(mode)
        
        if mode == "json":
            self.json_btn.configure(bootstyle="primary")
            self.manual_btn.configure(bootstyle="primary-outline")
            self.qr_btn.configure(bootstyle="primary-outline")
            self.mode_desc.configure(text="Upload medication schedule via JSON configuration file")
            self.file_btn.configure(text="📂 Choose JSON File")
            self.manual_frame.pack_forget()
        elif mode == "manual":
            self.json_btn.configure(bootstyle="primary-outline")
            self.manual_btn.configure(bootstyle="primary")
            self.qr_btn.configure(bootstyle="primary-outline")
            self.mode_desc.configure(text="Manually enter medication details and schedules")
            self.manual_frame.pack(fill="both", expand=True, pady=(0, 15))
        else:  # QR mode
            self.json_btn.configure(bootstyle="primary-outline")
            self.manual_btn.configure(bootstyle="primary-outline")
            self.qr_btn.configure(bootstyle="primary")
            self.mode_desc.configure(text="Upload QR code containing medication data")
            self.file_btn.configure(text="📱 Upload QR Code")
            self.manual_frame.pack_forget()
            
        self.show_empty_preview()
    
    def refresh_devices(self):
        """Refresh BLE devices with animation"""
        self.refresh_btn.configure(text="🔄 Scanning...", state="disabled")
        
        def refresh_task():
            time.sleep(0.5)
            self.refresh_ble_devices()
            self.app.after(0, lambda: [
                self.refresh_btn.configure(text="🔍 Scan", state="normal"),
                self.show_notification("BLE device scan completed!", "success")
            ])
            
        threading.Thread(target=refresh_task, daemon=True).start()
        
    def choose_file(self):
        """Choose file with proper handling"""
        mode = self.upload_mode.get()
        
        if mode == "json":
            filetypes = [("JSON Files", "*.json"), ("All Files", "*.*")]
            path = filedialog.askopenfilename(filetypes=filetypes)
            
            if path:
                self.process_json_file(path)
        else:
            self.show_notification("QR code processing will be implemented by you", "info")
            
    def process_json_file(self, path):
        """Process JSON file with validation"""
        try:
            with open(path, 'r', encoding='utf-8') as f:
                data = json.load(f)
                
            # Validate format
            if not isinstance(data, list):
                raise ValueError("JSON must contain a list of medications")
                
            # Validate each medication
            for med in data:
                required_fields = ['tube', 'type', 'amount', 'time_to_take']
                for field in required_fields:
                    if field not in med:
                        raise ValueError(f"Missing required field: {field}")
                        
                if not isinstance(med['time_to_take'], list):
                    raise ValueError("time_to_take must be a list")
                    
                for schedule in med['time_to_take']:
                    if 'time' not in schedule or 'dosage' not in schedule:
                        raise ValueError("Each schedule must have 'time' and 'dosage'")
            
            # Store data
            self.medication_data = data
            self.file_var.set(path)
            filename = os.path.basename(path)
            
            # Calculate stats
            total_medications = len(data)
            total_tubes = len(set(med['tube'] for med in data))
            total_schedules = sum(len(med['time_to_take']) for med in data)
            
            # Update UI
            self.file_label.configure(text=f"📄 {filename}")
            self.summary_label.configure(text=f"{total_medications} meds | {total_tubes} tubes | {total_schedules} schedules")
            
            # Display preview
            self.display_medication_preview(data)
            
            self.show_notification(f"Loaded {total_medications} medications from {total_tubes} tubes", "success")
            
        except Exception as e:
            self.show_notification(f"Error loading file: {str(e)}", "error")
            
    def display_medication_preview(self, medications):
        """Display medication data in preview with better formatting"""
        # Clear existing content
        for widget in self.preview_content.winfo_children():
            widget.destroy()
            
        if not medications:
            self.show_empty_preview()
            return
            
        # Display each medication
        for i, med in enumerate(medications):
            # Medication card
            med_frame = ttk.LabelFrame(
                self.preview_content,
                text=f"💊 {med.get('type', 'Unknown')} - {med.get('tube', 'Unknown Tube')}",
                padding=15,
                bootstyle="primary" if i % 2 == 0 else "success"
            )
            med_frame.pack(fill="x", pady=8, padx=10)
            
            # Info row
            info_frame = ttk.Frame(med_frame)
            info_frame.pack(fill="x", pady=(0, 10))
            
            ttk.Label(
                info_frame,
                text=f"📦 Amount: {med.get('amount', 0)} tablets",
                font=("Segoe UI", 10, "bold"),
                bootstyle="info"
            ).pack(side="left")
            
            # Calculate daily total
            total_daily = sum(int(schedule.get('dosage', '0').split()[0]) 
                            for schedule in med.get('time_to_take', []) 
                            if schedule.get('dosage', '').split()[0].isdigit())
            
            ttk.Label(
                info_frame,
                text=f"📅 Daily: {total_daily} tablets",
                font=("Segoe UI", 10),
                bootstyle="secondary"
            ).pack(side="right")
            
            # Schedule
            ttk.Label(
                med_frame,
                text="⏰ Schedule:",
                font=("Segoe UI", 10, "bold")
            ).pack(anchor="w", pady=(0, 5))
            
            for schedule in med.get('time_to_take', []):
                schedule_frame = ttk.Frame(med_frame)
                schedule_frame.pack(fill="x", padx=20, pady=2)
                
                ttk.Label(
                    schedule_frame,
                    text=f"🕐 {schedule.get('time', 'Unknown')} - {schedule.get('dosage', 'Unknown dosage')}",
                    font=("Segoe UI", 9),
                    bootstyle="secondary"
                ).pack(side="left")
                
    def submit_data(self):
        """Submit data to dispenser via BLE"""
        device_name = self.ble_var.get()
        mode = self.upload_mode.get()
        
        if not device_name:
            messagebox.showerror("Error", "Please select a BLE device!")
            return
            
        if mode == "json" and not self.medication_data:
            messagebox.showerror("Error", "Please upload medication data!")
            return
            
        # Confirmation
        if mode == "json":
            med_count = len(self.medication_data)
            confirm_msg = f"Submit {med_count} medications via {device_name}?"
        else:
            confirm_msg = f"Submit QR data via {device_name}?"
            
        if not messagebox.askyesno("Confirm", confirm_msg):
            return
            
        # Start submission
        self.is_sending = True
        self.submit_btn.configure(text="📡 SUBMITTING...", state="disabled")
        self.progress.start()
        self.status_label.configure(text="📡 Submitting...", bootstyle="warning")
        
        def submit_task():
            try:
                if mode == "json":
                    data = "#START#" + json.dumps(self.medication_data, indent=2) + "#END#"
                else:
                    data = self.qr_data
                    
                # Simulate submission steps
                steps = [
                    "🔍 Validating data...",
                    f"📶 Connecting to {device_name}...",
                    "📡 Transmitting...",
                    "⚙️ Configuring...",
                    "✅ Complete!"
                ]
                
                for step in steps:
                    time.sleep(0.8)
                    self.app.after(0, lambda s=step: self.status_label.configure(text=s))
                
                CHUNK_SIZE = 20  # BLE typically has smaller MTU
                CHUNK_DELAY = 0.2  # Slightly longer delay for BLE
                data_bytes = data.encode('utf-8')
                total_chunks = (len(data_bytes) + CHUNK_SIZE - 1) // CHUNK_SIZE
                
                # Get device address from stored mapping
                device_address = self.ble_devices.get(device_name)
                if not device_address:
                    raise Exception(f"Device address not found for {device_name}")
                
                # Run BLE transmission in async context
                asyncio.run(self.send_ble_data(device_address, data_bytes, CHUNK_SIZE, CHUNK_DELAY, total_chunks))
                
                self.app.after(0, lambda: [
                    self.show_notification("Successfully submitted to dispenser!", "success"),
                    self.status_label.configure(text="✅ Submission successful", bootstyle="success"),
                    self.chunk_progress_label.configure(text="📦 All chunks sent successfully"),
                    messagebox.showinfo("Success", f"Data successfully sent to {device_name}!")
                ])
                
            except Exception as e:
                self.app.after(0, lambda err=e: [
                    self.show_notification(f"Submission failed: {str(err)}", "error"),
                    self.status_label.configure(text="❌ Submission failed", bootstyle="danger"),
                    self.chunk_progress_label.configure(text="❌ Transmission interrupted"),
                    messagebox.showerror("Error", f"Failed to submit: {str(err)}")
                ])
            finally:
                self.app.after(0, self.reset_submit_button)
                
        threading.Thread(target=submit_task, daemon=True).start()

    async def send_ble_data(self, device_address, data_bytes, chunk_size, chunk_delay, total_chunks):
        """Send data via BLE connection with improved error handling"""
        if device_address.startswith("SIM:") or device_address == "00:00:00:00:00":
            # Simulate BLE transmission
            for i in range(0, len(data_bytes), chunk_size):
                chunk_num = (i // chunk_size) + 1
                chunk_data = data_bytes[i:i+chunk_size]
                
                progress_text = f"📦 Simulating chunk {chunk_num}/{total_chunks} ({len(chunk_data)} bytes)"
                self.app.after(0, lambda t=progress_text: self.chunk_progress_label.configure(text=t))
                
                await asyncio.sleep(chunk_delay)
            return
        
        # Standard BLE service UUID for data transmission
        SERVICE_UUID = "12345678-1234-1234-1234-123456789abc"
        CHARACTERISTIC_UUID = "87654321-4321-4321-4321-cba987654321"
        
        try:
            async with BleakClient(device_address, timeout=10.0) as client:
                if not client.is_connected:
                    raise Exception("Failed to connect to BLE device")
                
                await asyncio.sleep(1)  # Give device time to initialize
                
                for i in range(0, len(data_bytes), chunk_size):
                    chunk_num = (i // chunk_size) + 1
                    chunk_data = data_bytes[i:i+chunk_size]
                    
                    progress_text = f"📦 Sending chunk {chunk_num}/{total_chunks} ({len(chunk_data)} bytes)"
                    self.app.after(0, lambda t=progress_text: self.chunk_progress_label.configure(text=t))
                    
                    # Send chunk via BLE characteristic
                    await client.write_gatt_char(CHARACTERISTIC_UUID, chunk_data)
                    await asyncio.sleep(chunk_delay)
                
                await asyncio.sleep(0.5)
                
        except Exception as e:
            raise Exception(f"BLE transmission failed: {str(e)}")

    def get_ble_devices(self):
        """Get available BLE devices with improved error handling"""
        if not BLEAK_AVAILABLE:
            messagebox.showerror("BLE Error", "BLE library not available. Please install bleak: pip install bleak")
            return {"Simulation Device": "00:00:00:00:00:00"}
        
        try:
            if hasattr(asyncio, 'WindowsProactorEventLoopPolicy'):
                # Use ProactorEventLoop on Windows for better BLE compatibility
                asyncio.set_event_loop_policy(asyncio.WindowsProactorEventLoopPolicy())
            
            # Create new event loop to avoid conflicts
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            
            try:
                devices = loop.run_until_complete(self.scan_ble_devices())
            finally:
                loop.close()
                
            return devices if devices else {"No BLE devices found": "00:00:00:00:00:00"}
            
        except Exception as e:
            error_msg = str(e)
            print(f"BLE scan error: {error_msg}")
            
            if "tp_basicsize" in error_msg or "WinRT" in error_msg:
                messagebox.showwarning(
                    "BLE Compatibility Issue", 
                    "Windows BLE compatibility issue detected.\n\n"
                    "Try these solutions:\n"
                    "1. Update Python to latest version\n"
                    "2. Reinstall bleak: pip uninstall bleak && pip install bleak\n"
                    "3. Use simulation mode for testing\n\n"
                    "Using simulation device for now."
                )
            else:
                messagebox.showerror("BLE Error", f"BLE scan failed: {error_msg}")
            
            # Return simulation device as fallback
            return {"Simulation Device (BLE Error)": "SIM:00:00:00:00:00"}
    
    def on_device_select(self, event):
        """Handle device selection from list"""
        selection = self.device_tree.selection()
        if selection:
            item = self.device_tree.item(selection[0])
            device_name = item['values'][0]
            device_address = item['values'][1]
            self.selected_device_var.set(f"{device_name} ({device_address})")
            
            # Store selected device info
            self.selected_ble_device = {
                'name': device_name,
                'address': device_address
            }
        else:
            self.selected_device_var.set("No device selected")
            self.selected_ble_device = None

    def refresh_ble_devices(self):
        """Refresh BLE devices list and populate the tree view"""
        # Clear existing items
        for item in self.device_tree.get_children():
            self.device_tree.delete(item)
            
        self.ble_devices = self.get_ble_devices()
        
        for device_name, device_address in self.ble_devices.items():
            # Determine status based on device type
            if "Simulation" in device_name or "SIM:" in device_address:
                status = "Simulation"
            elif "No BLE devices" in device_name:
                status = "Not Found"
            else:
                status = "Available"
            
            self.device_tree.insert("", "end", values=(device_name, device_address, status))
        
        # Auto-select first available device if any
        children = self.device_tree.get_children()
        if children:
            self.device_tree.selection_set(children[0])
            self.on_device_select(None)

    async def scan_ble_devices(self):
        """Async BLE device scanning with timeout and error handling"""
        devices = {}
        
        try:
            scanner = BleakScanner()
            
            # Use shorter timeout to avoid hanging
            discovered_devices = await asyncio.wait_for(
                scanner.discover(), 
                timeout=10.0  # Increased timeout for better device discovery
            )
            
            for device in discovered_devices:
                device_name = device.name if device.name else f"Unknown Device"
                if device.address:
                    # Add RSSI info if available
                    rssi_info = f" (RSSI: {device.rssi})" if hasattr(device, 'rssi') and device.rssi else ""
                    full_name = f"{device_name}{rssi_info}"
                    devices[full_name] = device.address
            
            # Add some example devices for testing if no real devices found
            if not devices:
                devices["No BLE devices found"] = "00:00:00:00:00:00"
                        
        except asyncio.TimeoutError:
            print("BLE scan timeout - no devices found")
            devices["Scan Timeout"] = "00:00:00:00:00:00"
        except Exception as e:
            print(f"BLE scan exception: {e}")
            devices[f"Scan Error: {str(e)[:30]}..."] = "00:00:00:00:00:00"
        
        return devices

    def send_to_device(self):
        """Send configuration to selected BLE device"""
        if not hasattr(self, 'selected_ble_device') or not self.selected_ble_device:
            messagebox.showerror("Error", "Please select a BLE device from the list first!")
            return
            
        selected_address = self.selected_ble_device['address']
        selected_name = self.selected_ble_device['name']
        
        if not selected_address or selected_address == "00:00:00:00:00:00":
            messagebox.showwarning("Warning", f"Cannot connect to {selected_name}. Please select a valid BLE device.")
            return

    def reset_submit_button(self):
        """Reset submit button"""
        self.is_sending = False
        self.progress.stop()
        self.submit_btn.configure(text="📡 SUBMIT TO DISPENSER", state="normal")
        self.chunk_progress_label.configure(text="")
        
    def clear_data(self):
        """Clear loaded data"""
        self.medication_data = []
        self.file_var.set("")
        self.qr_data = ""
        # Added manual data clearing
        self.manual_medications = []
        self.current_med_schedules = []
        self.update_manual_listbox()
        self.clear_medication_form()
        
        self.file_label.configure(text="No medication data loaded")
        self.summary_label.configure(text="")
        self.show_empty_preview()
        self.show_notification("Data cleared", "info")
        
    def reset_app(self):
        """Reset entire application"""
        self.clear_data()
        self.ble_var.set("")
        self.upload_mode.set("json")
        self.switch_mode("json")
        self.status_label.configure(text="✅ Ready for configuration", bootstyle="success")
        self.chunk_progress_label.configure(text="")
        self.show_notification("Application reset", "info")

    def show_notification(self, message, type_="info"):
        """Show temporary notification"""
        colors = {
            "success": "success",
            "error": "danger", 
            "info": "info",
            "warning": "warning"
        }
        
        notification = ttk.Label(
            self.scrollable_frame,
            text=f"{'✅' if type_=='success' else '❌' if type_=='error' else 'ℹ️'} {message}",
            font=("Segoe UI", 10, "bold"),
            bootstyle=colors.get(type_, "info")
        )
        notification.pack(pady=5)
        self.app.after(3000, notification.destroy)

    def run(self):
        """Start the application"""
        self.app.mainloop()
    
    def add_schedule(self):
        """Add a schedule to current medication"""
        time_str = self.schedule_time_var.get().strip()
        dosage_str = self.schedule_dosage_var.get().strip()
        
        if not time_str or not dosage_str:
            messagebox.showerror("Error", "Please enter both time and dosage!")
            return
            
        # Validate time format
        try:
            time_parts = time_str.split(":")
            if len(time_parts) != 2:
                raise ValueError()
            hour, minute = int(time_parts[0]), int(time_parts[1])
            if not (0 <= hour <= 23 and 0 <= minute <= 59):
                raise ValueError()
        except ValueError:
            messagebox.showerror("Error", "Please enter time in HH:MM format (24-hour)!")
            return
            
        # Add to schedule list
        schedule_item = {"time": time_str, "dosage": dosage_str}
        self.current_med_schedules.append(schedule_item)
        
        # Update listbox
        self.schedule_listbox.insert("", "end", values=(time_str, dosage_str))
        
        # Clear inputs
        self.schedule_time_var.set("")
        self.schedule_dosage_var.set("")
        
    def remove_schedule(self):
        """Remove selected schedule"""
        selection = self.schedule_listbox.selection()
        if not selection:
            messagebox.showwarning("Warning", "Please select a schedule to remove!")
            return
            
        # Get index and remove
        item = selection[0]
        index = self.schedule_listbox.index(item)
        self.current_med_schedules.pop(index)
        self.schedule_listbox.delete(item)
        
    def save_medication(self):
        """Save current medication to manual list"""
        tube = self.tube_var.get()
        med_type = self.med_type_var.get().strip()
        amount_str = self.amount_var.get().strip()
        
        if not tube or not med_type or not amount_str:
            messagebox.showerror("Error", "Please fill in all medication details!")
            return
            
        if not self.current_med_schedules:
            messagebox.showerror("Error", "Please add at least one dosage schedule!")
            return
            
        try:
            amount = int(amount_str)
            if amount <= 0:
                raise ValueError()
        except ValueError:
            messagebox.showerror("Error", "Please enter a valid positive number for amount!")
            return
            
        # Check if tube already used
        for med in self.manual_medications:
            if med["tube"] == tube:
                if not messagebox.askyesno("Confirm", f"Tube {tube} already has medication. Replace it?"):
                    return
                self.manual_medications.remove(med)
                break
                
        # Create medication object
        medication = {
            "tube": tube,
            "type": med_type,
            "amount": amount,
            "time_to_take": self.current_med_schedules.copy()
        }
        
        self.manual_medications.append(medication)
        self.update_manual_listbox()
        self.clear_medication_form()
        self.show_notification(f"Added {med_type} to {tube}", "success")
        
    def clear_medication_form(self):
        """Clear medication input form"""
        self.tube_var.set("")
        self.med_type_var.set("")
        self.amount_var.set("")
        self.schedule_time_var.set("")
        self.schedule_dosage_var.set("")
        self.current_med_schedules.clear()
        
        # Clear schedule listbox
        for item in self.schedule_listbox.get_children():
            self.schedule_listbox.delete(item)
            
    def update_manual_listbox(self):
        """Update manual medications listbox"""
        # Clear existing items
        for item in self.manual_listbox.get_children():
            self.manual_listbox.delete(item)
            
        # Add medications
        for med in self.manual_medications:
            schedules_str = ", ".join([f"{s['time']} ({s['dosage']})" for s in med['time_to_take']])
            self.manual_listbox.insert("", "end", values=(med['tube'], med['type'], f"{med['amount']} tablets", schedules_str))
            
    def remove_manual_medication(self):
        """Remove selected manual medication"""
        selection = self.manual_listbox.selection()
        if not selection:
            messagebox.showwarning("Warning", "Please select a medication to remove!")
            return
            
        item = selection[0]
        index = self.manual_listbox.index(item)
        removed_med = self.manual_medications.pop(index)
        self.manual_listbox.delete(item)
        self.show_notification(f"Removed {removed_med['type']}", "info")
        
    def use_manual_data(self):
        """Use manually entered data as medication data"""
        if not self.manual_medications:
            messagebox.showerror("Error", "No manual medications added!")
            return
            
        self.medication_data = self.manual_medications.copy()
        
        # Format the data with the same structure as JSON submission
        formatted_data = "#START#" + json.dumps(self.medication_data, indent=2) + "#END#"
        
        # Store the formatted data for potential use
        self.formatted_manual_data = formatted_data
        
        # Update UI
        total_medications = len(self.medication_data)
        total_tubes = len(set(med['tube'] for med in self.medication_data))
        total_schedules = sum(len(med['time_to_take']) for med in self.medication_data)
        
        self.file_label.configure(text="📝 Manual Input Data")
        self.summary_label.configure(text=f"{total_medications} meds | {total_tubes} tubes | {total_schedules} schedules")
        
        # Display preview
        self.display_medication_preview(self.medication_data)
        self.show_notification(f"Using {total_medications} manually entered medications (formatted with #START# #END#)", "success")
    
    def show_empty_preview(self):
        """Show empty preview state"""
        for widget in self.preview_content.winfo_children():
            widget.destroy()
            
        empty_label = ttk.Label(
            self.preview_content,
            text="📋 No medication data to display\nUpload a JSON file to see medication schedule",
            font=("Segoe UI", 12),
            bootstyle="secondary",
            justify="center"
        )
        empty_label.pack(expand=True, pady=50)
        
if __name__ == "__main__":
    app = ResponsiveAutoPillDispenserApp()
    app.run()
