import ctypes
import hid
import queue
import threading
import time
import tkinter as tk
from ctypes import wintypes
from tkinter import messagebox, ttk


# ============================================================
# Tango 2 USB Racing Trainer GUI
#
# Student:
#   04D8:5710 USB Joystick
#
# Master:
#   04D8:F94C USB Agent
#
# Student Switch A:
#   CH5 = 0    released
#   CH5 = 2047 pressed
#
# IMPORTANT:
# Student "Arm" is ONLY a READY signal.
# It is never forwarded to the aircraft.
#
# Master firmware requirements:
# - USB trainer CRSF injection
# - ~150 ms trainer validity failsafe
# - T2TR master status report
# - Instructor stick override bit (0x04)
# ============================================================

STUDENT_VID = 0x04D8
STUDENT_PID = 0x5710

MASTER_VID = 0x04D8
MASTER_PID = 0xF94C

CRSF_SYNC = 0xC8
CRSF_RC_CHANNELS_PACKED = 0x16

CRSF_MIN = 172
CRSF_CENTER = 992
CRSF_MAX = 1811

SEND_PERIOD = 0.020          # 50 Hz
DEVICE_CHECK_PERIOD = 0.10
MASTER_STATUS_MAX_AGE = 0.25

STUDENT_ARM_THRESHOLD = 1500
THROTTLE_ARM_MAX = 300

JOY_RETURNALL = 0x000000FF
JOYERR_NOERROR = 0

PROFILES = {
    "BEGINNER": {
        "roll": 0.75,
        "pitch": 0.75,
        "yaw": 0.65,
        "throttle": 0.75,
    },
    "INTERMEDIATE": {
        "roll": 0.85,
        "pitch": 0.85,
        "yaw": 0.80,
        "throttle": 0.90,
    },
    "ADVANCED": {
        "roll": 1.00,
        "pitch": 1.00,
        "yaw": 1.00,
        "throttle": 1.00,
    },
}

APP_VERSION = "1.1"

# Large at-a-glance state banner.
# Colors are intentionally high-contrast for quick instructor recognition.
STATE_VISUALS = {
    "STOPPED": ("#3a3a3a", "#ffffff", "TRAINER STOPPED"),
    "CONNECTING": ("#355c7d", "#ffffff", "CONNECTING"),
    "MASTER CONTROL - NOT READY": ("#2457a7", "#ffffff", "MASTER CONTROL"),
    "READY - MASTER CONTROL": ("#c58b00", "#ffffff", "STUDENT READY"),
    "ACTIVE - STUDENT CONTROL": ("#18864b", "#ffffff", "STUDENT CONTROL"),
    "OVERRIDE - MASTER CONTROL": ("#c84b31", "#ffffff", "INSTRUCTOR OVERRIDE"),
    "MASTER CONTROL - INPUT INVALID": ("#b85c00", "#ffffff", "MASTER CONTROL"),
    "MASTER STATUS LOST": ("#a61b1b", "#ffffff", "MASTER STATUS LOST"),
    "TRAINER STOPPED": ("#6f1d1b", "#ffffff", "TRAINER STOPPED"),
    "STOPPING": ("#555555", "#ffffff", "STOPPING"),
}


class JOYINFOEX(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("dwFlags", wintypes.DWORD),
        ("dwXpos", wintypes.DWORD),
        ("dwYpos", wintypes.DWORD),
        ("dwZpos", wintypes.DWORD),
        ("dwRpos", wintypes.DWORD),
        ("dwUpos", wintypes.DWORD),
        ("dwVpos", wintypes.DWORD),
        ("dwButtons", wintypes.DWORD),
        ("dwButtonNumber", wintypes.DWORD),
        ("dwPOV", wintypes.DWORD),
        ("dwReserved1", wintypes.DWORD),
        ("dwReserved2", wintypes.DWORD),
    ]


def axis_to_crsf(value):
    value = max(0, min(65535, value))
    return round(
        CRSF_MIN +
        (value / 65535.0) * (CRSF_MAX - CRSF_MIN)
    )


def limit_axis(value, authority):
    value = CRSF_CENTER + int(
        (value - CRSF_CENTER) * authority
    )
    return max(CRSF_MIN, min(CRSF_MAX, value))


def limit_throttle(value, maximum):
    maximum_value = CRSF_MIN + int(
        (CRSF_MAX - CRSF_MIN) * maximum
    )
    return max(CRSF_MIN, min(maximum_value, value))


