# NovaForge в Windows

## Где что лежит

| Что | Папка | Кто управляет |
|---|---|---|
| Программа (`nova.exe`, документация, примеры, деинсталлятор) | `%LOCALAPPDATA%\Programs\NovaForge` — установка для себя (по умолчанию)<br>`C:\Program Files\NovaForge` — для всех пользователей | установщик |
| Настройки, ключи API, сессии, история, воркфлоу | `%APPDATA%\NovaForge` | `nova.exe` |
| Проекты | `Документы\NovaForge` (папка выбирается в установщике) | вы |

Программа, данные и проекты разделены: обновление переустанавливает только папку программы, удаление не трогает проекты, а настройки удаляются лишь по вашему согласию.

```
%LOCALAPPDATA%\Programs\NovaForge\
  nova.exe
  unins000.exe
  docs\        README.md  CHANGELOG.md  LICENSE  WINDOWS.md
  examples\    novaforge.json  workflows\*.json

%APPDATA%\NovaForge\
  config.json  auth.json  history  NOVA.md (общие инструкции, необязательно)
  sessions\  workflows\  runs\  tmp\

Документы\NovaForge\
  Инструкция.md
  default project\
    NOVA.md          ← инструкции проекта для ассистента
  my-app\
    NOVA.md
    ...
```

Как `nova.exe` находит папки:

- **Данные** — `%NOVA_HOME%`, если задана; иначе `%USERPROFILE%\.novaforge`, если осталась от старых версий; иначе `%APPDATA%\NovaForge`.
- **Проекты** — `projects_dir` из `config.json`; иначе папка, выбранная в установщике (`HKCU\Software\NovaForge\ProjectsDir`); иначе `Документы\NovaForge`. «Документы» берутся через системный API, поэтому учитываются перенос на другой диск и OneDrive.

## Инструкции для ассистента

Ассистент читает инструкции при каждом запросе, сверху вниз:

| Уровень | Файл | Для чего | Как открыть |
|---|---|---|---|
| Проект | `NOVA.md` в папке проекта (или `AGENTS.md`, `CLAUDE.md`) | стек, команды сборки и тестов, правила этого кода | `/instructions edit` в чате, `/init` — заполнить автоматически |
| Общие | `%APPDATA%\NovaForge\NOVA.md` | ваши предпочтения во всех проектах: язык, стиль, ОС | `/instructions global`, «Настройки → Общие инструкции», ярлык «Общие инструкции NovaForge» в «Пуске» |
| Дополнительные | любые файлы из `"instructions"` в `config.json` | общий стайлгайд, документация команды | `nova config set instructions "[\"C:/docs/style.md\"]"` |

`/instructions` в чате показывает, какие файлы ассистент читает прямо сейчас и заполнены ли они.

Новый проект создаётся с шаблоном `NOVA.md` (разделы «О проекте», «Команды», «Правила для ассистента», «Заметки»). Существующие папки не меняются. Отключить шаблон: `"project_instructions": false`.

Пример `NOVA.md`:

```markdown
# my-app

## О проекте
- Что это: Telegram-бот для записи к врачу
- Стек: Python 3.12, aiogram 3, PostgreSQL

## Команды
- Запуск: `python -m bot`
- Тесты: `pytest -q`

## Правила для ассистента
- Не трогай миграции в `db/migrations/`
- Новые обработчики — в `bot/handlers/`, по одному файлу на команду
```

## Заставка

- `"intro_mode": "always"` — ковка логотипа при каждом запуске (по умолчанию); `"6h"` — не чаще раза в 6 часов.
- `"intro": false` — без заставки; `nova --no-intro` — один раз без неё.
- `"animation_speed"` — темп: `0.7` медленнее, `1` обычный, `1.5` быстрее. Любая клавиша пропускает анимацию.
- В меню: «Настройки → Заставка при запуске» и «Скорость анимации».

## Сборка nova.exe

Нужны CMake 3.16+ и Visual Studio 2019/2022 с компонентом «Разработка классических приложений на C++» (или MinGW-w64).

```powershell
cmake -B build -A x64
cmake --build build --config Release        # → build\Release\nova.exe
```

`nova.exe` собирается со статическим рантаймом (VC++ Redistributable не нужен), с иконкой и сведениями о версии.

## Сборка установщика

Установщик — Inno Setup 6.7+ (тёмная тема «Кузница», фоновое изображение, свои страницы настроек). Два способа собрать:

**В Windows**

```powershell
winget install JRSoftware.InnoSetup
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1     # nova.exe (CMake + MSVC) → установщик
```

Параметры: `-SkipBuild` — только установщик из готового `build\Release\nova.exe`; `-Exe путь\nova.exe` — упаковать любой exe.

**В Linux (без Windows)**

```bash
scripts/build-windows-cross.sh        # llvm-mingw → nova.exe, Inno Setup под портативным Wine → установщик
```

Инструменты скачиваются один раз в `~/.cache/novaforge-tools` (≈ 600 МБ). Результат в обоих случаях — `dist/NovaForge-Setup-<версия>-x64.exe` и `.sha256`.

### Страницы установщика

1. **Режим** — для себя (без прав администратора, `%LOCALAPPDATA%\Programs`) или для всех (`Program Files`).
2. **Приветствие** и **лицензия**.
3. **Папка программы**.
4. **Компоненты** — полная / минимальная / выборочная: программа, документация, примеры воркфлоу.
5. **Папка проектов** — по умолчанию `Документы\NovaForge`.
6. **Настройки** (только при обновлении) — оставить текущие или настроить заново.
7. **Внешний вид** — тема с живым превью, заставка, скорость анимации, набор символов.
8. **Ассистент** — разрешения на правки файлов, команды и загрузку веб-страниц, лимит шагов, проект по умолчанию, шаблон `NOVA.md`.
9. **Подключение модели** — провайдер, API-ключ (скрытый ввод), ссылка «где взять ключ».
10. **Интеграция** — PATH, профиль Windows Terminal со схемой «Кузница», «Открыть NovaForge здесь» в Проводнике, ярлык на рабочем столе.
11. **Готово к установке** — сводка всех выбранных параметров.
12. **Завершение** — запустить NovaForge, открыть папку проектов.

Выбранные настройки записываются в `%APPDATA%\NovaForge\installer-settings.json`; при следующем запуске `nova.exe` переносит их в `config.json` (ключ — в `auth.json`) и удаляет файл. При обновлении установщик помнит прошлый выбор.

Удаление убирает программу, PATH, пункты Проводника, профиль Windows Terminal и ярлыки; спрашивает, удалить ли `%APPDATA%\NovaForge` (по умолчанию — нет); проекты не трогает никогда.

### Оформление

Вся графика генерируется скриптом `installer/windows/make-assets.py` (нужен Pillow): фон страниц `back-*.png`, большая картинка `wizard-*.bmp`, значок `small-*.bmp`, превью тем `theme-*.bmp`, иконка `novaforge.ico` (скопируйте её в `res/` для `nova.exe`). Поменяйте цвета или текст в скрипте и перезапустите его.

Новая версия: поменяйте `VERSION` в `src/config.hpp`, `project(... VERSION ...)` в `CMakeLists.txt` и `NOVA_VER` в `res/nova.rc`. `AppId` в `.iss` не меняйте — по нему находится установленная копия.

### Подпись (по желанию)

Без цифровой подписи SmartScreen покажет «Неизвестный издатель». С сертификатом подпишите `nova.exe` и установщик:

```powershell
signtool sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 /a build\Release\nova.exe
signtool sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 /a dist\NovaForge-Setup-1.5.0-x64.exe
```
