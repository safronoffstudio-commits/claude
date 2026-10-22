#!/usr/bin/env python3
"""Сценарий автотеста SamOS: проходит по всем экранам и приложениям,
нажимает «пальцем», печатает, рисует и сохраняет скриншоты в screenshots/."""
import os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
from qemu_driver import Emu

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "screenshots")
WORK = sys.argv[1] if len(sys.argv) > 1 else "/tmp/samos-test"
os.makedirs(OUT, exist_ok=True)

e = Emu(os.path.join(ROOT, "build/samos.elf"), WORK)
shot = lambda name: e.shot(os.path.join(OUT, name + ".png"))
home = lambda: e.swipe(270, 1162, 270, 900, steps=8, wait=1.0)

# Иконки домашнего экрана
GRID = {"photos": (78, 396), "paint": (206, 396), "snake": (334, 396), "2048": (462, 396),
        "calendar": (78, 524), "clock": (206, 524), "monitor": (334, 524)}
DOCK = {"notes": (78, 1064), "calc": (206, 1064), "terminal": (334, 1064), "settings": (462, 1064)}
APPS = {**GRID, **DOCK}
def launch(name, wait=1.2):
    e.tap(*APPS[name], wait=wait)

time.sleep(0.7)
shot("01_boot")
time.sleep(2.3)
shot("02_lock")
e.swipe(270, 1000, 270, 450, wait=1.4)
shot("03_home")

# Анимация запуска приложения (кадр в середине)
e.tap(*APPS["calendar"], wait=0.1)
shot("04_app_open_anim")
time.sleep(1.0)
home()
launch("photos")
shot("05_photos")
e.tap(270, 271, wait=5.0)          # Множество Мандельброта
shot("06_photo_viewer")
e.tap(270, 600, wait=0.6)
home()

# Калькулятор: 1234 × 56 =
launch("calc")
C = {"1": (78, 928), "2": (206, 928), "3": (334, 928), "4": (78, 800), "5": (206, 800), "6": (334, 800),
     "x": (462, 672), "=": (462, 1056)}
for k in "1234x56=":
    e.tap(*C[k], wait=0.25)
time.sleep(0.5)
shot("07_calculator")
home()

# Заметки: печатаем на экранной клавиатуре «Покорить мир»
launch("notes")
RU0 = lambda i: (4 + 21.45 + i * 48.909, 872)
RU1 = lambda i: (4 + 21.45 + i * 48.909, 942)
RU2 = lambda i: (52.9 + 21.45 + i * 48.909, 1012)
SHIFT, SPACE = (25, 1012), (279, 1082)
seq = [SHIFT, RU1(4), RU1(6), RU0(3), RU1(6), RU1(5), RU2(4), RU2(5), RU2(6), SPACE, RU2(3), RU2(4), RU1(5)]
for p in seq:
    e.tap(*p, wait=0.25)
# держим палец на клавише «ж», чтобы увидеть всплывающую подсказку
e.cmd("input-send-event", events=e._abs(*RU1(9)))
e.cmd("input-send-event", events=e._btn(True))
time.sleep(0.6)
shot("08_notes_keyboard")
e.cmd("input-send-event", events=e._btn(False))
time.sleep(0.3)
e.tap(*RU2(8), wait=0.3)  # ю -> удалим
e.tap(500, 1012, wait=0.3)  # backspace
home()

# Терминал: команды с физической клавиатуры
launch("terminal", wait=1.5)
for cmd in ["uname -a\n", "free\n", "ls\n", "cat readme.txt\n", "date\n"]:
    e.type_text(cmd)
time.sleep(0.6)
shot("09_terminal")
home()

# Холст: смайлик и сердечко
launch("paint")
import math
BOT = 1170 - 40 - 118
e.tap(36 + 3 * 50, BOT + 40, wait=0.2)     # жёлтый
e.tap(150 + 2 * 120, BOT + 92, wait=0.2)   # толстая кисть
pts = [(270 + 170 * math.cos(a / 40 * 2 * math.pi), 470 + 170 * math.sin(a / 40 * 2 * math.pi)) for a in range(41)]
e.drag(pts, step_delay=0.02, wait=0.3)
e.tap(36 + 0 * 50, BOT + 40, wait=0.2)     # чёрный
e.drag([(210, 420), (212, 422)], wait=0.2)
e.drag([(330, 420), (332, 422)], wait=0.2)
e.tap(150 + 1 * 120, BOT + 92, wait=0.2)
pts = [(270 + 90 * math.cos(a / 20 * math.pi), 480 + 70 * math.sin(a / 20 * math.pi)) for a in range(2, 19)]
e.drag(pts, step_delay=0.02, wait=0.3)
e.tap(36 + 1 * 50, BOT + 40, wait=0.2)     # красный
e.tap(150 + 2 * 120, BOT + 92, wait=0.2)
heart = []
for i in range(41):
    t = i / 40 * 2 * math.pi
    x = 16 * math.sin(t) ** 3
    y = 13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)
    heart.append((270 + x * 6, 830 - y * 6))
e.drag(heart, step_delay=0.02, wait=0.5)
shot("10_paint")
home()

# Змейка
launch("snake", wait=0.5)
for k, w in [("down", 0.5), ("left", 0.6), ("down", 0.3), ("right", 0.5)]:
    e.key(k, wait=w)
shot("11_snake")
home()

# 2048
launch("2048")
for dx, dy in [(-200, 0), (0, -200), (200, 0), (0, 200), (-200, 0), (0, -200), (200, 0), (-200, 0)]:
    e.swipe(270, 600, 270 + dx, 600 + dy, steps=6, wait=0.35)
time.sleep(0.4)
shot("12_2048")
home()

# Календарь
launch("calendar")
shot("13_calendar")
home()

# Часы: мировое время, затем секундомер
launch("clock")
shot("14_clock_world")
e.tap(405, 1092, wait=0.5)
e.tap(444, 682, wait=2.4)
e.tap(96, 682, wait=1.3)
e.tap(96, 682, wait=0.9)
shot("15_stopwatch")
home()

# Монитор системы
launch("monitor", wait=4.5)
shot("16_monitor")
home()

# Настройки и «Об устройстве»
launch("settings")
shot("17_settings")
e.tap(270, 270, wait=1.0)
shot("18_about")
e.tap(80, 98, wait=0.6)
home()

# Пункт управления: включаем Wi-Fi и тёмную тему
e.tap(480, 30, wait=1.2)
e.tap(87, 302, wait=0.5)
e.tap(333, 182, wait=0.8)
shot("19_control_center")
e.tap(270, 900, wait=0.8)

# Тёмные настройки + смена обоев на «Закат»
launch("settings")
e.tap(209, 913, wait=1.5)
shot("20_settings_dark")
home()
time.sleep(0.5)
shot("21_home_sunset")

# Блокировка через пункт управления
e.tap(480, 30, wait=1.2)
e.tap(459, 182, wait=1.2)
shot("22_lock_sunset")

e.close()
print(open(os.path.join(WORK, "serial.log")).read()[-1200:])
