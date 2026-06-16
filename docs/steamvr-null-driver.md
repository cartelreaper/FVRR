# SteamVR + Null Driver — единственная рабочая конфигурация на сейчас

Это пошаговая инструкция того, что реально привело к рабочему входу в Roblox VR
(хоть и без полноценного управления).

## Зачем это нужно

Roblox VR не запускается, пока SteamVR не сообщит, что есть подключённый шлем.
Без реальной гарнитуры SteamVR показывает «Шлем не обнаружен», и в Roblox остаётся белый экран.
`null` driver — встроенный в SteamVR виртуальный «фейковый» шлем, которым можно обмануть эту проверку.

## Шаги

1. Найди файл настроек:
   ```
   C:\Program Files (x86)\Steam\config\steamvr.vrsettings
   ```

2. Пропиши там (объединить с существующим содержимым, не затирая остальное):
   ```json
   {
     "steamvr": {
       "activateMultipleDrivers": true,
       "requireHmd": false,
       "enableSafeMode": false
     },
     "driver_null": {
       "enable": true
     }
   }
   ```

   Через PowerShell:
   ```powershell
   $cfg = "C:\Program Files (x86)\Steam\config\steamvr.vrsettings"
   $json = Get-Content $cfg -Raw | ConvertFrom-Json
   $json.steamvr | Add-Member -NotePropertyName "enableSafeMode" -NotePropertyValue $false -Force
   $json | Add-Member -NotePropertyName "driver_null" -NotePropertyValue ([PSCustomObject]@{ enable = $true }) -Force
   $json | ConvertTo-Json -Depth 10 | Set-Content $cfg -Encoding UTF8
   ```

3. Перезапусти SteamVR. В настройках (значок гамбургера → Настройки → Включение и выключение)
   убедись, что в "Управление дополнениями" `driver_null` не заблокирован.

4. После этого SteamVR должен перестать показывать «Шлем не обнаружен», а в Roblox при входе
   в VR-режим должен появиться экран настройки игровой зоны («Давайте подготовим комнату!»)
   вместо белого экрана.

## Важный нюанс: safe mode возвращается после краха

Если SteamVR крашится (например, при попытке загрузить кастомный неподписанный драйвер),
он автоматически включает safe mode и блокирует **все** сторонние драйверы, включая
`driver_null` через `blocked_by_safe_mode: true` и уже работавшие плагины вроде `00vrinputemulator`.

После каждого такого краха нужно повторно:
```powershell
$cfg = "C:\Program Files (x86)\Steam\config\steamvr.vrsettings"
$json = Get-Content $cfg -Raw | ConvertFrom-Json
$json.steamvr.enableSafeMode = $false
# Снять blocked_by_safe_mode с нужных драйверов
$json.'driver_null'.blocked_by_safe_mode = $false
$json | ConvertTo-Json -Depth 10 | Set-Content $cfg -Encoding UTF8
```

Либо проще — открыть настройки SteamVR → «Управление дополнениями» → нажать «Вкл.»
на нужных драйверах вручную.

## Известное ограничение

Этот null driver **не получает данные от `fakevr_companion.exe`** — он просто всегда
отдаёт нулевую/фиксированную позицию. Поэтому голова и руки в игре не двигаются от мыши,
работает только локомоция через стрелки (если её реализация на стороне Roblox её принимает
независимо от HMD tracking).

Чтобы это исправить, нужно либо:
- заменить сам `driver_null.dll` собственной версией, которая читает shared memory
  (пробовали — крашит `vrserver.exe`, см. главный README, раздел "Возможные следующие шаги"),
- либо обойти SteamVR полностью через собственный OpenXR Runtime, если выяснится,
  что у Roblox есть путь активации именно через OpenXR.
