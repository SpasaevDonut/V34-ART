# AGENTS.md — Архитектурный гайд по кодовой базе CS:S V34 ART

Этот документ предназначен для AI-агентов и разработчиков, чтобы быстро ориентироваться в структуре проекта, архитектуре подсистем, хуках движка и расположении ключевых компонентов.

---

## 1. Обзор проекта и технологический стек

- **Целевая платформа:** Counter-Strike: Source v34 (**Engine build 4044**, 32-bit x86, DirectX 9, `hl2.exe`).
- **Стек:** C++17, Win32 API, Direct3D 9, Dear ImGui (v1.91.x), MinHook (v1.3.4), ExtendScript (Adobe After Effects JSX), PowerShell.
- **Архитектура сборки:** Исключительно `Release|Win32` / `Debug|Win32`. Сборка 64-бит не поддерживается из-за архитектуры `hl2.exe`.

---

## 2. Навигатор по файлам и каталогам (Map of Repository)

### 2.1. Ядро инжектируемой DLL (`dll/`)

| Файл | Назначение и ключевые компоненты |
| :--- | :--- |
| [`dll/v34_art.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/v34_art.cpp) | **Точка входа (`DllMain`).** Закрепляет модуль в памяти (`GET_MODULE_HANDLE_EX_FLAG_PIN`), асинхронно дожидается загрузки модулей игры (`client.dll`, `engine.dll`, `materialsystem.dll`, `GameUI.dll`), инициализирует интерфейсы Source SDK и устанавливает базовые рендер-хуки. |
| [`dll/art_internal.h`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_internal.h) | **Главный внутренний заголовок DLL.** Глобальные интерфейсы движка, структуры статистики, флаги проходов (`RecordBits`), режимы очередей, классы `ConVarRestore` и прототипы внутренних функций. |
| [`dll/art_render.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_render.cpp) | **Рендер-хуки движка Source:**<br>• `View_Render` (index 23 в `VClient013`) — оркестрация покадрового многопроходного рендеринга.<br>• `DrawModelEx` (index 19 в `VEngineModel012`) — фильтрация моделей, изоляция игроков, оклюдеры глубины, flat chams.<br>• `GetViewModelFOV` (index 32 в `IClientMode`) — переопределение FOV рук/оружия.<br>Реализация рендеринга 7 проходов (`normal`, `clear`, `clear-noplayers`, `viewmodel`, `players`, `depth`, `objectid`). |
| [`dll/art_pipeline.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_pipeline.cpp) | **Пайплайн асинхронного захвата и I/O.** Очередь записи через `IFileSystem->AsyncWrite`, учет таймингов стадий (render, read, encode, write, queue), контроль лимитов памяти 32-битного адресного пространства, TGA RLE компрессия. |
| [`dll/art_runtime.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_runtime.cpp) | **Состояние рантайма и пути.** Глобальные переменные состояния записи, генерация путей тейков (`cstrike/art/takeXXXX/`), парсеры цветов и безопасных имен, класс безопасного восстановления кваров `ConVarRestore`. |
| [`dll/art_commands.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_commands.cpp) | **Консольные команды.** Регистрация и обработчики команд: `art_start`, `art_stop`, `art_record`, `art_hud`, `art_preview`, `art_output_mode`, `art_ffmpeg_path`, `art_ffmpeg_preset`, `art_ffmpeg_custom`, `art_ffmpeg_test`, `art_ffmpeg_status`, `art_viewmodel_color`, `art_players_color`, `art_objectid_color`, `art_depth_start`, `art_depth_end`, `art_fov`, `art_queue`, `art_validation`, `art_gui`, и др. |
| [`dll/art_ffmpeg.h`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_ffmpeg.h) / [`art_ffmpeg.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_ffmpeg.cpp) | **Прямой видеопайплайн FFmpeg.** Создание анонимных Win32 пайпов, запуск дочерних процессов `ffmpeg.exe` на каждый активный проход, потоковая передача кадров из RAM в stdin без записи TGA на диск, пресеты кодеков (ProRes 422/4444, x264 HQ/Lossless, NVENC H.264/HEVC, Custom) и автопоиск `ffmpeg.exe` рядом с лоадером/DLL, в папке игры или в PATH. |
| [`dll/art_hlae.h`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_hlae.h) | **Интерфейс HLAE-моста.** Структуры состояния камеры, пути `mirv_campath`, 6-DOF ввода `mirv_input`, экспорта трекинга. |
| [`dll/art_hlae.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_hlae.cpp) | **Реализация команд и логики HLAE.** `mirv_campath`, `mirv_input`, `mirv_camio` (`.cam`), `mirv_camexport` / `mirv_camimport` (`.bvh`), `mirv_agr` (`.agr`), `mirv_fov`. |
| [`dll/art_hlae_advancedfx.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_hlae_advancedfx.cpp) | Интеграция математики и алгоритмов из AdvancedFX. |
| [`dll/art_gui.h`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_gui.h) | Внешний интерфейс оверлея: хукирование, проверка видимости, No-Flash, No-Smoke, Chams, ObjectID. |
| [`dll/art_gui.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_gui.cpp) | **Внутриигровой GUI на ImGui / D3D9.** Хуки D3D9 (`EndScene`, `Present`, `Reset`), обработка ввода Windows WndProc, реализация 9 страниц меню (*Capture, Passes, Visuals, Output, Info, HLAE, Configs, Console, Settings*), управление демками и спектатором, сохранение/загрузка конфигов. |
| [`dll/art_statistics.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_statistics.cpp) | **Манифест тейка и валидация.** Генерация `take.json`, замер объемов и кадров, фоновая валидация целостности записанных TGA (проверка заголовков, пропущенных кадров, повреждений). |
| [`dll/art_logic.h`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_logic.h) / [`art_logic.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_logic.cpp) | **Чистая движково-независимая логика.** Алгоритм сжатия TGA RLE, конвертация FOV (16:9 / 4:3 / вертикальный), парсинг RGB, санитайзер имен файлов. |
| [`dll/art_logging.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_logging.cpp) | Логирование в консоль и опциональный файл `art_debug.log`. |

