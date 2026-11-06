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
// asCapas: "tipo|argb|ruta;tipo|argb|ruta;..." (argb en decimal, con o sin signo).
// Devuelve: 2 pintada ahora, 1 ya estaba pintada con esas capas, 0 su cabeza aún no está (reintentar),
//          -1 actor nulo, -2 sin capas válidas, -3 su cabeza no usa el sombreado de cara, -4 no se pudo crear la textura.

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>

#include <cstring>

// soltarTeclas1: para saber si una tecla está pulsada de verdad (user32).
extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int vKey);
extern "C" __declspec(dllimport) unsigned int __stdcall MapVirtualKeyW(unsigned int uCode, unsigned int uMapType);
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std::literals;

namespace
{
	constexpr std::int32_t kVersion = 2;

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

	bool Registrar(RE::BSScript::IVirtualMachine* a_vm)
	{
		a_vm->RegisterFunction("PintarCara"sv, "AesCaras"sv, PintarCara);
		a_vm->RegisterFunction("Version"sv, "AesCaras"sv, Version);
		a_vm->RegisterFunction("SoltarTeclas"sv, "AesCaras"sv, SoltarTeclas);
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
	.Version = REL::Version{ 2, 0, 0, 0 },
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
