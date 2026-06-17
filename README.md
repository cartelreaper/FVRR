# FakeVR

Эмулятор VR-гарнитуры и контроллеров для **Roblox VR без реальной гарнитуры**, на Windows 10/11 x64.
Идея: подменить tracking-данные мышью/клавиатурой, чтобы зайти в VR-режим Roblox без HMD.

> ⚠️ **Статус: experimental / work in progress.** Заход в Roblox VR работает, локомоция стрелками работает.
> Полноценное управление головой/руками мышью пока **не работает стабильно** — подробности в разделе [Известные проблемы](#известные-проблемы).
> Если будет время и интерес — буду доводить до нормального состояния.

## Что внутри

В процессе экспериментов выяснилось, что Roblox VR **не использует OpenXR** напрямую, а ходит через **SteamVR / OpenVR**, и SteamVR агрессивно блокирует неподписанные кастомные драйверы через safe mode. Поэтому в репозитории несколько подходов, опробованных по очереди:

| Папка | Подход | Статус |
|---|---|---|
| `build/companion` | `fakevr_companion.exe` — читает мышь/клавиатуру, пишет позиции в shared memory | ✅ Работает само по себе |
| `build/openxr-layer` | OpenXR API Layer (`fakevr_layer.dll`) — перехватывает OpenXR вызовы | ❌ Бесполезно: Roblox не вызывает OpenXR |
| `build/openvr-driver` | Кастомный OpenVR драйвер (`driver_fakevr2.dll`) для SteamVR | ❌ Блокируется SteamVR safe mode при крахе |
| `build/openxr-runtime` | Полноценный OpenXR Runtime (`fakevr_runtime.dll`), замена SteamVR целиком | 🟡 Не до конца протестирован |

Рабочая на данный момент конфигурация — **SteamVR + null driver** (включён через настройки SteamVR, `enableSafeMode: false`). Roblox видит "шлем", заходит в VR без белого экрана, но позиция головы/рук не подключена к null driver, поэтому управление ограничено локомоцией со стрелок.

## Структура

```
fakevr/
├── src/                        # исходники .cpp / .h
│   ├── shared_mem.h            # общая структура данных (companion ↔ слой/драйвер/runtime)
│   ├── fakevr_companion.cpp    # GUI: читает мышь/клавиатуру, пишет в shared memory
│   ├── fakevr_layer.cpp        # OpenXR API Layer (не сработал для Roblox)
│   └── driver_fakevr2.cpp      # OpenVR Driver (блокируется SteamVR)
├── include/                    # (OpenXR SDK, openvr_driver.h)
└── build/                      # уже скомпилированные .dll/.exe + манифесты + установщики
    ├── companion/
    ├── openxr-layer/
    ├── openvr-driver/
    └── openxr-runtime/
```

## Быстрый старт (рабочий вариант: SteamVR + null driver)

1. Установи SteamVR, отключи safe mode и включи null driver — см. [`docs/steamvr-null-driver.md`](docs/steamvr-null-driver.md)
2. Запусти `build/companion/fakevr_companion.exe`
3. Запусти SteamVR
4. Запусти Roblox, включи VR-режим
5. Управление: `F1/F2/F3` — режим (голова/левая рука/правая рука), `F4` — захват мыши, `WASD+QE` — двигать руку, стрелки — ходьба, ЛКМ/ПКМ — trigger/grip

## Известные проблемы

- **Голова и руки не двигаются от мыши в игре.** Roblox берёт tracking прямо из null driver SteamVR, который всегда отдаёт нулевую позицию. Наш `fakevr_companion` пишет данные в shared memory, но ничего их оттуда не читает на стороне SteamVR.
- **Кастомный OpenVR драйвер крашит SteamVR** при загрузке (`driver_fakevr2.dll`) — после краха SteamVR включает safe mode и блокирует все неподписанные драйверы, включая ранее работавшие (`00vrinputemulator`).
- **OpenXR слой не подключается**, так как Roblox использует OpenVR, не OpenXR — подтверждено инспекцией загруженных модулей процесса (`Get-Process | Modules`), там нет ни `openxr_loader.dll`, ни `openvr_api.dll` до входа в VR-режим.
- **OpenXR Runtime** (`fakevr_runtime.dll`) — собран в отдельной сессии, теоретически должен заменить SteamVR полностью и обойти проблему блокировки драйверов, но **не протестирован до конца** в этом репозитории.

## Возможные следующие шаги

- Доделать и протестировать `fakevr_runtime.dll` как полную замену SteamVR (регистрация через `HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime`) — раз Roblox всё-таки умеет OpenXR, если SteamVR вообще убрать из цепочки
- Попробовать заменить `driver_null.dll` (подписанный встроенный драйвер SteamVR) собственной версией — оказалось, что подмена крашит `vrserver.exe`; нужно разобраться почему именно крашится (скорее всего несовпадение версии интерфейса `IVRServerDriverHost`)
- Поискать способ безопасно отключить safe mode SteamVR навсегда, а не только до следующего краха
- Изучить протокол `00vrinputemulator` (named pipe `\\.\pipe\vrinputemulator`) как обходной путь — он не блокируется и уже умеет подменять позиции устройств

## Сборка из исходников

Компилятор: `x86_64-w64-mingw32-g++` (MinGW-w64).

```bash
# Companion app
x86_64-w64-mingw32-g++ -o fakevr_companion.exe src/fakevr_companion.cpp \
  -Iinclude -std=c++17 -DUNICODE -D_UNICODE -O2 \
  -lkernel32 -luser32 -lgdi32 -mwindows -static-libgcc -static-libstdc++

# OpenXR API Layer
x86_64-w64-mingw32-g++ -shared -o fakevr_layer.dll src/fakevr_layer.cpp \
  -Iinclude -std=c++17 -O2 -lkernel32 -luser32 -static-libgcc -static-libstdc++

# OpenVR Driver
x86_64-w64-mingw32-g++ -shared -o driver_fakevr2.dll src/driver_fakevr2.cpp \
  -Iinclude -std=c++17 -O2 -lkernel32 -luser32 -static-libgcc -static-libstdc++
```

## Лицензия

MIT 
