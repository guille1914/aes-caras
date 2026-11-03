# AesCaras

Plugin de SKSE (Skyrim AE 1.6.x) de Aestherius. Fabrica la textura de la cara de los jugadores remotos de SkyMP con
sus capas de tinte (tono de piel, maquillaje, pinturas), como hace Skyrim Together. Sin él, las cabezas de los
demás salen sin color.

- `src/main.cpp`: el plugin (CommonLibSSE-NG).
- `papyrus/AesCaras.psc` y `AesCaras.pex`: la declaración de las funciones nativas. El .pex se genera con
  `python3 papyrus/hacer_pex.py TESModPlatform.pex papyrus/AesCaras.pex` (no hace falta el Creation Kit).
- GitHub Actions lo compila en cada push: el resultado (`AesCaras.zip`, con `SKSE/Plugins/AesCaras.dll` y
  `Scripts/AesCaras.pex`) está en la pestaña Actions → la última ejecución → Artifacts.

Instalación (modpack): `Data/SKSE/Plugins/AesCaras.dll` y `Data/Scripts/AesCaras.pex`. Necesita Address Library
for SKSE Plugins (ya la usa SkyrimPlatform). El registro sale en `Documentos/My Games/Skyrim Special Edition/SKSE/AesCaras.log`.
