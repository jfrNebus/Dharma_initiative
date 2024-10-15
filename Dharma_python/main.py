import os
import threading
import time
from tkinter import *
import socket
import pygame

BG_BLACK = "#000000"
# TEXT_COLOR = "#00FF00"
TEXT_COLOR = "#0cc20c"
FONT = "Terminal"

message = ">:"

bool_test = True
key_on = True
trigger_failure_state = False

# host = "192.168.1.138"
host = "192.168.130.138"
port = 80
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

pygame.mixer.init()


# Nuevos

number_connexion_attempt = 22
command = ""
lost_connection = 0
failure_count = 0


# ------


def clear_entry():
    entry.delete("1.0", END)


def close_entry():
    entry.configure(state='disabled')


def open_entry():
    entry.configure(state='normal')


def play_first_alarm():
    sound = pygame.mixer.Sound(r'C:\Users\codin\OneDrive\Proyecto Dharma\Fotos perdidos\first_alarm.wav')
    sound.play()


def play_second_alarm():
    sound = pygame.mixer.Sound(r'C:\Users\codin\OneDrive\Proyecto Dharma\Fotos perdidos\second_alarm.mp3')
    sound.play()


# Reset ----------------------------------
def reset_console():
    clear_entry()
    close_entry()

# Get commands from UI -------------------


def type_python_command(self):
    global bool_test, trigger_failure_state
    entry_numbers = entry.get("1.0", "end-2c").strip()
    state = entry.cget("state")
    if state == "normal" and len(entry_numbers) > 0:
        print(entry_numbers)
        print(len(entry_numbers))
        match entry_numbers:
            case "4815162342" | "4 8 15 16 23 42":
                send_server_command("passwordReset")
                reset_console()
            case "enableFailure":
                send_server_command("failureEnabled")
                trigger_failure_state = True
                clear_entry()
                close_entry()
            case "disableFailure":
                send_server_command("failureDisabled")
                trigger_failure_state = False
                clear_entry()
                close_entry()
            case _:
                send_server_command("failureOn")
                bool_test = False
                # Set to false in order to not load onClose when the program gets closed through the
                # system failure mode
    else:
        entry.delete("1.0", END)


# Send and get commands to/from the server
def send_server_command(command):
    try:
        s.sendall(command.encode('utf-8'))
        print("Send is done.")
    except OSError as exception:
        print("Exception: " + str(exception))


def get_server_command():
    global command
    command = ""
    try:
        command = s.recv(1024).decode("utf-8")
        send_server_command("alive")
    except Exception:
        command = "null"


# Methods needed to perform the failure --
def system_failure_iteration():
    global failure_count
    if failure_count < 200:
        txt.insert(END, "System failure")
        txt.see(END)
        failure_count += 1
        window.after(50, system_failure_iteration)
    elif failure_count == 200:
        time.sleep(2)
        if trigger_failure_state:
            # os.system('cmd /c "shutdown /s /t 15 /c "Dharma initiative routine status: Failed. '
            #           'System status: Shutting down. You have 15 seconds to save any valuable data.'
            #           ' Press Esc key to close this message."')
            window.destroy()
        else:
            txt.delete("1.0", END)
            txt.insert(END, "System reboot required.\n")
            txt.configure(state='disabled')


def gui_adaptation():
    entry.destroy()
    txt.configure(width=80)
    txt.configure(state="normal")
    txt.delete("1.0", END)


def send_failure_command():
    gui_adaptation()
    system_failure_iteration()


# Connexion attempt ----------------------
def get_connected():
    global s, number_connexion_attempt, entry, host, port, key_on, bool_test
    iterate = True
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(1)
        s.connect((host, port))
        # setBlocking false sets the socket to not wait the timeout time for the socket to get
        # connected. It tries to connection right in the moment and if in that moment it is not
        # posible the program keeps working.
        s.setblocking(False)
        open_entry()
        reset_console()
        window.after(0, listener)
    except (TimeoutError, OSError) as exception:
        number_connexion_attempt -= 1
        if 10 >= number_connexion_attempt > -1:
            open_entry()
            entry.delete("1.0", END)
            entry.insert(END, f'Failed connection attempt. Number of attempts left: {number_connexion_attempt}')
            close_entry()
            entry.update_idletasks()
        elif number_connexion_attempt == -1:
            iterate = False
        else:
            points = entry.get("1.0", "end").strip()
            open_entry()
            if len(points) == 3:
                entry.delete("1.0", END)
            elif len(points) < 3:
                entry.insert(END, ".")
            close_entry()
            entry.update_idletasks()
        if iterate:
            if not isinstance(exception, TimeoutError):
                time.sleep(1)
            window.after(0, get_connected)
        else:
            gui_adaptation()
            system_failure_iteration()


# Actions to perform on close ------------

def try_package_lost(new_bool_test):
    global s, host, port, command
    iterations = 0
    connected = False
    while iterations < 10:
        try:
            if not connected:
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                # This conditional is here to avoid new connection tries
                # when the connection was already established.
                s.settimeout(1)
                s.connect((host, port))
                print("Connected successfully")
                connected = True
            command = s.recv(1024).decode("utf-8")
            s.setblocking(False)
            print("Command = " + command + ". Connection back.")
            break
        except Exception as e:
            print("Connection exception caught: " + str(e))
            if not connected:
                # This conditional works together with the previous one,
                # if the connection was established the connection must
                # not be closed. It is needed to close the connection to
                # restart the socket when the connection was not established.
                # If the connection is not closed a "[WinError 10022] Se ha
                # proporcionado un argumento no válido" will be thrown
                s.close()
            if iterations < 10:
                iterations += 1
    if iterations == 10:
        new_bool_test[0] = False


