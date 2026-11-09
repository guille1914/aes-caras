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