def apply_profile(profile_name, roll, pitch, yaw, throttle):
    p = PROFILES[profile_name]
    return (
        limit_axis(roll, p["roll"]),
        limit_axis(pitch, p["pitch"]),
        limit_axis(yaw, p["yaw"]),
        limit_throttle(throttle, p["throttle"]),
    )


def crc8_dvb_s2(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0xD5) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def pack_channels(channels):
    accumulator = 0
    bits = 0
    packed = bytearray()

    for channel in channels:
        accumulator |= (channel & 0x07FF) << bits
        bits += 11

        while bits >= 8:
            packed.append(accumulator & 0xFF)
            accumulator >>= 8
            bits -= 8

    if bits:
        packed.append(accumulator & 0xFF)

    if len(packed) != 22:
        raise RuntimeError(
            f"Expected 22 CRSF channel bytes, got {len(packed)}"
        )

    return bytes(packed)


def make_crsf_rc_frame(channels):
    payload = pack_channels(channels)
    body = bytes([CRSF_RC_CHANNELS_PACKED]) + payload
    crc = crc8_dvb_s2(body)

    return (
        bytes([CRSF_SYNC, 24, CRSF_RC_CHANNELS_PACKED])
        + payload
        + bytes([crc])
    )


def get_raw_ch5(dev):
    """Read Student Switch A from raw Tango joystick CH5."""
    newest = None

    while True:
        data = dev.read(64)
        if not data:
            break
        newest = bytes(data)

    if newest is None or len(newest) < 13:
        return None

    return (
        newest[11]
        | (newest[12] << 8)
    ) & 0x07FF


def read_master_status(dev):
    """
    Read custom T2TR status from Master Tango.

    Returns:
      (handoff, trainer_valid, instructor_override, timeout)
    """
    newest_status = None

    while True:
        data = dev.read(64)
        if not data:
            break

        b = bytes(data)
        pos = b.find(b"T2TR")

        if pos < 0 or len(b) < pos + 7:
            continue

        version = b[pos + 4]
        if version != 1:
            continue

        flags = b[pos + 5]
        timeout = b[pos + 6]

        handoff = bool(flags & 0x01)
        trainer_valid = bool(flags & 0x02)
        instructor_override = bool(flags & 0x04)

        newest_status = (
            handoff,
            trainer_valid,
            instructor_override,
            timeout,
        )

    return newest_status


