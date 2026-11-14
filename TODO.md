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

## Hecho despues de la primera pasada

- [x] 9. Evitar duplicados: no se envia ningun actor a menos de `city_near` (60 m)
      del peer; en la zona comun no hay copias dobles. Verificado: con los dos
      jugadores juntos se envian solo 2-10 actores lejanos en vez de 24.
- [x] 10. Vehiculos: hasta 8 por snapshot, con el mismo camino (plantilla,
      copia estatica, puerta por distancia).
- [x] 11. Save compartido para dos PCs: `tools/sync-save.ps1` copia los
      `slot-*.bin` del host al cliente con verificacion de hash y respaldo.

## Pendiente (requiere investigacion de motor)

- [ ] Animar las copias de la ciudad: intentado con `P2` (pose de los mas
      cercanos); aplicar la pose revienta en el motor (`Pose::Update` -> call
      nulo y `Drawable::GetSkeleton` con drawables muertos). Queda tras
      `city_pose=0` hasta encontrar una via segura.
- [ ] Latigo en el actor remoto y su tendril (el contenido no admite dos actores).
- [ ] Efectos anclados y motion trails.
- [ ] Disfraz.
- [ ] Armas en el suelo y vehiculos para los jugadores.
- [ ] Salud/dano y agarres entre jugadores.
- [ ] Modo historia, menu coop y polish (desync, ritmo de paquetes).
- Sincronizacion de enemigos y bosses
