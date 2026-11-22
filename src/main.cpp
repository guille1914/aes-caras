// AesCaras · Aestherius
// Fabrica la textura de la cara (capas de tinte: tono de piel, maquillaje, pinturas) de los jugadores remotos de SkyMP.
// Skyrim solo la fabrica para el jugador propio; a los NPCs les pone la que viene hecha con su mod, y nuestros
// personajes no la tienen: su cabeza salía sin color (blanca/gris). Es lo mismo que hace Skyrim Together
// (Code/client/Systems/FaceGenSystem.cpp), con las direcciones de Address Library de Skyrim AE (1.6.x).
//
// Papyrus (AesCaras.psc):
//   Int Function PintarCara(Actor akActor, String asCapas) global native
//   Int Function Version() global native
//   Int Function SoltarTeclas() global native   (v2, ver más abajo)
//   Int Function RehacerHierba(Float afX, Float afY, Float afRadio) global native   (v3, hierba1, ver más abajo)
//   Int Function GirarJugador(Float afGrados) global native   (v8, giro2, ver más abajo)
//   Float Function CamaraLibre(Int aiDato) global native   (v9, camara1, ver más abajo)
//   Int Function PonerCamaraLibre(Float afX, Float afY, Float afZ, Float afCabeceo, Float afRumbo) global native   (v9)
//   String Function LeerPortapapeles() global native   (v10, pegar1, ver más abajo)
// v7 (ajustes1): al arrancar, fija algunos ajustes de otros mods con lo que manda el servidor (AesAjustes.ini).
// asCapas: "tipo|argb|ruta;tipo|argb|ruta;..." (argb en decimal, con o sin signo).
// Devuelve: 2 pintada ahora, 1 ya estaba pintada con esas capas, 0 su cabeza aún no está (reintentar),
//          -1 actor nulo, -2 sin capas válidas, -3 su cabeza no usa el sombreado de cara, -4 no se pudo crear la textura.

#include <chrono>
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>
#include <cstring>

// soltarTeclas1: para saber si una tecla está pulsada de verdad (user32).
extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int vKey);
// pegar1: el portapapeles de Windows (declarados a mano, como GetAsyncKeyState; si ya vino Windows.h, se usan los suyos)
#ifndef _WINUSER_
extern "C" __declspec(dllimport) int __stdcall OpenClipboard(void* hWndNewOwner);
extern "C" __declspec(dllimport) int __stdcall CloseClipboard();
extern "C" __declspec(dllimport) void* __stdcall GetClipboardData(unsigned int uFormat);
#endif
#ifndef _WINBASE_
extern "C" __declspec(dllimport) void* __stdcall GlobalLock(void* hMem);
extern "C" __declspec(dllimport) int __stdcall GlobalUnlock(void* hMem);
#endif
extern "C" __declspec(dllimport) unsigned int __stdcall MapVirtualKeyW(unsigned int uCode, unsigned int uMapType);
// hierba1: para saber si NGIO está cargado (kernel32).
extern "C" __declspec(dllimport) void* __stdcall GetModuleHandleW(const wchar_t* lpModuleName);
// ajustes1: la carpeta del juego (kernel32).
extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(void* hModule, wchar_t* lpFilename, unsigned long nSize);
#include <filesystem>
#include <fstream>
#include <mutex>
#include <numbers>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std::literals;

namespace
{
	constexpr std::int32_t kVersion = 10;

	// Address Library (AE). Mismas que Skyrim Together (y SkyMP para ApplyMasksToRenderTargets / BSRenderManager).
	constexpr REL::ID kCrearTextura{ 70717 };       // NiRenderedTexture* (BSFixedString&)
	constexpr REL::ID kCrearBuffer{ 77299 };        // Ni2DBuffer* (BSRenderManager*, uint32 ancho, uint32 alto)
	constexpr REL::ID kRenderManager{ 411393 };     // BSRenderManager (objeto global)
	constexpr REL::ID kAplicarCapas{ 27040 };       // void (BSTArray<TintMask*>&, NiRenderedTexture*)
	constexpr std::uintptr_t kOffsetBuffer = 0x48;  // NiRenderedTexture::buffer
	constexpr std::uint32_t kLado = 512;

	struct Capa
	{
		std::uint32_t tipo;
		std::uint32_t argb;
		std::string ruta;
	};

	struct Pintada
	{
		std::string capas;
		RE::NiTexture* textura{ nullptr };  // solo para comparar, no se usa como puntero
	};

	std::mutex g_m;
	std::unordered_map<RE::FormID, Pintada> g_pintadas;

