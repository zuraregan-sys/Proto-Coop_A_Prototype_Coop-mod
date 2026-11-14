# TODO - sincronizacion de ciudad / free roam

Objetivo: replicar en cada lado los NPCs (y mas adelante vehiculos) que el otro
jugador tiene cerca, activado solo cuando los jugadores estan dentro del radio
del snapshot, sin cuelgues ni crashes.

## Subtareas

- [x] 1. Diagnostico: clasificar los objetos cercanos por behaviours (personaje /
      vehiculo / prop) y resolver su plantilla (`MetaObjectTemplate` +0x10).
- [x] 2. Mapa objeto -> plantilla dinamico (indice hash, mas alla del anillo de
      4096 spawns) para poder resolver la plantilla de cualquier NPC vivo.
- [x] 3. Paquete de snapshot `C1` (id, plantilla, posicion, orientacion) con
      troceado en varios datagramas.
- [x] 4. Puerta por distancia entre jugadores (encender <150 m, apagar >200 m)
      con histeresis.
- [x] 5. Receptor: crear/actualizar copias visuales de los NPCs (sin IA, sin
      dano) y moverlas con el snapshot.
- [x] 6. Limpieza de copias que dejan de llegar (se mueven fuera de la vista y
      su hueco se reutiliza).
- [x] 7. Verificacion local: build, arranque de las dos instancias, logs de
      snapshots y copias, captura de pantalla, sesion larga sin cuelgues.
- [x] 8. Documentar en README/NOTES y commit.

## Resultado

- `C1` viaja a 4 Hz con hasta 32 personajes por lado; el receptor crea una copia
  por personaje (plantilla resuelta por hash) y la coloca en su posicion.
- Verificado con las dos instancias: ~24 personajes enviados por lado, 32 copias
  creadas, `missing=0`, sin crashes ni cuelgues en sesiones de 3-4 minutos con
  los valores por defecto.
- Las copias son estaticas a proposito (sin IA, animacion ni fisica) para que no
  ejecuten codigo de mision que no tienen; se ven como maniquies.
- Sin supresion del mundo local: en la zona comun cada jugador ve sus NPCs y las
  copias del otro (duplicados). Es el siguiente problema a resolver.

## Siguiente

- Evitar duplicados: repartir la autoridad por distancia (cada lado simula lo
  suyo y el otro no lo pinta) o congelar los NPCs locales que el peer tambien
  envia.
- Vehiculos (111 en 300 m en Times Square) y animacion de las copias.
- Save compartido para dos PCs y carpeta de saves propia del online.