class TrainerWorker(threading.Thread):
    def __init__(self, updates, initial_profile):
        super().__init__(daemon=True)
        self.updates = updates
        self.stop_event = threading.Event()
        self.profile_lock = threading.Lock()
        self.current_profile = initial_profile

        self.student_active = False
        self.running = False

        self.winmm = None
        self.student_hid = None
        self.master = None
        self.joy_id = None

    def emit(self, **kwargs):
        self.updates.put(kwargs)

    def stop(self):
        self.stop_event.set()

    def request_profile(self, profile_name):
        if profile_name not in PROFILES:
            return False, "Unknown profile."

        with self.profile_lock:
            if self.student_active:
                return False, "Release Master D before changing profiles."

            self.current_profile = profile_name

        self.emit(profile=profile_name)
        return True, ""

    def get_profile(self):
        with self.profile_lock:
            return self.current_profile

    def read_joystick(self):
        j = JOYINFOEX()
        j.dwSize = ctypes.sizeof(JOYINFOEX)
        j.dwFlags = JOY_RETURNALL

        if self.winmm.joyGetPosEx(
            self.joy_id, ctypes.byref(j)
        ) != JOYERR_NOERROR:
            return None

        return j

    def find_joystick(self):
        for joy_id in range(16):
            j = JOYINFOEX()
            j.dwSize = ctypes.sizeof(JOYINFOEX)
            j.dwFlags = JOY_RETURNALL

            if self.winmm.joyGetPosEx(
                joy_id, ctypes.byref(j)
            ) == JOYERR_NOERROR:
                return joy_id

        return None

    def close_devices(self):
        if self.student_hid is not None:
            try:
                self.student_hid.close()
            except Exception:
                pass
            self.student_hid = None

        if self.master is not None:
            try:
                self.master.close()
            except Exception:
                pass
            self.master = None

    def run(self):
        self.running = True
        self.emit(
            running=True,
            state="CONNECTING",
            student_connected=False,
            master_connected=False,
        )

        try:
            self.winmm = ctypes.WinDLL("winmm")

            student_devices = hid.enumerate(
                STUDENT_VID, STUDENT_PID
            )
            if not student_devices:
                raise RuntimeError(
                    "Student Tango not found. "
                    "Select USB Joystick (HID)."
                )

            self.student_hid = hid.device()
            self.student_hid.open_path(
                student_devices[0]["path"]
            )
            self.student_hid.set_nonblocking(True)

            self.joy_id = self.find_joystick()
            if self.joy_id is None:
                raise RuntimeError(
                    "No Windows joystick input found "
                    "for the Student Tango."
                )

            master_devices = hid.enumerate(
                MASTER_VID, MASTER_PID
            )
            if not master_devices:
                raise RuntimeError(
                    "Master Tango not found. "
                    "Select USB Agent (HID)."
                )

            self.master = hid.device()
            self.master.open_path(
                master_devices[0]["path"]
            )
            self.master.set_nonblocking(True)

            self.emit(
                student_connected=True,
                master_connected=True,
                state="MASTER CONTROL - NOT READY",
            )

            student_ready = False
            previous_arm_pressed = False

            # Startup always requires A to be released once.
            arm_release_seen = False
            last_ch5 = 0

            master_handoff = False
            master_trainer_valid = False
            master_instructor_override = False
            master_timeout = 0
            last_master_status_time = 0.0

            last_device_check = 0.0
            packet_counter = 0
            packet_rate = 0.0
            rate_window_start = time.monotonic()

            while not self.stop_event.is_set():
                loop_start = time.monotonic()

                # ----------------------------------------------
                # Periodic physical connection check
                # ----------------------------------------------
                if loop_start - last_device_check >= DEVICE_CHECK_PERIOD:
                    last_device_check = loop_start

                    if not hid.enumerate(
                        STUDENT_VID, STUDENT_PID
                    ):
                        raise RuntimeError(
                            "STUDENT DISCONNECTED - "
                            "trainer packets stopped."
                        )

                    if not hid.enumerate(
                        MASTER_VID, MASTER_PID
                    ):
                        raise RuntimeError(
                            "MASTER DISCONNECTED - "
                            "trainer packets stopped."
                        )

                # ----------------------------------------------
                # Student sticks
                # ----------------------------------------------
                j = self.read_joystick()
                if j is None:
                    raise RuntimeError(
                        "Student joystick read failed."
                    )

                raw_throttle = axis_to_crsf(j.dwXpos)
                raw_roll = axis_to_crsf(j.dwYpos)
                raw_pitch = axis_to_crsf(j.dwZpos)
                raw_yaw = axis_to_crsf(j.dwVpos)

                profile_name = self.get_profile()

                roll, pitch, yaw, throttle = apply_profile(
                    profile_name,
                    raw_roll,
                    raw_pitch,
                    raw_yaw,
                    raw_throttle,
                )

                # ----------------------------------------------
                # Student A / READY
                # ----------------------------------------------
                ch5 = get_raw_ch5(self.student_hid)
                if ch5 is not None:
                    last_ch5 = ch5

                arm_pressed = (
                    last_ch5 > STUDENT_ARM_THRESHOLD
                )

                if not arm_pressed:
                    student_ready = False
                    arm_release_seen = True

                rising_edge = (
                    arm_pressed and not previous_arm_pressed
                )

                if rising_edge:
                    if not arm_release_seen:
                        student_ready = False

                    elif raw_throttle <= THROTTLE_ARM_MAX:
                        student_ready = True
                        arm_release_seen = False

                    else:
                        student_ready = False
                        arm_release_seen = False

                previous_arm_pressed = arm_pressed

                # ----------------------------------------------
                # Send student trainer data when READY
                # ----------------------------------------------
                written = 0

                if student_ready:
                    channels = [
                        roll,           # CH1 -> Master Roll
                        throttle,       # CH2 -> Master Throttle
                        yaw,            # CH3 -> Master Yaw
                        pitch,          # CH4 -> Master Pitch
                        CRSF_CENTER,    # CH5  master-only AUX
                        CRSF_CENTER,    # CH6
                        CRSF_CENTER,    # CH7
                        CRSF_CENTER,    # CH8
                        CRSF_CENTER,    # CH9
                        CRSF_CENTER,    # CH10
                        CRSF_CENTER,    # CH11
                        CRSF_CENTER,    # CH12
                        CRSF_CENTER,    # CH13
                        CRSF_CENTER,    # CH14
                        CRSF_CENTER,    # CH15
                        CRSF_CENTER,    # CH16
                    ]

                    frame = make_crsf_rc_frame(channels)
                    hid_payload = (
                        frame + bytes(64 - len(frame))
                    )
                    report = bytes([0]) + hid_payload

                    written = self.master.write(report)
                    if written:
                        packet_counter += 1

                # ----------------------------------------------
                # Master T2TR status
                # ----------------------------------------------
                master_status = read_master_status(self.master)

                if master_status is not None:
                    (
                        master_handoff,
                        master_trainer_valid,
                        master_instructor_override,
                        master_timeout,
                    ) = master_status

                    last_master_status_time = time.monotonic()

                master_status_fresh = (
                    time.monotonic()
                    - last_master_status_time
                ) < MASTER_STATUS_MAX_AGE

                if not master_status_fresh:
                    master_handoff = False
                    master_trainer_valid = False
                    master_instructor_override = False

                self.student_active = (
                    student_ready
                    and master_status_fresh
                    and master_handoff
                    and master_trainer_valid
                    and not master_instructor_override
                )

                # ----------------------------------------------
                # Display/control state
                # ----------------------------------------------
                if not master_status_fresh:
                    state = "MASTER STATUS LOST"

                elif master_instructor_override:
                    state = "OVERRIDE - MASTER CONTROL"

                elif not student_ready:
                    state = "MASTER CONTROL - NOT READY"

                elif (
                    master_handoff
                    and master_trainer_valid
                ):
                    state = "ACTIVE - STUDENT CONTROL"

                elif (
                    master_handoff
                    and not master_trainer_valid
                ):
                    state = "MASTER CONTROL - INPUT INVALID"

                else:
                    state = "READY - MASTER CONTROL"

                # ----------------------------------------------
                # Packet-rate estimate
                # ----------------------------------------------
                now = time.monotonic()
                elapsed = now - rate_window_start

                if elapsed >= 1.0:
                    packet_rate = packet_counter / elapsed
                    packet_counter = 0
                    rate_window_start = now

                self.emit(
                    state=state,
                    student_connected=True,
                    master_connected=True,
                    student_ready=student_ready,
                    handoff=master_handoff,
                    trainer_valid=master_trainer_valid,
                    override=master_instructor_override,
                    master_timeout=master_timeout,
                    profile=profile_name,
                    packet_rate=packet_rate,
                    arm_ch5=last_ch5,
                    raw_roll=raw_roll,
                    roll=roll,
                    raw_pitch=raw_pitch,
                    pitch=pitch,
                    raw_yaw=raw_yaw,
                    yaw=yaw,
                    raw_throttle=raw_throttle,
                    throttle=throttle,
                    written=written,
                )

                sleep_time = SEND_PERIOD - (
                    time.monotonic() - loop_start
                )
                if sleep_time > 0:
                    time.sleep(sleep_time)

        except Exception as exc:
            # Stop sending immediately on any worker failure.
            self.student_active = False
            self.emit(
                state="TRAINER STOPPED",
                error=str(exc),
                running=False,
            )

        finally:
            self.student_active = False
            self.close_devices()
            self.running = False
            self.emit(running=False)