	std::vector<Capa> LeerCapas(std::string_view s)
	{
		std::vector<Capa> res;
		std::size_t i = 0;
		while (i < s.size()) {
			auto fin = s.find(';', i);
			if (fin == std::string_view::npos)
				fin = s.size();
			auto trozo = s.substr(i, fin - i);
			i = fin + 1;
			auto a = trozo.find('|');
			auto b = a == std::string_view::npos ? a : trozo.find('|', a + 1);
			if (b == std::string_view::npos)
				continue;
			try {
				Capa c;
				c.tipo = static_cast<std::uint32_t>(std::stoll(std::string(trozo.substr(0, a))));
				c.argb = static_cast<std::uint32_t>(std::stoll(std::string(trozo.substr(a + 1, b - a - 1))));
				c.ruta = std::string(trozo.substr(b + 1));
				if (!c.ruta.empty() && c.ruta.size() < 260)
					res.push_back(std::move(c));
			} catch (...) {
			}
			if (res.size() >= 64)
				break;
		}
		return res;
	}

	RE::BSLightingShaderMaterialFacegen* MaterialDeCara(RE::Actor* a_actor, int& a_codigo)
	{
		a_codigo = 0;
		if (!a_actor->Is3DLoaded())
			return nullptr;
		auto objeto = a_actor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kFace);
		if (!objeto)
			return nullptr;
		auto geometria = objeto->AsGeometry();
		if (!geometria)
			return nullptr;
		auto efecto = geometria->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect].get();
		auto sombreado = netimmerse_cast<RE::BSLightingShaderProperty*>(efecto);
		if (!sombreado || !sombreado->material)
			return nullptr;
		if (sombreado->material->GetFeature() != RE::BSShaderMaterial::Feature::kFaceGen) {
			a_codigo = -3;
			return nullptr;
		}
		return static_cast<RE::BSLightingShaderMaterialFacegen*>(sombreado->material);
	}

	RE::NiTexture* Fabricar(const std::vector<Capa>& a_capas)
	{
		using CrearTextura_t = RE::NiTexture*(RE::BSFixedString&);
		using CrearBuffer_t = void*(void*, std::uint32_t, std::uint32_t);
		using AplicarCapas_t = void(RE::BSTArray<RE::TintMask*>&, RE::NiTexture*);
		REL::Relocation<CrearTextura_t*> crearTextura{ kCrearTextura };
		REL::Relocation<CrearBuffer_t*> crearBuffer{ kCrearBuffer };
		REL::Relocation<AplicarCapas_t*> aplicarCapas{ kAplicarCapas };
		REL::Relocation<void*> renderManager{ kRenderManager };

		RE::BSFixedString nombre{ "" };
		auto textura = crearTextura(nombre);
		if (!textura)
			return nullptr;
		auto buffer = crearBuffer(renderManager.get(), kLado, kLado);
		if (!buffer) {
			return nullptr;
		}
		*reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(textura) + kOffsetBuffer) = buffer;

		RE::BSTArray<RE::TintMask*> capas;
		std::vector<RE::TESTexture*> texturas;
		for (auto& c : a_capas) {
			auto t = static_cast<RE::TESTexture*>(RE::malloc(sizeof(RE::TESTexture)));
			auto m = static_cast<RE::TintMask*>(RE::malloc(sizeof(RE::TintMask)));
			if (!t || !m) {
				if (t)
					RE::free(t);
				if (m)
					RE::free(m);
				continue;
			}
			std::memset(t, 0, sizeof(RE::TESTexture));
			*reinterpret_cast<std::uintptr_t*>(t) = RE::VTABLE_TESTexture[0].address();
			new (&t->textureName) RE::BSFixedString(c.ruta.c_str());
			std::memset(m, 0, sizeof(RE::TintMask));
			m->texture = t;
			const auto a = static_cast<std::uint8_t>((c.argb >> 24) & 0xFF);
			m->color.red = static_cast<std::uint8_t>((c.argb >> 16) & 0xFF);
			m->color.green = static_cast<std::uint8_t>((c.argb >> 8) & 0xFF);
			m->color.blue = static_cast<std::uint8_t>(c.argb & 0xFF);
			m->color.alpha = a;
			m->alpha = a / 255.0f;
			m->type = static_cast<RE::TintMask::Type>(c.tipo);
			capas.push_back(m);
			texturas.push_back(t);
		}

		aplicarCapas(capas, textura);

		for (auto m : capas)
			RE::free(m);
		for (auto t : texturas) {
			t->textureName.~BSFixedString();
			RE::free(t);
		}
		return textura;
	}

	std::int32_t PintarCara(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::BSFixedString a_capas)
	{
		if (!a_actor)
			return -1;
		const std::string texto = a_capas.c_str() ? a_capas.c_str() : "";
		const auto capas = LeerCapas(texto);
		if (capas.empty())
			return -2;

		int codigo = 0;
		auto material = MaterialDeCara(a_actor, codigo);
		if (!material)
			return codigo;

		const auto id = a_actor->GetFormID();
		std::lock_guard l(g_m);
		auto it = g_pintadas.find(id);
		RE::NiTexture* actual = material->tintTexture.get();
		if (it != g_pintadas.end() && it->second.capas == texto && it->second.textura == actual && actual)
			return 1;

		auto textura = Fabricar(capas);
		if (!textura) {
			SKSE::log::warn("{:08X}: no se pudo crear la textura", id);
			return -4;
		}
		// tintTexture es NiPointer<NiSourceTexture> en CommonLib; el motor solo necesita una NiTexture.
		reinterpret_cast<RE::NiPointer<RE::NiTexture>&>(material->tintTexture) = RE::NiPointer<RE::NiTexture>(textura);
		g_pintadas[id] = Pintada{ texto, textura };
		SKSE::log::info("{:08X} «{}»: cara pintada con {} capas (material {})", id, a_actor->GetDisplayFullName(),
			capas.size(), static_cast<void*>(material));
		return 2;
	}

	std::int32_t Version(RE::StaticFunctionTag*)
	{
		return kVersion;
	}

	// soltarTeclas1 · «Autowalk» al abrir el chat de SkyMP andando. Mientras el navegador tiene el foco, SkyrimPlatform
	// tira los eventos del teclado que iban al juego (DInputHook: GetDeviceData devuelve 0 eventos), y también el de
	// soltar la W: el teclado del juego (BSWin32KeyboardDevice::curState) se queda con la W pulsada y el personaje
	// sigue andando hasta que la vuelves a pulsar. Esto pone «suelta» en el juego cada tecla que Windows dice que ya
	// no está pulsada. No inventa eventos (Input.ReleaseKey/TapKey de SKSE meten eventos falsos en el búfer de
	// DirectInput y con SkyMP llegaron a romper el juego). Devuelve cuántas teclas ha soltado (-1 sin teclado).
	std::int32_t SoltarTeclas(RE::StaticFunctionTag*)
	{
		auto* gestor = RE::BSInputDeviceManager::GetSingleton();
		auto* teclado = gestor ? gestor->GetKeyboard() : nullptr;
		if (!teclado)
			return -1;
		std::int32_t soltadas = 0;
		for (unsigned int dik = 1; dik < 0x100; ++dik) {
			if (!(teclado->curState[dik] & 0x80))
				continue;
			// DIK = código de escaneo; los de 0x80 en adelante son los extendidos (E0 xx).
			const unsigned int escaneo = dik < 0x80 ? dik : (0xE000 | (dik & 0x7F));
			const unsigned int vk = MapVirtualKeyW(escaneo, 3 /* MAPVK_VSC_TO_VK_EX */);
			if (vk == 0)
				continue;  // no sabemos qué tecla es: mejor no tocarla
			if (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000)
				continue;  // de verdad está pulsada
			teclado->curState[dik] = 0;
			++soltadas;
		}
		if (soltadas > 0)
			SKSE::log::info("SoltarTeclas: {} tecla(s) que el juego creía pulsadas", soltadas);
		return soltadas;
	}

	// hierba1 · La hierba atraviesa los huertos y muebles que pone el servidor: NGIO (NGIO-NG.dll) quita la hierba que
	// choca con objetos, pero solo cuando el juego crea la hierba de la celda, y eso pasa antes de que lleguen los
	// objetos del servidor. RehacerHierba(x, y, radio) quita la hierba de las celdas exteriores cargadas que toca ese
	// círculo y vuelve a encolar su tarea de hierba; NGIO la intercepta, espera a que la colisión esté en el mundo y no
	// pone la que choca. Sin NGIO no hace nada (solo daría un parpadeo). Devuelve 1 encolado, -1 sin NGIO.
	// Address Library (AE): BGSGrassManager::RemoveCellGrass y AddCellGrassTask::CreateGrassLoadTaskForCell (las mismas
	// que trae CommonLibSSE-NG en la rama ng de alandtse/CommonLibVR).
	constexpr REL::ID kQuitarHierbaCelda{ 15375 };   // void (BGSGrassManager*, TESObjectCELL*)
	constexpr REL::ID kEncolarHierbaCelda{ 13277 };  // void (TESObjectCELL*)
	constexpr REL::ID kCrearHierbaCelda{ 15372 };    // void (BGSGrassManager*, TESObjectCELL*, volatile bool* cancelar)
	constexpr int     kMaxEsperas = 180;             // fotogramas esperando a que acabe la tarea que ya había (~3 s)
	constexpr int     kComprobarTras = 90;           // hierba2: fotogramas tras encolar para ver si ha vuelto la hierba
	constexpr float   kLadoCelda = 4096.0f;

	// Se mira cada vez: al cargarse AesCaras puede que SKSE aún no haya cargado NGIO.
	bool HayNgio()
	{
		return GetModuleHandleW(L"NGIO-NG.dll") != nullptr;
	}

	// hierba2 · Diagnóstico: cuántos trozos de hierba tiene la celda y si hay una tarea de hierba en cola.
	std::pair<std::size_t, bool> EstadoHierba(RE::TESObjectCELL* a_celda)
	{
		RE::BSSpinLockGuard tarea(a_celda->grassTaskLock);
		auto* datos = a_celda->extraList.GetByType<RE::ExtraCellGrassData>();
		if (!datos)
			return { 0, false };
		return { datos->grassHandles.size(), static_cast<bool>(datos->addGrassTask) };
	}

	int registros = 0;  // para no llenar el log: las 40 primeras celdas con detalle
	bool Detalle() { return registros < 40; }

	// hierba2 · Si la tarea encolada no ha vuelto a poner la hierba (la celda sigue sin trozos y sin tarea), se crea
	// directamente con BGSGrassManager::AddCellGrass en el hilo principal. NGIO también la filtra ahí (CreatePatch).
	void Comprobar(RE::FormID a_id, int a_fotograma)
	{
		auto* celda = RE::TESForm::LookupByID<RE::TESObjectCELL>(a_id);
		auto* hierba = RE::BGSGrassManager::GetSingleton();
		if (!celda || !hierba || !celda->IsAttached())
			return;
		auto [trozos, enCola] = EstadoHierba(celda);
		if (enCola || (trozos == 0 && a_fotograma < kComprobarTras)) {
			if (a_fotograma < kComprobarTras * 3)
				SKSE::GetTaskInterface()->AddTask([a_id, a_fotograma]() { Comprobar(a_id, a_fotograma + 1); });
			return;
		}
		if (trozos > 0) {
			if (Detalle())
				SKSE::log::info("RehacerHierba {:08X}: la tarea ha vuelto a poner la hierba ({} trozos, {} fotogramas)", a_id, trozos, a_fotograma);
			++registros;
			return;
		}
		volatile bool cancelar = false;
		{
			RE::BSSpinLockGuard crear(celda->grassCreateLock);
			REL::Relocation<void (*)(RE::BGSGrassManager*, RE::TESObjectCELL*, volatile bool*)> crearHierba{ kCrearHierbaCelda };
			crearHierba(hierba, celda, &cancelar);
		}
		auto [despues, _] = EstadoHierba(celda);
		if (Detalle())
			SKSE::log::info("RehacerHierba {:08X}: la tarea no la ha puesto; creada directamente ({} trozos)", a_id, despues);
		++registros;
	}

	void RehacerCelda(RE::FormID a_id, int a_espera)
	{
		auto* celda = RE::TESForm::LookupByID<RE::TESObjectCELL>(a_id);
		auto* tes = RE::TES::GetSingleton();
		auto* hierba = RE::BGSGrassManager::GetSingleton();
		// hierba3: el mundo se compara con el del jugador (TES::worldSpace no cuadra en 1.6.1170: decía que no era el mismo).
		auto* jugador = RE::PlayerCharacter::GetSingleton();
		auto* mundo = jugador ? jugador->GetWorldspace() : nullptr;
		if (!celda || !tes || !hierba || !hierba->enableGrass || celda->IsInteriorCell() || !celda->IsAttached() ||
			!mundo || celda->GetRuntimeData().worldSpace != mundo) {
			if (Detalle())
				SKSE::log::info("RehacerHierba {:08X}: no se toca (celda {}, hierba {}, cargada {}, interior {}, mundo de la celda {:08X}, del jugador {:08X})",
					a_id, celda ? "sí" : "no", hierba && hierba->enableGrass ? "activada" : "desactivada",
					celda && celda->IsAttached() ? "sí" : "no", celda && celda->IsInteriorCell() ? "sí" : "no",
					celda && celda->GetRuntimeData().worldSpace ? celda->GetRuntimeData().worldSpace->GetFormID() : 0u,
					mundo ? mundo->GetFormID() : 0u);
			++registros;
			return;
		}
		{
			RE::BSSpinLockGuard tarea(celda->grassTaskLock);
			auto* datos = celda->extraList.GetByType<RE::ExtraCellGrassData>();
			if (datos && datos->addGrassTask) {
				// Ya tiene una tarea en cola (pudo encolarse antes de que llegara el objeto): otra vez luego.
				if (a_espera < kMaxEsperas)
					SKSE::GetTaskInterface()->AddTask([a_id, a_espera]() { RehacerCelda(a_id, a_espera + 1); });
				return;
			}
		}
		const auto antes = EstadoHierba(celda).first;
		{
			RE::BSSpinLockGuard crear(celda->grassCreateLock);
			REL::Relocation<void (*)(RE::BGSGrassManager*, RE::TESObjectCELL*)> quitar{ kQuitarHierbaCelda };
			quitar(hierba, celda);
		}
		const auto quitada = EstadoHierba(celda).first;
		// hierba6 · Se vuelve a crear en el mismo fotograma, sin dejar la celda sin hierba hasta que acabe la tarea (eso era
		// el parpadeo). NGIO la filtra igual que en la tarea (CreatePatch), con los objetos ya puestos. Si no sale nada
		// (la hierba aún no estaba lista), como antes: se encola la tarea y se comprueba.
		{
			const auto t0 = std::chrono::steady_clock::now();
			volatile bool cancelar = false;
			{
				RE::BSSpinLockGuard crear(celda->grassCreateLock);
				REL::Relocation<void (*)(RE::BGSGrassManager*, RE::TESObjectCELL*, volatile bool*)> crearHierba{ kCrearHierbaCelda };
				crearHierba(hierba, celda, &cancelar);
			}
			const auto creada = EstadoHierba(celda).first;
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
			if (creada > 0 || antes == 0) {
				if (Detalle())
					SKSE::log::info("RehacerHierba {:08X}: trozos antes {} · tras quitar {} · creada al momento {} ({} ms)", a_id, antes, quitada, creada, ms);
				++registros;
				return;
			}
			if (Detalle())
				SKSE::log::info("RehacerHierba {:08X}: al momento no ha salido ({} ms); se encola la tarea", a_id, ms);
		}
		{
			RE::BSSpinLockGuard tarea(celda->grassTaskLock);
			REL::Relocation<void (*)(RE::TESObjectCELL*)> encolar{ kEncolarHierbaCelda };
			encolar(celda);
		}
		const auto [tras, enCola] = EstadoHierba(celda);
		if (Detalle())
			SKSE::log::info("RehacerHierba {:08X}: trozos antes {} · tras quitar {} · tras encolar {} · tarea en cola {}",
				a_id, antes, quitada, tras, enCola ? "sí" : "no");
		SKSE::GetTaskInterface()->AddTask([a_id]() { Comprobar(a_id, 1); });
	}

	// Las celdas exteriores cargadas (las de uGridsToLoad) cuyo cuadrado toca el círculo.
	void RehacerZona(float a_x, float a_y, float a_radio)
	{
		auto* tes = RE::TES::GetSingleton();
		auto* rejilla = tes ? tes->gridCells : nullptr;
		if (!rejilla)
			return;
		std::vector<RE::FormID> celdas;
		for (std::uint32_t i = 0; i < rejilla->length; ++i) {
			for (std::uint32_t j = 0; j < rejilla->length; ++j) {
				auto* celda = rejilla->GetCell(i, j);
				if (!celda || !celda->IsExteriorCell() || !celda->IsAttached())
					continue;
				auto* coord = celda->GetCoordinates();
				if (!coord)
					continue;
				const float x0 = coord->cellX * kLadoCelda, y0 = coord->cellY * kLadoCelda;
				const float cx = std::clamp(a_x, x0, x0 + kLadoCelda), cy = std::clamp(a_y, y0, y0 + kLadoCelda);
				if ((cx - a_x) * (cx - a_x) + (cy - a_y) * (cy - a_y) <= a_radio * a_radio)
					celdas.push_back(celda->GetFormID());
			}
		}
		if (Detalle())
			SKSE::log::info("RehacerHierba ({:.0f}, {:.0f}, r {:.0f}): {} celda(s) cargada(s) lo tocan", a_x, a_y, a_radio, celdas.size());
		for (auto id : celdas)
			RehacerCelda(id, 0);
	}

	std::int32_t RehacerHierba(RE::StaticFunctionTag*, float a_x, float a_y, float a_radio)
	{
		static bool avisado = false;
		if (!avisado) {
			avisado = true;
			SKSE::log::info("RehacerHierba: NGIO {}", HayNgio() ? "cargado" : "no está (no se hace nada)");
		}
		if (!HayNgio())
			return -1;
		const float radio = std::clamp(a_radio, 0.0f, 8192.0f);
		SKSE::GetTaskInterface()->AddTask([a_x, a_y, radio]() { RehacerZona(a_x, a_y, radio); });
		return 1;
	}

	// v8 (giro2): el creador de personaje giraba al jugador con SetAngle de Papyrus, que para el motor es un «salto»
	// (como un teletransporte): rehace físicas y el juego se congela un instante. GirarJugador(grados) solo cambia el
	// rumbo, como cuando el jugador gira con el ratón (Actor::SetRotationZ), en el hilo principal del juego. El creador
	// lo llama en cada fotograma con un ángulo que avanza poco a poco, y el giro se ve suave.
	// Devuelve 1 si lo ha encolado, 0 si aún no hay jugador.
	std::int32_t GirarJugador(RE::StaticFunctionTag*, float a_grados)
	{
		if (!RE::PlayerCharacter::GetSingleton())
			return 0;
		const float radianes = a_grados * (std::numbers::pi_v<float> / 180.0f);
		static bool primera = true;
		if (primera) {
			primera = false;
			SKSE::log::info("GirarJugador: primera llamada ({:.1f} grados)", a_grados);
		}
		SKSE::GetTaskInterface()->AddTask([radianes]() {
			if (auto jugador = RE::PlayerCharacter::GetSingleton())
				jugador->SetRotationZ(radianes);
		});
		return 1;
	}

	// v9 (camara1): los encuadres del creador (cara, cuerpo, pies, entero) mueven la cámara libre (tfc), que Papyrus no
	// deja tocar. CamaraLibre(dato) lee 0-2 la posición x, y, z, 3 el cabeceo y 4 el rumbo (en radianes, tal como los
	// guarda el juego); -99999 si la cámara no está en modo libre. PonerCamaraLibre(x, y, z, cabeceo, rumbo) la coloca
	// (en el hilo principal); devuelve 1 si lo ha encolado, 0 si la cámara no está en modo libre.
	RE::FreeCameraState* CamaraLibreActual()
	{
		auto camara = RE::PlayerCamera::GetSingleton();
		if (!camara || !camara->currentState || camara->currentState->id != RE::CameraState::kFree)
			return nullptr;
		return static_cast<RE::FreeCameraState*>(camara->currentState.get());
	}

	float CamaraLibre(RE::StaticFunctionTag*, std::int32_t a_dato)
	{
		auto libre = CamaraLibreActual();
		if (!libre)
			return -99999.0f;
		switch (a_dato) {
		case 0: return libre->translation.x;
		case 1: return libre->translation.y;
		case 2: return libre->translation.z;
		case 3: return libre->rotation.x;
		case 4: return libre->rotation.y;
		default: return -99999.0f;
		}
	}

	std::int32_t PonerCamaraLibre(RE::StaticFunctionTag*, float a_x, float a_y, float a_z, float a_cabeceo, float a_rumbo)
	{
		if (!CamaraLibreActual())
			return 0;
		static bool primera = true;
		if (primera) {
			primera = false;
			SKSE::log::info("PonerCamaraLibre: primera llamada ({:.0f}, {:.0f}, {:.0f}; cabeceo {:.2f}, rumbo {:.2f})", a_x, a_y, a_z, a_cabeceo, a_rumbo);
		}
		SKSE::GetTaskInterface()->AddTask([a_x, a_y, a_z, a_cabeceo, a_rumbo]() {
			if (auto libre = CamaraLibreActual()) {
				libre->translation = RE::NiPoint3(a_x, a_y, a_z);
				libre->rotation.x = a_cabeceo;
				libre->rotation.y = a_rumbo;
			}
		});
		return 1;
	}

	// v10 (pegar1): el navegador del juego no deja leer el portapapeles (navigator.clipboard falla), así que Ctrl+V y
	// el botón «Pegar» del creador lo leen aquí: el texto copiado en Windows (CF_UNICODETEXT), en UTF-8, como mucho
	// 200 caracteres; "" si no hay texto o no se puede abrir.
	RE::BSFixedString LeerPortapapeles(RE::StaticFunctionTag*)
	{
		std::string texto;
		if (!OpenClipboard(nullptr))
			return RE::BSFixedString(texto.c_str());
		if (void* datos = GetClipboardData(13)) {  // CF_UNICODETEXT
			if (auto w = static_cast<const wchar_t*>(GlobalLock(datos))) {
				for (std::size_t i = 0, n = 0; w[i] && n < 200; ++i, ++n) {
					std::uint32_t c = static_cast<std::uint16_t>(w[i]);
					if (c >= 0xD800 && c <= 0xDBFF && w[i + 1] >= 0xDC00 && w[i + 1] <= 0xDFFF) {
						c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<std::uint16_t>(w[i + 1]) - 0xDC00);
						++i;
					}
					if (c < 0x20 || (c >= 0xD800 && c <= 0xDFFF)) c = ' ';  // saltos de línea y sueltos: espacio
					if (c < 0x80) {
						texto += static_cast<char>(c);
					} else if (c < 0x800) {
						texto += static_cast<char>(0xC0 | (c >> 6));
						texto += static_cast<char>(0x80 | (c & 0x3F));
					} else if (c < 0x10000) {
						texto += static_cast<char>(0xE0 | (c >> 12));
						texto += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
						texto += static_cast<char>(0x80 | (c & 0x3F));
					} else {
						texto += static_cast<char>(0xF0 | (c >> 18));
						texto += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
						texto += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
						texto += static_cast<char>(0x80 | (c & 0x3F));
					}
				}
				GlobalUnlock(datos);
			}
		}
		CloseClipboard();
		return RE::BSFixedString(texto.c_str());
	}

	bool Registrar(RE::BSScript::IVirtualMachine* a_vm)
	{
		a_vm->RegisterFunction("PintarCara"sv, "AesCaras"sv, PintarCara);
		a_vm->RegisterFunction("Version"sv, "AesCaras"sv, Version);
		a_vm->RegisterFunction("SoltarTeclas"sv, "AesCaras"sv, SoltarTeclas);
		a_vm->RegisterFunction("RehacerHierba"sv, "AesCaras"sv, RehacerHierba);  // hierba1
		a_vm->RegisterFunction("GirarJugador"sv, "AesCaras"sv, GirarJugador);    // giro2
		a_vm->RegisterFunction("CamaraLibre"sv, "AesCaras"sv, CamaraLibre);      // camara1
		a_vm->RegisterFunction("PonerCamaraLibre"sv, "AesCaras"sv, PonerCamaraLibre);
		a_vm->RegisterFunction("LeerPortapapeles"sv, "AesCaras"sv, LeerPortapapeles);  // pegar1
		SKSE::log::info("funciones registradas");
		return true;
	}

	// ajustes1 · El servidor fija algunos ajustes de otros mods para todos los jugadores (la hierba de NGIO, el parallax
	// de Community Shaders…). Los manda en Data/SKSE/Plugins/AesAjustes.ini, dentro del paquete del cliente; al
	// arrancar el juego (antes de que esos mods lean sus archivos: SKSE carga los plugins por orden alfabético y
	// AesCaras va primero) se cambian solo esas claves, el resto de cada archivo queda igual. Formato:
	//   [Data/SKSE/Plugins/GrassControl.ini]                          ← un .ini: «clave = valor» (solo si la clave ya está)
	//   Ray-cast-width-multiplier = 1.0
	//   [Data/SKSE/Plugins/CommunityShaders/SettingsUser.json|Extended Materials]   ← un bloque de un .json
	//   EnableParallax = 0
	// Las rutas son desde la carpeta del juego (la del .exe). Si algo no cuadra, no se toca ese archivo (queda en el registro).
	std::string Recortar(std::string a_t)
	{
		const auto ini = a_t.find_first_not_of(" \t\r\n");
		if (ini == std::string::npos)
			return {};
		const auto fin = a_t.find_last_not_of(" \t\r\n");
		return a_t.substr(ini, fin - ini + 1);
	}

	bool LeerArchivo(const std::filesystem::path& a_ruta, std::string& a_texto)
	{
		std::ifstream f(a_ruta, std::ios::binary);
		if (!f)
			return false;
		std::ostringstream o;
		o << f.rdbuf();
		a_texto = o.str();
		return true;
	}

	// En un .ini: cambia el valor de las líneas «clave = …» (sin añadir las que no están).
	int CambiarIni(std::string& a_texto, const std::vector<std::pair<std::string, std::string>>& a_valores)
	{
		int cambios = 0;
		std::size_t pos = 0;
		while (pos < a_texto.size()) {
			auto fin = a_texto.find('\n', pos);
			if (fin == std::string::npos)
				fin = a_texto.size();
			auto lin = a_texto.substr(pos, fin - pos);
			const auto igual = lin.find('=');
			if (igual != std::string::npos) {
				const auto clave = Recortar(lin.substr(0, igual));
				for (const auto& [k, v] : a_valores) {
					if (clave != k)
						continue;
					const bool cr = !lin.empty() && lin.back() == '\r';
					auto actual = Recortar(lin.substr(igual + 1));
					if (actual == v)
						break;
					const bool espacio = igual + 1 < lin.size() && (lin[igual + 1] == ' ' || lin[igual + 1] == '\t');
					auto nueva = lin.substr(0, igual + 1) + (espacio ? " " : "") + v + (cr ? "\r" : "");
					a_texto.replace(pos, fin - pos, nueva);
					fin = pos + nueva.size();
					++cambios;
					break;
				}
			}
			pos = fin + 1;
		}
		return cambios;
	}

	// En un .json: dentro del bloque "nombre": { … } cambia o añade «"clave": valor». -1 si no hay tal bloque.
	int CambiarJson(std::string& a_texto, const std::string& a_bloque, const std::vector<std::pair<std::string, std::string>>& a_valores)
	{
		const auto nombre = a_texto.find("\"" + a_bloque + "\"");
		if (nombre == std::string::npos)
			return -1;
		const auto abre = a_texto.find('{', nombre);
		if (abre == std::string::npos)
			return -1;
		int cambios = 0;
		for (const auto& [k, v] : a_valores) {
			// el final del bloque (se busca cada vez: cambia al escribir)
			int nivel = 0;
			std::size_t cierra = std::string::npos;
			for (auto i = abre; i < a_texto.size(); ++i) {
				if (a_texto[i] == '{')
					++nivel;
				else if (a_texto[i] == '}' && --nivel == 0) {
					cierra = i;
					break;
				}
			}
			if (cierra == std::string::npos)
				return -1;
			const auto clave = a_texto.find("\"" + k + "\"", abre);
			if (clave != std::string::npos && clave < cierra) {
				const auto dos = a_texto.find(':', clave);
				auto ini = a_texto.find_first_not_of(" \t", dos + 1);
				auto fin = a_texto.find_first_of(",}\r\n", ini);
				if (dos == std::string::npos || ini == std::string::npos || fin == std::string::npos || fin > cierra)
					continue;
				while (fin > ini && (a_texto[fin - 1] == ' ' || a_texto[fin - 1] == '\t'))
					--fin;
				if (a_texto.substr(ini, fin - ini) == v)
					continue;
				a_texto.replace(ini, fin - ini, v);
			} else {
				const bool vacio = Recortar(a_texto.substr(abre + 1, cierra - abre - 1)).empty();
				a_texto.insert(abre + 1, "\n        \"" + k + "\": " + v + (vacio ? "\n    " : ","));
			}
			++cambios;
		}
		return cambios;
	}

	void AplicarAjustes()
	{
		try {
			wchar_t exe[1024]{};
			if (!GetModuleFileNameW(nullptr, exe, 1024))
				return;
			const auto juego = std::filesystem::path(exe).parent_path();
			const auto plugins = juego / "Data" / "SKSE" / "Plugins";
			std::string conf;
			if (!LeerArchivo(plugins / "AesAjustes.ini", conf)) {
				SKSE::log::info("ajustes: no hay AesAjustes.ini (no se toca nada)");
				return;
			}
			struct Grupo { std::string archivo, bloque; std::vector<std::pair<std::string, std::string>> valores; };
			std::vector<Grupo> grupos;
			std::istringstream lineas(conf);
			for (std::string l; std::getline(lineas, l);) {
				l = Recortar(l);
				if (l.empty() || l[0] == ';' || l[0] == '#')
					continue;
				if (l.front() == '[' && l.back() == ']') {
					auto dentro = l.substr(1, l.size() - 2);
					const auto barra = dentro.find('|');
					grupos.push_back({ Recortar(dentro.substr(0, barra)), barra == std::string::npos ? "" : Recortar(dentro.substr(barra + 1)), {} });
					continue;
				}
				const auto igual = l.find('=');
				if (igual != std::string::npos && !grupos.empty())
					grupos.back().valores.emplace_back(Recortar(l.substr(0, igual)), Recortar(l.substr(igual + 1)));
			}
			for (const auto& g : grupos) {
				const auto ruta = juego / std::filesystem::path(g.archivo);
				if (g.archivo.find("..") != std::string::npos) {
					SKSE::log::info("ajustes: {}: ruta no válida", g.archivo);
					continue;
				}
				std::string texto;
				if (!LeerArchivo(ruta, texto)) {
					SKSE::log::info("ajustes: {}: no está (no se toca)", g.archivo);
					continue;
				}
				const int n = g.bloque.empty() ? CambiarIni(texto, g.valores) : CambiarJson(texto, g.bloque, g.valores);
				if (n < 0) {
					SKSE::log::info("ajustes: {}: no tiene el bloque «{}» (no se toca)", g.archivo, g.bloque);
					continue;
				}
				if (n == 0) {
					SKSE::log::info("ajustes: {}: ya estaba", g.archivo);
					continue;
				}
				std::ofstream f(ruta, std::ios::binary | std::ios::trunc);
				f << texto;
				SKSE::log::info("ajustes: {}: {} valor(es) cambiado(s){}", g.archivo, n, f ? "" : " (¡no se ha podido escribir!)");
			}
		} catch (const std::exception& e) {
			SKSE::log::error("ajustes: error: {}", e.what());
		} catch (...) {
			SKSE::log::error("ajustes: error");
		}
	}

	void IniciarRegistro()
	{
		auto carpeta = SKSE::log::log_directory();
		if (!carpeta)
			return;
		auto ruta = *carpeta / "AesCaras.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(ruta.string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S] %v");
	}
}

SKSEPluginInfo(
	.Version = REL::Version{ 10, 0, 0, 0 },
	.Name = "AesCaras"sv,
	.Author = "Aestherius"sv,
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	IniciarRegistro();
	SKSE::log::info("AesCaras {} cargado (juego {})", kVersion, REL::Module::get().version().string());
	AplicarAjustes();  // ajustes1: antes que nada (y antes de que los otros plugins lean sus archivos)
	if (REL::Module::IsSE() || REL::Module::IsVR()) {
		SKSE::log::error("solo funciona con Skyrim AE (1.6.x)");
		return true;
	}
	SKSE::GetPapyrusInterface()->Register(Registrar);
	return true;
}
