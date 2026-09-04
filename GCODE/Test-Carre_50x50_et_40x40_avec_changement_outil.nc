(Test a vide - Carre de 50x50 mm puis carre de 40x40 mm avec changement d'outil)
G21 (Unites en millimetres)
G90 (Positionnement absolu)

(Preparation)
G0 Z5.000 (Leve la broche de 5mm pour plus de securite)
G0 X0 Y0 (S'assure d'etre au point de depart)
M3 S1000 (Simule l'allumage de la broche - le relais doit faire clic)
G4 P2 (Pause de 2 secondes pour laisser la broche accelerer)

(Plongee)
G1 Z-1.000 F300 (Plonge doucement de 1mm - vitesse 300 mm/min)

(Tracage du carre 50x50 - outil T1)
G1 X50.000 Y0.000 F1500 (Va a droite - vitesse 1500 mm/min)
G1 X50.000 Y50.000 F1500 (Va au fond)
G1 X0.000 Y50.000 F1500 (Va a gauche)
G1 X0.000 Y0.000 F1500 (Revient a l'origine de l'avant)

(Fin de l'usinage du premier carre)
G0 Z5.000 (Leve la broche pour degager la piece)
M5 (Eteint la broche avant le changement d'outil)

(Changement d'outil vers T2)
T2 M6 (Demande le changement d'outil - voir remarque ci-dessous si non supporte par le controleur)
G4 P2 (Pause de 2 secondes pour laisser le temps de changer l'outil)

(Repositionnement sur le coin du second carre - 40x40 centre dans le premier)
G0 X5.000 Y5.000 (Se place au coin bas-gauche du carre interieur, decale de 5mm de chaque cote)
M3 S1000 (Rallume la broche avec le nouvel outil)
G4 P2 (Pause de 2 secondes pour laisser la broche accelerer)

(Plongee - meme profondeur que le premier carre)
G1 Z-1.000 F300 (Plonge doucement de 1mm - vitesse 300 mm/min)

(Tracage du carre 40x40 - outil T2)
G1 X45.000 Y5.000 F1500 (Va a droite)
G1 X45.000 Y45.000 F1500 (Va au fond)
G1 X5.000 Y45.000 F1500 (Va a gauche)
G1 X5.000 Y5.000 F1500 (Revient au point de depart du second carre)

(Fin du programme)
G0 Z5.000 (Leve la broche pour degager la piece)
M5 (Eteint la broche)
G0 X0 Y0 (Retour a la position de depart)
M30 (Fin du programme)
