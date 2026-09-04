(Test à vide - Carré de 50x50 mm)
G21 (Unites en millimetres)
G90 (Positionnement absolu)

(Preparation)
G0 Z5.000 (Leve la broche de 5mm pour plus de securite)
G0 X0 Y0 (S'assure d'etre au point de depart)
M3 S1000 (Simule l'allumage de la broche - le relais doit faire clic)
G4 P2 (Pause de 2 secondes pour laisser la broche accelerer)

(Plongee)
G1 Z-1.000 F300 (Plonge doucement de 1mm - vitesse 300 mm/min)

(Tracage du carre)
G1 X50.000 Y0.000 F1500 (Va a droite - vitesse 1500 mm/min)
G1 X50.000 Y50.000 F1500 (Va au fond)
G1 X0.000 Y50.000 F1500 (Va a gauche)
G1 X0.000 Y0.000 F1500 (Revient a l'origine de l'avant)

(Fin du programme)
G0 Z5.000 (Leve la broche pour degager la piece)
M5 (Eteint la broche)
G0 X0 Y0 (Retour a la position de depart)
M30 (Fin du programme)