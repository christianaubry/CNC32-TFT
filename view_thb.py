import tkinter as tk
from tkinter import filedialog, messagebox
import struct

def show_thb_gui():
    root = tk.Tk()
    root.withdraw() # Cache la fenêtre principale
    
    # Popup de sélection
    path = filedialog.askopenfilename(
        title="Sélectionnez un fichier miniature (.thb)",
        filetypes=[("Fichiers Thumbnail", "*.thb"), ("Tous les fichiers", "*.*")]
    )
    
    if not path:
        return # L'utilisateur a annulé

    try:
        with open(path, "rb") as f:
            data = f.read()
    except Exception as e:
        messagebox.showerror("Erreur", f"Erreur de lecture: {e}")
        return

    if len(data) < 18:
        messagebox.showerror("Erreur", "Fichier trop petit pour être valide.")
        return
        
    magic, w, h, bpp, res, srcSize, srcMtime = struct.unpack("<4sHHBBII", data[:18])
    magic = magic.decode('ascii', errors='ignore')
    
    pixels = data[18:]
    if len(pixels) < w * h:
        messagebox.showerror("Erreur", f"Fichier incomplet (manque de pixels).")
        return

    # Calcule le nombre de pixels allumés (valeur > 0 car le fond est 0)
    allumes = sum(1 for p in pixels if p > 0)

    # Création de la fenêtre d'affichage
    win = tk.Toplevel(root)
    nom_fichier = path.replace('\\', '/').split('/')[-1]
    win.title(f"Aperçu - {nom_fichier} ({w}x{h})")
    win.configure(bg="#2d2d2d")
    
    # Ajout des infos
    info_lbl = tk.Label(win, text=f"Magic: {magic} | Pixels non-vides: {allumes} / {w*h}", 
                        bg="#2d2d2d", fg="white", font=("Arial", 10))
    info_lbl.pack(pady=10)

    # Affichage du canevas
    scale = 5 # Zoom x5
    canvas = tk.Canvas(win, width=w*scale, height=h*scale, bg="#0A0F16", highlightthickness=1, highlightbackground="#444")
    canvas.pack(padx=20, pady=(0, 20))
    
    # Dessin des pixels
    for y in range(h):
        for x in range(w):
            val = pixels[y * w + x]
            if val > 0:
                # Interpolation vers blanc ambré
                r = 10 + int((255 - 10) * val / 255)
                g = 15 + int((235 - 15) * val / 255)
                b = 22 + int((200 - 22) * val / 255)
                
                color = f"#{r:02x}{g:02x}{b:02x}"
                canvas.create_rectangle(
                    x*scale, y*scale, (x+1)*scale, (y+1)*scale, 
                    fill=color, outline=""
                )
    
    # Centre la fenêtre
    win.update_idletasks()
    x_c = (win.winfo_screenwidth() // 2) - (win.winfo_width() // 2)
    y_c = (win.winfo_screenheight() // 2) - (win.winfo_height() // 2)
    win.geometry(f"+{x_c}+{y_c}")
    
    # Garde la fenêtre ouverte
    win.mainloop()

if __name__ == "__main__":
    show_thb_gui()