class TrainerApp(tk.Tk):
    def __init__(self):
        super().__init__()

        self.title(f"Tango 2 USB Racing Trainer v{APP_VERSION}")
        self.geometry("820x720")
        self.minsize(760, 660)

        self.updates = queue.Queue()
        self.worker = None
        self.last_state = {}

        self.profile_var = tk.StringVar(
            value="BEGINNER"
        )
        self.state_var = tk.StringVar(
            value="STOPPED"
        )

        self.build_ui()
        self.protocol("WM_DELETE_WINDOW", self.on_close)

        self.after(50, self.process_updates)

        # Start automatically after the window appears.
        self.after(300, self.start_trainer)

    def build_ui(self):
        root = ttk.Frame(self, padding=16)
        root.pack(fill="both", expand=True)

        title_row = ttk.Frame(root)
        title_row.pack(fill="x", pady=(0, 10))

        title = ttk.Label(
            title_row,
            text="TANGO 2 USB RACING TRAINER",
            font=("Segoe UI", 18, "bold"),
        )
        title.pack(side="left")

        ttk.Label(
            title_row,
            text=f"v{APP_VERSION}",
            font=("Segoe UI", 10),
        ).pack(side="right", pady=(7, 0))

        # ----------------------------------------------------
        # Large instructor state banner
        # ----------------------------------------------------
        self.banner_frame = tk.Frame(
            root,
            bg="#3a3a3a",
            bd=0,
            highlightthickness=0,
        )
        self.banner_frame.pack(fill="x", pady=(0, 14))

        self.banner_title_var = tk.StringVar(value="TRAINER STOPPED")
        self.banner_detail_var = tk.StringVar(value="")

        self.state_label = tk.Label(
            self.banner_frame,
            textvariable=self.banner_title_var,
            anchor="center",
            font=("Segoe UI", 24, "bold"),
            bg="#3a3a3a",
            fg="#ffffff",
            padx=10,
            pady=12,
        )
        self.state_label.pack(fill="x")

        self.state_detail_label = tk.Label(
            self.banner_frame,
            textvariable=self.banner_detail_var,
            anchor="center",
            font=("Segoe UI", 11, "bold"),
            bg="#3a3a3a",
            fg="#ffffff",
            padx=10,
            pady=(0, 10),
        )
        self.state_detail_label.pack(fill="x")

        self.update_state_banner("STOPPED")

        # ----------------------------------------------------
        # Connections / status
        # ----------------------------------------------------
        status_frame = ttk.LabelFrame(
            root, text="System Status", padding=12
        )
        status_frame.pack(fill="x", pady=(0, 12))

        self.status_vars = {
            "student_connected": tk.StringVar(value="NO"),
            "master_connected": tk.StringVar(value="NO"),
            "student_ready": tk.StringVar(value="NO"),
            "handoff": tk.StringVar(value="OFF"),
            "trainer_valid": tk.StringVar(value="NO"),
            "override": tk.StringVar(value="OFF"),
            "packet_rate": tk.StringVar(value="0.0 Hz"),
            "timeout": tk.StringVar(value="0"),
        }

        labels = [
            ("Student Tango", "student_connected"),
            ("Master Tango", "master_connected"),
            ("Student Ready", "student_ready"),
            ("Master Handoff", "handoff"),
            ("Trainer Input", "trainer_valid"),
            ("Instructor Override", "override"),
            ("Packet Rate", "packet_rate"),
            ("Failsafe Timer", "timeout"),
        ]

        for idx, (label, key) in enumerate(labels):
            row = idx // 2
            col = (idx % 2) * 2

            ttk.Label(
                status_frame,
                text=label + ":",
            ).grid(
                row=row,
                column=col,
                sticky="w",
                padx=(0, 8),
                pady=3,
            )

            if not hasattr(self, "status_value_labels"):
                self.status_value_labels = {}

            value_label = tk.Label(
                status_frame,
                textvariable=self.status_vars[key],
                font=("Segoe UI", 10, "bold"),
                bg="#e6e6e6",
                fg="#222222",
                padx=7,
                pady=2,
                bd=0,
            )
            value_label.grid(
                row=row,
                column=col + 1,
                sticky="w",
                padx=(0, 28),
                pady=3,
            )
            self.status_value_labels[key] = value_label

        # ----------------------------------------------------
        # Profiles
        # ----------------------------------------------------
        profile_frame = ttk.LabelFrame(
            root, text="Training Profile", padding=12
        )
        profile_frame.pack(fill="x", pady=(0, 12))

        for col, name in enumerate(PROFILES):
            rb = ttk.Radiobutton(
                profile_frame,
                text=name.title(),
                value=name,
                variable=self.profile_var,
                command=lambda n=name: self.change_profile(n),
            )
            rb.grid(
                row=0,
                column=col,
                padx=(0, 18),
                sticky="w",
            )

        self.profile_detail_var = tk.StringVar()
        ttk.Label(
            profile_frame,
            textvariable=self.profile_detail_var,
        ).grid(
            row=1,
            column=0,
            columnspan=3,
            sticky="w",
            pady=(8, 0),
        )

        self.update_profile_detail("BEGINNER")

        # ----------------------------------------------------
        # Student commands
        # ----------------------------------------------------
        stick_frame = ttk.LabelFrame(
            root, text="Student Commands", padding=12
        )
        stick_frame.pack(fill="x", pady=(0, 12))

        self.stick_vars = {
            "roll": tk.StringVar(value="--"),
            "pitch": tk.StringVar(value="--"),
            "yaw": tk.StringVar(value="--"),
            "throttle": tk.StringVar(value="--"),
            "arm": tk.StringVar(value="--"),
        }

        stick_labels = [
            ("Roll", "roll"),
            ("Pitch", "pitch"),
            ("Yaw", "yaw"),
            ("Throttle", "throttle"),
            ("Student A / CH5", "arm"),
        ]

        for idx, (label, key) in enumerate(stick_labels):
            ttk.Label(
                stick_frame,
                text=label + ":",
            ).grid(
                row=idx,
                column=0,
                sticky="w",
                pady=2,
            )

            ttk.Label(
                stick_frame,
                textvariable=self.stick_vars[key],
                font=("Consolas", 10),
            ).grid(
                row=idx,
                column=1,
                sticky="w",
                padx=(12, 0),
                pady=2,
            )

        ttk.Label(
            stick_frame,
            text=(
                "Student A is READY only. "
                "Aircraft ARM remains master-only."
            ),
        ).grid(
            row=5,
            column=0,
            columnspan=2,
            sticky="w",
            pady=(8, 0),
        )

        # ----------------------------------------------------
        # Controls
        # ----------------------------------------------------
        button_frame = ttk.Frame(root)
        button_frame.pack(fill="x", pady=(4, 0))

        self.start_button = ttk.Button(
            button_frame,
            text="Start / Restart Trainer",
            command=self.start_trainer,
        )
        self.start_button.pack(side="left")

        self.stop_button = ttk.Button(
            button_frame,
            text="Stop Trainer",
            command=self.stop_trainer,
        )
        self.stop_button.pack(side="left", padx=(8, 0))

        self.message_var = tk.StringVar(
            value="Connect Student as USB Joystick and Master as USB Agent."
        )
        ttk.Label(
            root,
            textvariable=self.message_var,
            wraplength=700,
        ).pack(
            fill="x",
            pady=(12, 0),
        )

    def update_state_banner(self, state):
        bg, fg, title = STATE_VISUALS.get(
            state,
            ("#3a3a3a", "#ffffff", state),
        )

        detail_map = {
            "STOPPED": "Connect both radios, then start the trainer.",
            "CONNECTING": "Looking for Student USB Joystick and Master USB Agent.",
            "MASTER CONTROL - NOT READY": "Instructor has control. Student is not READY.",
            "READY - MASTER CONTROL": "Student is READY. Instructor still has control.",
            "ACTIVE - STUDENT CONTROL": "Student has the flight controls.",
            "OVERRIDE - MASTER CONTROL": "Instructor override is latched. Release D to reset.",
            "MASTER CONTROL - INPUT INVALID": "Instructor has control. Student trainer input is invalid.",
            "MASTER STATUS LOST": "Master status feedback is stale. Do not assume student control.",
            "TRAINER STOPPED": "Trainer packets are stopped. Instructor retains aircraft control.",
            "STOPPING": "Stopping trainer packet output.",
        }

        self.banner_title_var.set(title)
        self.banner_detail_var.set(detail_map.get(state, state))

        self.banner_frame.configure(bg=bg)
        self.state_label.configure(bg=bg, fg=fg)
        self.state_detail_label.configure(bg=bg, fg=fg)

    def update_status_badges(self):
        if not hasattr(self, "status_value_labels"):
            return

        # Neutral defaults for numeric values.
        neutral = ("#e6e6e6", "#222222")
        good = ("#d8f3dc", "#155724")
        warning = ("#fff3cd", "#7a5200")
        bad = ("#f8d7da", "#721c24")
        active = ("#d1ecf1", "#0c5460")

        values = {
            key: var.get()
            for key, var in self.status_vars.items()
        }

        badge_colors = {
            "student_connected": good if values["student_connected"] == "YES" else bad,
            "master_connected": good if values["master_connected"] == "YES" else bad,
            "student_ready": good if values["student_ready"] == "YES" else neutral,
            "handoff": active if values["handoff"] == "ON" else neutral,
            "trainer_valid": good if values["trainer_valid"] == "YES" else warning,
            "override": bad if values["override"] == "ON" else neutral,
            "packet_rate": neutral,
            "timeout": neutral,
        }

        for key, (bg, fg) in badge_colors.items():
            label = self.status_value_labels.get(key)
            if label is not None:
                label.configure(bg=bg, fg=fg)

    def update_profile_detail(self, name):
        p = PROFILES[name]
        self.profile_detail_var.set(
            f"Roll {p['roll']*100:.0f}%   |   "
            f"Pitch {p['pitch']*100:.0f}%   |   "
            f"Yaw {p['yaw']*100:.0f}%   |   "
            f"Throttle {p['throttle']*100:.0f}%"
        )

    def change_profile(self, name):
        self.update_profile_detail(name)

        if self.worker is None or not self.worker.is_alive():
            return

        ok, reason = self.worker.request_profile(name)

        if not ok:
            self.profile_var.set(
                self.worker.get_profile()
            )
            self.update_profile_detail(
                self.worker.get_profile()
            )
            messagebox.showwarning(
                "Profile Change Blocked",
                reason,
            )

    def start_trainer(self):
        if self.worker is not None and self.worker.is_alive():
            return

        self.message_var.set("Connecting...")
        self.state_var.set("CONNECTING")
        self.update_state_banner("CONNECTING")

        self.worker = TrainerWorker(
            self.updates,
            self.profile_var.get(),
        )
        self.worker.start()

    def stop_trainer(self):
        if self.worker is not None:
            self.worker.stop()

        self.state_var.set("STOPPING")
        self.update_state_banner("STOPPING")
        self.message_var.set(
            "Stopping trainer packets..."
        )

    def process_updates(self):
        try:
            while True:
                update = self.updates.get_nowait()
                self.apply_update(update)
        except queue.Empty:
            pass

        self.after(50, self.process_updates)

    def apply_update(self, update):
        self.last_state.update(update)

        if "state" in update:
            self.state_var.set(update["state"])
            self.update_state_banner(update["state"])

        if "profile" in update:
            profile = update["profile"]
            self.profile_var.set(profile)
            self.update_profile_detail(profile)

        mapping = {
            "student_connected": (
                "student_connected",
                lambda v: "YES" if v else "NO",
            ),
            "master_connected": (
                "master_connected",
                lambda v: "YES" if v else "NO",
            ),
            "student_ready": (
                "student_ready",
                lambda v: "YES" if v else "NO",
            ),
            "handoff": (
                "handoff",
                lambda v: "ON" if v else "OFF",
            ),
            "trainer_valid": (
                "trainer_valid",
                lambda v: "YES" if v else "NO",
            ),
            "override": (
                "override",
                lambda v: "ON" if v else "OFF",
            ),
            "packet_rate": (
                "packet_rate",
                lambda v: f"{v:.1f} Hz",
            ),
            "master_timeout": (
                "timeout",
                str,
            ),
        }

        for incoming, (target, formatter) in mapping.items():
            if incoming in update:
                self.status_vars[target].set(
                    formatter(update[incoming])
                )

        self.update_status_badges()

        if "raw_roll" in update:
            self.stick_vars["roll"].set(
                f"{update['raw_roll']:4d} -> "
                f"{update['roll']:4d}"
            )

        if "raw_pitch" in update:
            self.stick_vars["pitch"].set(
                f"{update['raw_pitch']:4d} -> "
                f"{update['pitch']:4d}"
            )

        if "raw_yaw" in update:
            self.stick_vars["yaw"].set(
                f"{update['raw_yaw']:4d} -> "
                f"{update['yaw']:4d}"
            )

        if "raw_throttle" in update:
            self.stick_vars["throttle"].set(
                f"{update['raw_throttle']:4d} -> "
                f"{update['throttle']:4d}"
            )

        if "arm_ch5" in update:
            self.stick_vars["arm"].set(
                str(update["arm_ch5"])
            )

        if "error" in update:
            self.message_var.set(update["error"])
            self.status_vars["student_connected"].set("NO")
            self.status_vars["master_connected"].set("NO")
            self.update_state_banner("TRAINER STOPPED")
            self.update_status_badges()

        if "running" in update:
            running = bool(update["running"])

            if running:
                self.start_button.state(["disabled"])
                self.stop_button.state(["!disabled"])
                self.message_var.set(
                    "Trainer running. "
                    "Release Student A once before READY."
                )
            else:
                self.start_button.state(["!disabled"])
                self.stop_button.state(["disabled"])

                if "error" not in update and self.state_var.get() == "STOPPING":
                    self.state_var.set("STOPPED")
                    self.update_state_banner("STOPPED")

    def on_close(self):
        if self.worker is not None:
            self.worker.stop()

        self.after(100, self.destroy)


if __name__ == "__main__":
    app = TrainerApp()
    app.mainloop()