# def listener():
#     global bool_test, key_on, command, lost_connection, s
#     get_server_command()
#     state = entry.cget("state")
#     print("-----------------")
#     print("Command: " + command + ". State: " + state)
#     if command.endswith("key_activated"):
#         key_on = False
#     elif command.endswith("stopSystem"):
#         print("stopSystem")
#         bool_test = False
#     if bool_test and key_on:
#         match command:
#             case "overFirstStage" | "underFirstStage":
#                 print("System alive")
#             case "first":
#                 play_first_alarm()
#             case "second":
#                 play_second_alarm()
#             case "null":
#                 lost_connection += 1
#                 if lost_connection == 5:
#                     print("Connection lost.")
#                     s.close()
#                     print("Socket closed.")
#                     print("-----------------")
#                     new_bool_test = [True]
#                     try_package_lost(new_bool_test)
#                     bool_test = new_bool_test[0]
#                     lost_connection = 0
#
#         if command != "null":
#             lost_connection = 0
#
#         if command == "overFirstStage" or command == "null":
#             if state == "normal":
#                 print("Entry state: close")
#                 close_entry()
#         elif command != "overFirstStage" or command != "null":
#             if state == "disabled":
#                 print("Entry state: open")
#                 open_entry()
#
#         print("-----------------")
#         window.after(1000, listener)
#     else:
#         if key_on:
#             if command != "null":
#                 gui_adaptation()
#                 entry.update_idletasks()
#                 time.sleep(2.2)
#                 system_failure_iteration()
#             else:
#                 send_failure_command()
#         else:
#             gui_adaptation()
#             time.sleep(1)
#             txt.insert("1.0", "Key triggered.")
#             txt.update_idletasks()
#             time.sleep(5)
#             window.destroy()

def listener():
    global bool_test, key_on, command, lost_connection, s
    get_server_command()
    state = entry.cget("state")
    print("-----------------")
    print("Command: " + command + ". State: " + state)
    if command.endswith("key_activated"):
        key_on = False
    elif command.endswith("stopSystem"):
        print("stopSystem")
        bool_test = False
    if bool_test and key_on:
        if command.endswith("overFirstStage") | command.endswith("underFirstStage"):
            print("System alive")
        elif command.endswith("first"):
            play_first_alarm()
        elif command.endswith("second"):
            play_second_alarm()
        elif command.endswith("null"):
            lost_connection += 1
            if lost_connection == 5:
                print("Connection lost.")
                s.close()
                print("Socket closed.")
                print("-----------------")
                new_bool_test = [True]
                try_package_lost(new_bool_test)
                bool_test = new_bool_test[0]
                lost_connection = 0

        if command != "null":
            lost_connection = 0

        if command == "overFirstStage" or command == "null":
            if state == "normal":
                print("Entry state: close")
                close_entry()
        elif command != "overFirstStage" or command != "null":
            if state == "disabled":
                print("Entry state: open")
                open_entry()

        print("-----------------")
        window.after(1000, listener)
    else:
        if key_on:
            if command != "null":
                gui_adaptation()
                entry.update_idletasks()
                time.sleep(2.2)
                system_failure_iteration()
            else:
                send_failure_command()
        else:
            gui_adaptation()
            time.sleep(1)
            txt.insert("1.0", "Key triggered.")
            txt.update_idletasks()
            time.sleep(5)
            window.destroy()


def on_close():
    print(bool_test)
    print("On close")
    if key_on:
        send_server_command("failureOn")
        # os.system('cmd /c "shutdown /s /t 15 /c "Dharma initiative routine status: Failed. '
        #       'System status: Shutting down. You have 15 seconds to save any valuable data.'
        #       ' Press Esc key to close this message."')


# ---------------------------- Gui

window = Tk()
window.configure(background="black")
window.attributes('-fullscreen', True)
window.bind('<Return>', type_python_command)
# window.iconbitmap(r'G:\Mi unidad\Proyecto Dharma\Fotos perdidos\transparency.ico')
window.title('')
# icon = PhotoImage(file=r'G:\Mi unidad\Proyecto Dharma\Fotos perdidos\transparency.ico')
# window.iconphoto(True, icon)
# http://www.rw-designer.com/online_icon_maker.php
# Toma notas de la r delante de '' a la hora de declarar la ruta del iconbitmap


# -------- Frame

frame_one = Frame(window)
frame_one.pack()

# -------- Text

txt = Text(frame_one, bg=BG_BLACK, bd=0, fg=TEXT_COLOR, font=FONT, width=2,
           height=24, spacing1=6, spacing2=6, insertbackground=TEXT_COLOR)
txt.insert(END, message)
txt.configure(state='disabled')
txt.pack(side=LEFT)

# -------- Text as entry

entry = Text(frame_one, bg=BG_BLACK, bd=0, fg=TEXT_COLOR,
             font=FONT, width=78, height=24, insertbackground=TEXT_COLOR,
             insertwidth=8, spacing1=6, spacing2=6)
entry.configure(state='disabled')
entry.pack(side=LEFT)
entry.focus_set()

# -------- Frame location

screen_width = window.winfo_screenwidth()
screen_height = window.winfo_screenheight()

txt_width = txt.winfo_reqwidth()
entry_width = entry.winfo_reqwidth()
txt_height = txt.winfo_reqheight()

common_height = txt_height
# Since they are in the same row, height will be the same for both

frame_x = int((screen_width - (txt_width + entry_width)) / 2)
frame_y = int((screen_height - txt_height) / 2)

frame_one.place(x=frame_x, y=frame_y)

# ----------------------------

# Para tomar la ip local socket.gethostbyname(socket.gethostname())
# host1 = socket.gethostbyname()

# https://www.youtube.com/watch?v=YwWfKitB8aA&t=10s

threading.Thread(target=get_connected).start()

window.mainloop()

on_close()
