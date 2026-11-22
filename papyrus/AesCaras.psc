Scriptname AesCaras Hidden
{Aestherius: fabrica la textura de la cara de los jugadores remotos (AesCaras.dll). AesCaras.pex se genera con hacer_pex.py.}

; asCapas: "tipo|argb|ruta;tipo|argb|ruta;..."
; Devuelve 2 pintada, 1 ya estaba, 0 aún sin cabeza, negativo = error (ver src/main.cpp)
Int Function PintarCara(Actor akActor, String asCapas) global native

Int Function Version() global native

; v2: suelta en el juego las teclas que Windows dice que ya no están pulsadas (autowalk al cerrar el chat).
; Devuelve cuántas ha soltado, -1 si aún no hay teclado.
Int Function SoltarTeclas() global native

; v3 (hierba1): rehace la hierba de las celdas exteriores cargadas que toca el círculo (afX, afY, afRadio), para que
; NGIO quite la que atraviesa los objetos del servidor. Devuelve 1 si lo ha encolado, -1 si NGIO no está.
Int Function RehacerHierba(Float afX, Float afY, Float afRadio) global native

; v8 (giro2): gira al jugador a afGrados (rumbo, en grados) sin el «salto» de SetAngle, como al girar con el ratón.
; El creador de personaje lo llama en cada fotograma. Devuelve 1 si lo ha encolado, 0 si aún no hay jugador.
Int Function GirarJugador(Float afGrados) global native

; v9 (camara1): la cámara libre (tfc) para los encuadres del creador. CamaraLibre: 0-2 posición x, y, z; 3 cabeceo;
; 4 rumbo (radianes del juego); -99999 si no está en modo libre. PonerCamaraLibre: 1 encolado, 0 si no está en libre.
Float Function CamaraLibre(Int aiDato) global native
Int Function PonerCamaraLibre(Float afX, Float afY, Float afZ, Float afCabeceo, Float afRumbo) global native

; v10 (pegar1): el texto copiado en Windows (para Ctrl+V y el botón «Pegar» del creador); "" si no hay.
String Function LeerPortapapeles() global native
