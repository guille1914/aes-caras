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
// asCapas: "tipo|argb|ruta;tipo|argb|ruta;..." (argb en decimal, con o sin signo).
// Devuelve: 2 pintada ahora, 1 ya estaba pintada con esas capas, 0 su cabeza aún no está (reintentar),
//          -1 actor nulo, -2 sin capas válidas, -3 su cabeza no usa el sombreado de cara, -4 no se pudo crear la textura.

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>
#include <cstring>

// soltarTeclas1: para saber si una tecla está pulsada de verdad (user32).
extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int vKey);
extern "C" __declspec(dllimport) unsigned int __stdcall MapVirtualKeyW(unsigned int uCode, unsigned int uMapType);
// hierba1: para saber si NGIO está cargado (kernel32).
extern "C" __declspec(dllimport) void* __stdcall GetModuleHandleW(const wchar_t* lpModuleName);
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std::literals;

namespace
{
	constexpr std::int32_t kVersion = 5;

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

	bool Registrar(RE::BSScript::IVirtualMachine* a_vm)
	{
		a_vm->RegisterFunction("PintarCara"sv, "AesCaras"sv, PintarCara);
		a_vm->RegisterFunction("Version"sv, "AesCaras"sv, Version);
		a_vm->RegisterFunction("SoltarTeclas"sv, "AesCaras"sv, SoltarTeclas);
		a_vm->RegisterFunction("RehacerHierba"sv, "AesCaras"sv, RehacerHierba);  // hierba1
		SKSE::log::info("funciones registradas");
		return true;
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
	.Version = REL::Version{ 5, 0, 0, 0 },
	.Name = "AesCaras"sv,
	.Author = "Aestherius"sv,
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	IniciarRegistro();
	SKSE::log::info("AesCaras {} cargado (juego {})", kVersion, REL::Module::get().version().string());
	if (REL::Module::IsSE() || REL::Module::IsVR()) {
		SKSE::log::error("solo funciona con Skyrim AE (1.6.x)");
		return true;
	}
	SKSE::GetPapyrusInterface()->Register(Registrar);
	return true;
}