---

### 2.2. Загрузчик (`loader/`)

| Файл | Описание |
| :--- | :--- |
| [`loader/v34_art_loader.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/loader/v34_art_loader.cpp) | Консольный Win32-загрузчик. Находит `hl2.exe`, проверяет разрядность (WOW64), проверяет, что DLL еще не загружена, выделяет память под путь DLL (`VirtualAllocEx`), вызывает `CreateRemoteThread` с `LoadLibraryW` и дожидается успешной инъекции. |

---

### 2.3. Инструменты для After Effects (`tools/after_effects/`)

| Файл | Описание |
| :--- | :--- |
| [`tools/after_effects/ART_Importer_v1.0.jsx`](file:///d:/RealDump/trashhsh/dev/V34-ART/tools/after_effects/ART_Importer_v1.0.jsx) | **Основной импортер тейков для AE.** Читает `take.json`, импортирует TGA/EXR сиквенции всех проходов, собирает прекомпозиции со слоями и масками, импортирует трекинг камеры `.cam` / `.bvh` и парсит бинарные файлы `.agr` (создает 3D Null-объекты для игроков, костей и энтити). |
| [`tools/after_effects/ART_Camera_Baker_v1.0.jsx`](file:///d:/RealDump/trashhsh/dev/V34-ART/tools/after_effects/ART_Camera_Baker_v1.0.jsx) | **Бейкер камеры при Time Remapping.** Сэмплирует свойства замедленной/ускоренной прекомпозиции и запекает обычные ключевые кадры для камеры и нуллов в мастер-композицию. |

---

### 2.4. Скрипты и тесты (`scripts/`, `tests/`)

| Файл / Папка | Описание |
| :--- | :--- |
| [`scripts/build.ps1`](file:///d:/RealDump/trashhsh/dev/V34-ART/scripts/build.ps1) | Основной скрипт сборки DLL + Loader + упаковка релиза в `dist/`. |
| [`scripts/package-release.ps1`](file:///d:/RealDump/trashhsh/dev/V34-ART/scripts/package-release.ps1) | Формирование релизного архива `dist/v34-art-v1.0.zip`. |
| [`scripts/verify-source.ps1`](file:///d:/RealDump/trashhsh/dev/V34-ART/scripts/verify-source.ps1) | Проверка целостности исходников, структуры файлов и запрещенных терминов. |
| [`tests/art_logic_tests.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/tests/art_logic_tests.cpp) | Модульные тесты FOV-математики, RLE-компрессора, парсеров и форматеров. |
| [`tests/source_contracts.ps1`](file:///d:/RealDump/trashhsh/dev/V34-ART/tests/source_contracts.ps1) | Проверка архитектурных контрактов проекта. |

---

## 3. Архитектурные принципы и важные детали реализации

### 3.1. Жизненный цикл DLL и потокобезопасность
- **Закрепление модуля:** При старте DLL вызывается `GetModuleHandleExA(..., GET_MODULE_HANDLE_EX_FLAG_PIN)`, чтобы предотвратить случайную выгрузку DLL и порчу vtable движка.
- **Никаких деструктивных действий в `DllMain(DLL_PROCESS_DETACH)`:** Из-за Loader Lock в Windows в `DllMain` только выставляется флаг завершения `g_bArtGuiTerminating` и останавливается запись. Снятие хуков D3D / MinHook при выходе из игры не выполняется внутри `DllMain`.

### 3.2. Рендеринг и проходы захвата (Multi-Pass Engine)
- Все проходы рендерятся последовательно в рамках одного кадра `View_Render`:
  1. `normal` — рендер через `g_pClient->RenderViewEx` со стандартными флагами.
  2. `viewmodel` — `RenderViewEx` с `RENDERVIEW_DRAWVIEWMODEL`, цвет очистки выставлен в `g_nViewmodelBackground*`.
  3. `players` — активируется `PlayerRenderFilterScope` (в `DrawModelEx` рисуются только сущности игроков `CCSPlayer` / `CCSRagdoll`, а вся геометрия карты становится z-оклюдером без наложения текстур).
  4. `objectid` — плоская цветовая сегментация мира, скайбокса, рук и игроков.
  5. `depth` — туман на расстоянии `art_depth_start` .. `art_depth_end`, белый цвет очистки.
  6. `clear-noplayers` — скрытие моделей игроков через `HidePlayersRenderScope`.
  7. `clear` — стандартная сцена без HUD.
- Захват кадра выполняется функцией `CaptureTga(...)`, которая читает буфер и отправляет данные в асинхронную очередь `g_pFileSystem->AsyncWrite`.

### 3.3. Память 32-битного процесса (`hl2.exe`)
- 32-битный процесс ограничен виртуальным адресным пространством ~2-4 ГБ.
- В пайплайне записи (`art_pipeline.cpp`) действует **Backpressure**:
  - `EnsureArtQueueCapacity(...)` проверяет свободную виртуальную память (`GlobalMemoryStatusEx`) и размер очереди (`max_files`, `max_mb`, `reserve_mb`).
  - При нехватке памяти принудительно вызывается `g_pFileSystem->AsyncFinishAllWrites()`, чтобы освободить буферы перед выделением новых.

### 3.4. Управление FOV
- В CS:S v34 стандартный горизонтальный FOV задается для пропорции 4:3.
- На широкоформатных экранах (16:9, 16:10) движок автоматически масштабирует угол обзора.
- Функции `art::logic::CalculateWidescreenHorizontalFov` и `CalculateVerticalFov` в [`art_logic.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_logic.cpp) обеспечивают правильную трансляцию FOV в манифест `take.json` для точного совпадения камеры в After Effects.

---

## 4. Гайд по внесению изменений (Checklist для агента)

1. **Добавление новой консольной команды:**
   - Объявите `ConCommand` в [`dll/art_commands.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_commands.cpp).
   - Зарегистрируйте в `RegisterArtCommands()`.
   - Обновите `PrintArtHelp()` и документацию [`docs/COMMANDS.md`](file:///d:/RealDump/trashhsh/dev/V34-ART/docs/COMMANDS.md).

2. **Изменение / добавление элементов GUI:**
   - Найдите соответствующую вкладку `Draw*Page()` в [`dll/art_gui.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_gui.cpp) (строки ~6000+).
   - Используйте стандартные обертки стилей ImGui, не создавайте прямых блокирующих вызовов в потоке D3D.

3. **Изменение логики записи или формата манифеста:**
   - Обновите структуру статистики в [`dll/art_internal.h`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_internal.h).
   - Обновите генерацию JSON в [`dll/art_statistics.cpp`](file:///d:/RealDump/trashhsh/dev/V34-ART/dll/art_statistics.cpp).
   - Синхронно обновите парсер манифеста в [`tools/after_effects/ART_Importer_v1.0.jsx`](file:///d:/RealDump/trashhsh/dev/V34-ART/tools/after_effects/ART_Importer_v1.0.jsx).

4. **Проверка после изменений:**
   - Запустите скрипт проверки контрактов: `pwsh ./scripts/test.ps1`.
   - Соберите проект: `.\build.bat`.
