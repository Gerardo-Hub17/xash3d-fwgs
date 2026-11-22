# Xash3D FWGS Engine (Wii port) <img align="right" width="128" height="128" src="https://github.com/FWGS/xash3d-fwgs/raw/master/game_launch/icon-xash-material.png" alt="Xash3D FWGS icon" />

Xash3D ([pronounced](https://ipa-reader.com/?text=ks%C9%91%CA%82) `[ksɑʂ]`) FWGS Es un motor de juego diseñado para brindar compatibilidad con Half-Life Engine y ampliarlo, además de ofrecer a los desarrolladores de juegos un flujo de trabajo conocido. Esta es la versión para Wii/Gamecube del motor.

Xash3D FWGS is a heavily modified fork of an original [Xash3D Engine](https://www.moddb.com/engines/xash3d-engine) by Unkle Mike.

## Controles

La mayoría de controles son soportados.

### Wii remote + nunchuk

El Nunshuck es necesario en este esquema "Debido a que su stick es el que usarás para moverte"

| Botón | Acción |
| --- | --- |
| **Z** | Fuego |
| **A** | Saltar |
| **B** | agacharse |
| **Nunchuk C** | interactuar |
| **1** | Tab |
| **2** | Guardado Rápido |
| **D-pad up** | Linterna |
| **D-pad down** | Fuego Secundario |
| **D-pad left / right** | Previo / Siguiente arma|
| **-** | Recargar |
| **+** | Menú de ajustes |
| **Home** | Menu |

Para apuntar se usa el puntero del mando.

### Classic controller

| Botón | Acción |
| --- | --- |
| **R** | Fuego |
| **ZR** | Linterna |
| **L** | Fuego secundario |
| **ZL** | Agacharse |
| **a** | Saltado |
| **b** | Guardado Rápido |
| **x** | Recargar |
| **y** | Interactuar/Usar |
| **D-pad left / right** | Previo / Siguiente Arma |
| **+** | Menú de ajustes |
| **-** | Tab |
| **Home** | Menu |

El stick izquierdo mueve, el stick derecho mira.

### GameCube controller

| Botón | Acción |
| --- | --- |
| **R** | Fuego |
| **Z** | Agarcharse |
| **L** | Disparo Secundario|
| **A** | Saltar |
| **B** | Menú de ajustes |
| **X** | Recargar |
| **Y** | Usar |
| **D-pad left / right** | Previo / Siguiente arma|
| **D-pad down** | Última arma usada |
| **Start** | Menu |

Con el stick izquierdo te mueves, con el C-stick mueves la cámara.

### Menú de ajustes (un jugador)

Con **+** (Wiimote y Classic) o **B** (GameCube) se abre un menú sobre la partida (en un solo jugador la pausa). Se maneja con la cruz, **A** (aceptar) y **B** (volver). Tiene:

- Sensibilidad y zona muerta: del puntero (Wiimote), del stick del Nunchuk o de la palanca (Classic / GameCube).
- Seguir girando en el borde: si la vista sigue girando un rato cuando el puntero sale por un borde de la pantalla.
- Palanca por gesto (Wiimote): con la palanca (o el cuchillo) en la mano, mover el Nunchuk o el Wiimote como un golpe ataca. Modos: solo botón, botón o gesto, solo gesto.
- Configurar botones: cada mando tiene su propia lista de botones y asignación; Home/Start y el botón del menú son fijos.
- Guardado rápido, escribir chat (en línea) y restaurar ajustes.

Todo se guarda en la configuración (`wii_ir_sens`, `wii_ir_deadzone`, `wii_stick_sens`, `wii_stick_deadzone`, `wii_nunchuk_sens`, `wii_nunchuk_deadzone`, `wii_ir_edgehold`, `wii_swing`, `wii_swing_dev`, `wii_swing_sens`, `wii_map_*`).

El menú reproduce media/gamestartup.mp3. Las versiones de Steam incluyen la banda sonora como media/Half-Life01.mp3 en adelante, sin ningún archivo gamestartup.mp3, por lo que el menú se inicia en silencio hasta que copies uno de ellos con ese nombre.
