; NovaForge — Windows x64 installer (Inno Setup 6.7+).
; Build:  scripts\build-windows.ps1
;   or:   ISCC.exe /DAppVersion=1.5.1 /DNovaExe=..\..\build\Release\nova.exe installer\windows\novaforge.iss
; Artwork: installer\windows\make-assets.py regenerates everything in assets\.
;
; Wizard: Welcome → License → Folder → Components → Projects folder → (upgrade: keep settings?)
;         → Appearance → Assistant → Model & API key → Integration → Ready → Install → Finish
;
; What goes where:
;   {app}                 program: nova.exe, docs, examples, uninstaller
;                         per user (default):  %LOCALAPPDATA%\Programs\NovaForge
;                         all users:           C:\Program Files\NovaForge
;   %APPDATA%\NovaForge   settings, API keys, sessions (managed by nova.exe)
;   Documents\NovaForge   projects — chosen on a wizard page, never removed by the uninstaller
;
; Settings chosen in the wizard are written to %APPDATA%\NovaForge\installer-settings.json;
; nova.exe applies them on its next start (config.json + auth.json) and deletes the file.

#ifndef AppVersion
  #define AppVersion "1.5.1"
#endif
#ifndef NovaExe
  #define NovaExe "..\..\build\Release\nova.exe"
#endif
#define AppName      "NovaForge"
#define AppPublisher "NovaForge"
#define AppURL       "https://novaforge.dev"
#define AppIdGuid    "6E0F1C1A-6B5D-4C55-9A0E-4E6F7661F0A1"

[Setup]
; keep AppId forever: upgrades and the uninstaller find the installation by it
AppId={{{#AppIdGuid}}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppCopyright=© 2026 NovaForge. MIT License.
VersionInfoVersion={#AppVersion}.0
VersionInfoDescription={#AppName} Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
; per-user install by default (no admin rights); the first dialog offers "for all users"
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableWelcomePage=no
DisableDirPage=no
UsePreviousAppDir=yes
UsePreviousTasks=yes
UsePreviousSetupType=yes
UsedUserAreasWarning=no
ChangesEnvironment=yes
CloseApplications=yes
RestartApplications=no
ShowLanguageDialog=no
; ---- look: dark forge style ----
WizardStyle=modern dark includetitlebar hidebevels
WizardSizePercent=120
WizardBackImageFile=assets\back-596.png,assets\back-796.png,assets\back-994.png,assets\back-1272.png
WizardBackColor=#120806
WizardImageFile=assets\wizard-164.bmp,assets\wizard-192.bmp,assets\wizard-246.bmp,assets\wizard-273.bmp,assets\wizard-328.bmp,assets\wizard-355.bmp,assets\wizard-410.bmp
WizardImageBackColor=#120806
WizardSmallImageFile=assets\small-55.bmp,assets\small-64.bmp,assets\small-69.bmp,assets\small-83.bmp,assets\small-92.bmp,assets\small-110.bmp,assets\small-119.bmp,assets\small-138.bmp
WizardSmallImageBackColor=#120806
SetupIconFile=assets\novaforge.ico
UninstallDisplayIcon={app}\nova.exe
UninstallDisplayName={#AppName} {#AppVersion}
LicenseFile=..\..\LICENSE
; ---- output ----
OutputDir=..\..\dist
OutputBaseFilename=NovaForge-Setup-{#AppVersion}-x64
Compression=lzma2/ultra64
SolidCompression=yes

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[Messages]
WelcomeLabel1=Добро пожаловать в NovaForge
WelcomeLabel2=AI-ассистент для разработки и автоматизации прямо в терминале.%n%n•  читает и правит код, запускает команды и тесты%n•  OpenAI, Anthropic, Gemini и локальные модели%n•  помнит правила проекта в NOVA.md%n%nМастер установит NovaForge {#AppVersion} и поможет настроить внешний вид, поведение ассистента и подключение к модели. Все параметры можно изменить позже в самой программе.%n%nПроекты хранятся отдельно от программы и никогда не удаляются деинсталлятором.
StatusExtractFiles=Кую файлы…
WizardInstalling=Установка
InstallingLabel=NovaForge устанавливается — это займёт несколько секунд.
FinishedHeadingLabel=NovaForge готов к работе
FinishedLabel=Установка завершена.%n%nЗапустите NovaForge из меню «Пуск», из Windows Terminal или командой nova в любом терминале.
FinishedLabelNoIcons=Установка завершена. Запустите NovaForge командой nova в терминале.
SelectComponentsDesc=Какие части NovaForge установить?
WizardSelectTasks=Интеграция с Windows
SelectTasksDesc=Как встроить NovaForge в Windows?
SelectTasksLabel2=Выберите дополнительные возможности и нажмите «Далее».

[Types]
Name: "full";    Description: "Полная установка (рекомендуется)"
Name: "compact"; Description: "Минимальная — только nova.exe"
Name: "custom";  Description: "Выборочная"; Flags: iscustom

[Components]
Name: "main";     Description: "NovaForge — программа (nova.exe)";                     Types: full compact custom; Flags: fixed
Name: "docs";     Description: "Документация: README, руководство для Windows, история версий"; Types: full
Name: "examples"; Description: "Примеры конфигурации и воркфлоу автоматизации";         Types: full

[Tasks]
Name: "path";        Description: "Добавить nova в PATH — запуск командой nova из любого терминала"; GroupDescription: "Командная строка:"
Name: "wtprofile";   Description: "Профиль NovaForge в Windows Terminal (с цветовой схемой «Кузница»)"; GroupDescription: "Командная строка:"
Name: "contextmenu"; Description: "«Открыть NovaForge здесь» в контекстном меню папок";              GroupDescription: "Проводник:"
Name: "desktopicon"; Description: "Ярлык на рабочем столе";                                         GroupDescription: "Ярлыки:"; Flags: unchecked

[Files]
Source: "{#NovaExe}";               DestDir: "{app}";          Components: main;     Flags: ignoreversion
Source: "..\..\README.md";          DestDir: "{app}\docs";     Components: docs;     Flags: ignoreversion
Source: "..\..\CHANGELOG.md";       DestDir: "{app}\docs";     Components: docs;     Flags: ignoreversion
Source: "..\..\docs\*";             DestDir: "{app}\docs";     Components: docs;     Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "..\..\LICENSE";            DestDir: "{app}";          Components: main;     Flags: ignoreversion
Source: "..\..\examples\*";         DestDir: "{app}\examples"; Components: examples; Flags: ignoreversion recursesubdirs
; theme previews for the Appearance page (only extracted to {tmp})
Source: "assets\theme-*.bmp";       Flags: dontcopy
; finish page artwork (swapped in at runtime, multi-DPI like WizardImageFile)
Source: "assets\finish-*.bmp";      Flags: dontcopy

[Dirs]
Name: "{code:GetProjectsDir}"; Flags: uninsneveruninstall

[Icons]
Name: "{group}\{#AppName}";                Filename: "{app}\nova.exe"; WorkingDir: "{code:GetProjectsDir}"; IconFilename: "{app}\nova.exe"; Comment: "AI-ассистент для кода в терминале"
Name: "{group}\Проекты NovaForge";         Filename: "{code:GetProjectsDir}"
Name: "{group}\Руководство NovaForge";     Filename: "{app}\docs\WINDOWS.md"; Components: docs
Name: "{group}\Общие инструкции NovaForge"; Filename: "{sys}\notepad.exe"; Parameters: """{userappdata}\NovaForge\NOVA.md"""; Comment: "Правила для ассистента во всех проектах"
Name: "{group}\Удалить NovaForge";         Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";          Filename: "{app}\nova.exe"; WorkingDir: "{code:GetProjectsDir}"; IconFilename: "{app}\nova.exe"; Tasks: desktopicon

[Registry]
; read by nova.exe: projects folder chosen on the wizard page (config "projects_dir" still wins)
Root: HKCU; Subkey: "Software\NovaForge"; ValueType: string; ValueName: "ProjectsDir"; ValueData: "{code:GetProjectsDir}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\NovaForge"; ValueType: string; ValueName: "InstallDir";  ValueData: "{app}"
Root: HKCU; Subkey: "Software\NovaForge"; ValueType: string; ValueName: "Version";     ValueData: "{#AppVersion}"
; Explorer: right click on a folder, or on empty space inside a folder
Root: HKA; Subkey: "Software\Classes\Directory\shell\NovaForge";                    ValueType: string; ValueName: "";     ValueData: "Открыть NovaForge здесь"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\NovaForge";                    ValueType: string; ValueName: "Icon"; ValueData: "{app}\nova.exe";         Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\NovaForge\command";            ValueType: string; ValueName: "";     ValueData: """{app}\nova.exe"" --no-menu --cwd ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\NovaForge";         ValueType: string; ValueName: "";     ValueData: "Открыть NovaForge здесь"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\NovaForge";         ValueType: string; ValueName: "Icon"; ValueData: "{app}\nova.exe";         Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\NovaForge\command"; ValueType: string; ValueName: "";     ValueData: """{app}\nova.exe"" --no-menu --cwd ""%V"""; Tasks: contextmenu

[Run]
Filename: "{app}\nova.exe"; WorkingDir: "{code:GetProjectsDir}"; Description: "Запустить NovaForge"; Flags: postinstall nowait skipifsilent
Filename: "{code:GetProjectsDir}"; Description: "Открыть папку проектов"; Flags: postinstall shellexec nowait skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{app}\docs"
Type: filesandordirs; Name: "{app}\examples"
Type: filesandordirs; Name: "{code:WTFragmentDir}"

[Code]
const
  ThemeIds     = 'forge|amber|phosphor|steel|default|mono';
  ThemeNames   = 'Кузница — раскалённый металл|Янтарный монитор|Зелёный люминофор|Сталь|Цвета терминала|Без цвета';
  IntroIds     = 'always|6h|off';
  IntroNames   = 'При каждом запуске|Раз в 6 часов|Выключена';
  SpeedValues  = '0.7|1|1.5|0';
  SpeedNames   = 'Медленно — плавная заставка|Обычно (рекомендуется)|Быстро|Без анимации';
  GlyphIds     = 'auto|rich|safe';
  GlyphNames   = 'Автоматически|Полный набор (Windows Terminal)|Совместимый (классическая консоль)';
  PermIds      = 'ask|allow|deny';
  PermNames    = 'Спрашивать каждый раз|Разрешать без вопросов|Запретить';
  WebIds       = 'allow|ask|deny';
  WebNames     = 'Разрешать|Спрашивать каждый раз|Запретить';
  StepIds      = '20|40|80|150';
  StepNames    = '20 — коротко и экономно|40 — рекомендуется|80 — сложные задачи|150 — без ограничений';
  ProvIds      = '|openai|anthropic|google|openrouter|deepseek|groq|mistral|xai|together';
  ProvNames    = 'Настроить позже (nova connect)|OpenAI|Anthropic|Google Gemini|OpenRouter|DeepSeek|Groq|Mistral|xAI|Together AI';
  ClrAccent    = $0047B3FF;  { #FFB347 }
  ClrMuted     = $008F9AA8;  { #A89A8F }
  ClrOk        = $0069C09C;  { #9CC069 }
  FinishSizes  = '164|192|246|273|328|355|410';
  Tips         = 'Shift+Tab в чате переключает режим работы ассистента.|' +
                 '/init изучит проект и заполнит NOVA.md: стек, команды сборки, правила кода.|' +
                 '@ в запросе — выбрать файл проекта, ! — выполнить команду shell.|' +
                 '/undo откатит последний ход вместе с изменениями файлов.|' +
                 'Ollama и LM Studio находятся автоматически — без ключей и настроек.|' +
                 'Правой кнопкой по папке → «Открыть NovaForge здесь» — и ассистент уже в проекте.';
  ProvUrls     = '|https://platform.openai.com/api-keys|https://console.anthropic.com/settings/keys|https://aistudio.google.com/apikey|https://openrouter.ai/keys|https://platform.deepseek.com/api_keys|https://console.groq.com/keys|https://console.mistral.ai/api-keys|https://console.x.ai|https://api.together.ai/settings/api-keys';

var
  ProjectsPage: TInputDirWizardPage;
  KeepPage: TInputOptionWizardPage;
  LookPage, AssistPage, ModelPage: TWizardPage;
  ThemeCombo, SpeedCombo, GlyphCombo: TNewComboBox;
  IntroCombo: TNewComboBox;
  Preview: TBitmapImage;
  EditCombo, BashCombo, WebCombo, StepsCombo: TNewComboBox;
  TemplateCheck: TNewCheckBox;
  DefProjectEdit: TNewEdit;
  ProvCombo: TNewComboBox;
  KeyEdit: TPasswordEdit;
  KeyPlain: TNewEdit;
  ShowKeyCheck: TNewCheckBox;
  StepLabel, StepDots, TipLabel, FirstSteps, FirstStepsTitle: TNewStaticText;
  LastTip: Integer;
  KeyLink, KeyHint: TNewStaticText;
  HadSettings: Boolean;

{ ---------- small helpers ---------- }
function Part(List: String; Index: Integer): String;
var
  I, P: Integer;
begin
  Result := '';
  for I := 0 to Index do
  begin
    P := Pos('|', List);
    if P = 0 then begin Result := List; List := ''; end
    else begin Result := Copy(List, 1, P - 1); Delete(List, 1, P); end;
    if (I < Index) and (P = 0) then begin Result := ''; Exit; end;
  end;
end;

function IndexOfPart(List, Value: String): Integer;
var
  I: Integer;
begin
  Result := -1;
  for I := 0 to 30 do
    if Part(List, I) = Value then begin Result := I; Exit; end
    else if (Part(List, I) = '') and (Pos('|', List) = 0) then Exit;
end;

procedure FillCombo(C: TNewComboBox; Names: String);
var
  P: Integer;
begin
  C.Items.Clear;
  while Names <> '' do
  begin
    P := Pos('|', Names);
    if P = 0 then begin C.Items.Add(Names); Names := ''; end
    else begin C.Items.Add(Copy(Names, 1, P - 1)); Delete(Names, 1, P); end;
  end;
end;

function AddLabel(Page: TWizardPage; Caption: String; Left, Top, Width: Integer): TNewStaticText;
begin
  Result := TNewStaticText.Create(Page);
  Result.Parent := Page.Surface;
  Result.Caption := Caption;
  Result.Left := Left;
  Result.Top := Top;
  Result.Width := Width;
  Result.AutoSize := False;
  Result.WordWrap := True;
  Result.Height := ScaleY(16);
end;

function AddField(Page: TWizardPage; Caption: String; Left, Top, Width: Integer): TNewStaticText;
begin
  Result := AddLabel(Page, Caption, Left, Top, Width);
  Result.Font.Style := [fsBold];
  Result.Font.Color := ClrAccent;
  Result.StyleElements := [seClient, seBorder];
end;

function AddHint(Page: TWizardPage; Caption: String; Left, Top, Width, Lines: Integer): TNewStaticText;
begin
  Result := AddLabel(Page, Caption, Left, Top, Width);
  Result.Font.Color := ClrMuted;
  Result.StyleElements := [seClient, seBorder];
  Result.Height := ScaleY(15) * Lines + ScaleY(2);
end;

function KeyText: String;
begin
  if ShowKeyCheck.Checked then Result := Trim(KeyPlain.Text) else Result := Trim(KeyEdit.Text);
end;

function AddCombo(Page: TWizardPage; Names, Prev: String; Ids: String; Def, Left, Top, Width: Integer): TNewComboBox;
var
  I: Integer;
begin
  Result := TNewComboBox.Create(Page);
  Result.Parent := Page.Surface;
  Result.Style := csDropDownList;
  Result.Left := Left;
  Result.Top := Top;
  Result.Width := Width;
  FillCombo(Result, Names);
  I := IndexOfPart(Ids, Prev);
  if (Prev = '') or (I < 0) then I := Def;
  Result.ItemIndex := I;
end;

function AddCheck(Page: TWizardPage; Caption: String; Checked: Boolean; Left, Top, Width: Integer): TNewCheckBox;
begin
  Result := TNewCheckBox.Create(Page);
  Result.Parent := Page.Surface;
  Result.Caption := Caption;
  Result.Checked := Checked;
  Result.Left := Left;
  Result.Top := Top;
  Result.Width := Width;
  Result.Height := ScaleY(18);
end;

function JsonEsc(S: String): String;
begin
  Result := S;
  StringChangeEx(Result, '\', '\\', True);
  StringChangeEx(Result, '"', '\"', True);
  StringChangeEx(Result, #13, '', True);
  StringChangeEx(Result, #10, '', True);
  StringChangeEx(Result, #9, ' ', True);
end;

function JsonBool(B: Boolean): String;
begin
  if B then Result := 'true' else Result := 'false';
end;

{ ---------- event handlers ---------- }
procedure UpdatePreview;
var
  F: String;
begin
  F := 'theme-' + Part(ThemeIds, ThemeCombo.ItemIndex) + '.bmp';
  if not FileExists(ExpandConstant('{tmp}\' + F)) then ExtractTemporaryFile(F);
  Preview.Bitmap.LoadFromFile(ExpandConstant('{tmp}\' + F));
end;

procedure ThemeChange(Sender: TObject);
begin
  UpdatePreview;
end;

procedure IntroChange(Sender: TObject);
begin
  SpeedCombo.Enabled := IntroCombo.ItemIndex <> 2;
end;

function IntroOn: Boolean;
begin
  Result := IntroCombo.ItemIndex <> 2;
end;

procedure ProviderChange(Sender: TObject);
var
  Url: String;
begin
  Url := Part(ProvUrls, ProvCombo.ItemIndex);
  KeyEdit.Enabled := Url <> '';
  KeyPlain.Enabled := Url <> '';
  ShowKeyCheck.Enabled := Url <> '';
  KeyLink.Visible := Url <> '';
  if Url <> '' then KeyLink.Caption := 'Где взять ключ: ' + Url;
end;

procedure KeyLinkClick(Sender: TObject);
var
  Code: Integer;
  Url: String;
begin
  Url := Part(ProvUrls, ProvCombo.ItemIndex);
  if Url <> '' then ShellExecAsOriginalUser('open', Url, '', '', SW_SHOWNORMAL, ewNoWait, Code);
end;

procedure ShowKeyClick(Sender: TObject);
begin
  if ShowKeyCheck.Checked then KeyPlain.Text := KeyEdit.Text else KeyEdit.Text := KeyPlain.Text;
  KeyPlain.Visible := ShowKeyCheck.Checked;
  KeyEdit.Visible := not ShowKeyCheck.Checked;
end;

{ ---------- wizard pages ---------- }
procedure CreateLookPage;
var
  W, Col, Y: Integer;
begin
  LookPage := CreateCustomPage(KeepPage.ID, 'Внешний вид', 'Тема, заставка и символы интерфейса в терминале');
  W := LookPage.SurfaceWidth;
  Col := W * 45 div 100;
  Y := 0;
  AddField(LookPage, 'Цветовая тема', 0, Y, Col);
  ThemeCombo := AddCombo(LookPage, ThemeNames, GetPreviousData('Theme', ''), ThemeIds, 0, 0, Y + ScaleY(18), Col);
  ThemeCombo.OnChange := @ThemeChange;
  Y := Y + ScaleY(50);
  AddField(LookPage, 'Заставка с ковкой логотипа', 0, Y, Col);
  IntroCombo := AddCombo(LookPage, IntroNames, GetPreviousData('IntroMode', ''), IntroIds, 0, 0, Y + ScaleY(18), Col);
  IntroCombo.OnChange := @IntroChange;
  Y := Y + ScaleY(50);
  AddField(LookPage, 'Скорость анимации', 0, Y, Col);
  SpeedCombo := AddCombo(LookPage, SpeedNames, GetPreviousData('Speed', ''), SpeedValues, 1, 0, Y + ScaleY(18), Col);
  Y := Y + ScaleY(50);
  AddField(LookPage, 'Символы интерфейса', 0, Y, Col);
  GlyphCombo := AddCombo(LookPage, GlyphNames, GetPreviousData('Glyphs', ''), GlyphIds, 0, 0, Y + ScaleY(18), Col);

  Preview := TBitmapImage.Create(LookPage);
  Preview.Parent := LookPage.Surface;
  Preview.Left := Col + ScaleX(16);
  Preview.Width := W - Preview.Left;
  Preview.Height := Preview.Width * 220 div 300;
  Preview.Top := 0;
  Preview.Stretch := True;
  AddHint(LookPage, 'Всё это меняется и после установки: «Настройки» в главном меню или /theme в чате.', 0,
    Y + ScaleY(58), W, 2);
  IntroChange(nil);
end;

procedure CreateAssistPage;
var
  W, L, Y, Row: Integer;
begin
  AssistPage := CreateCustomPage(LookPage.ID, 'Ассистент', 'Что NovaForge может делать без вашего подтверждения');
  W := AssistPage.SurfaceWidth;
  L := W * 42 div 100;
  Row := ScaleY(30);
  Y := 0;
  AddField(AssistPage, 'Правки файлов', 0, Y + ScaleY(3), L);
  EditCombo := AddCombo(AssistPage, PermNames, GetPreviousData('PermEdit', ''), PermIds, 0, L, Y, W - L);
  Y := Y + Row;
  AddField(AssistPage, 'Команды в терминале', 0, Y + ScaleY(3), L);
  BashCombo := AddCombo(AssistPage, PermNames, GetPreviousData('PermBash', ''), PermIds, 0, L, Y, W - L);
  Y := Y + Row;
  AddField(AssistPage, 'Загрузка веб-страниц', 0, Y + ScaleY(3), L);
  WebCombo := AddCombo(AssistPage, WebNames, GetPreviousData('PermWeb', ''), WebIds, 0, L, Y, W - L);
  Y := Y + Row;
  AddField(AssistPage, 'Шагов на один запрос', 0, Y + ScaleY(3), L);
  StepsCombo := AddCombo(AssistPage, StepNames, GetPreviousData('Steps', ''), StepIds, 1, L, Y, W - L);
  Y := Y + Row;
  AddField(AssistPage, 'Проект по умолчанию', 0, Y + ScaleY(3), L);
  DefProjectEdit := TNewEdit.Create(AssistPage);
  DefProjectEdit.Parent := AssistPage.Surface;
  DefProjectEdit.Left := L;
  DefProjectEdit.Top := Y;
  DefProjectEdit.Width := W - L;
  DefProjectEdit.Text := GetPreviousData('DefProject', 'default project');
  Y := Y + Row + ScaleY(4);
  TemplateCheck := AddCheck(AssistPage, 'Создавать NOVA.md с инструкциями для ассистента в новых проектах',
    GetPreviousData('Template', '1') = '1', 0, Y, W);
  Y := Y + ScaleY(28);
  with AddHint(AssistPage, 'Инструкции для ассистента: NOVA.md в папке проекта (/instructions edit) и общие для всех проектов — ' +
    'ярлык «Общие инструкции NovaForge» в меню «Пуск» (/instructions global).', 0, Y, W, 2) do
    Height := Height;
end;

procedure CreateModelPage;
var
  W, Y: Integer;
begin
  ModelPage := CreateCustomPage(AssistPage.ID, 'Подключение модели', 'Облачный API — сейчас или позже');
  W := ModelPage.SurfaceWidth;
  Y := 0;
  with AddHint(ModelPage, 'Выберите провайдера и вставьте API-ключ — после запуска NovaForge загрузит список моделей и предложит выбрать нужную. Ключ сохраняется только на этом компьютере.', 0, Y, W, 3) do
    Height := Height;
  Y := Y + ScaleY(52);
  AddField(ModelPage, 'Провайдер', 0, Y, W);
  ProvCombo := AddCombo(ModelPage, ProvNames, GetPreviousData('Provider', ''), ProvIds, 0, 0, Y + ScaleY(18), W * 60 div 100);
  ProvCombo.OnChange := @ProviderChange;
  Y := Y + ScaleY(52);
  AddField(ModelPage, 'API-ключ', 0, Y, W);
  KeyEdit := TPasswordEdit.Create(ModelPage);
  KeyEdit.Parent := ModelPage.Surface;
  KeyEdit.Left := 0;
  KeyEdit.Top := Y + ScaleY(18);
  KeyEdit.Width := W;
  KeyPlain := TNewEdit.Create(ModelPage);
  KeyPlain.Parent := ModelPage.Surface;
  KeyPlain.Left := 0;
  KeyPlain.Top := KeyEdit.Top;
  KeyPlain.Width := W;
  KeyPlain.Visible := False;
  ShowKeyCheck := AddCheck(ModelPage, 'Показать ключ', False, W - ScaleX(160), Y, ScaleX(160));
  ShowKeyCheck.OnClick := @ShowKeyClick;
  Y := Y + ScaleY(46);
  KeyLink := AddLabel(ModelPage, '', 0, Y, W);
  KeyLink.Cursor := crHand;
  KeyLink.Font.Style := [fsUnderline];
  KeyLink.OnClick := @KeyLinkClick;
  KeyLink.Font.Color := ClrAccent;
  KeyLink.StyleElements := [seClient, seBorder];
  Y := Y + ScaleY(30);
  KeyHint := AddHint(ModelPage, 'Локальные модели — Ollama, LM Studio, llama.cpp — находятся автоматически, ключ им не нужен. Подключения можно менять в любой момент: nova connect.', 0, Y, W, 2);
  ProviderChange(nil);
end;

function ConfigureSettings: Boolean;
begin
  Result := (not HadSettings) or (KeepPage.SelectedValueIndex = 1);
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if PageID = KeepPage.ID then Result := not HadSettings
  else if (PageID = LookPage.ID) or (PageID = AssistPage.ID) or (PageID = ModelPage.ID) then Result := not ConfigureSettings;
end;

{ ---------- step indicator, tips, finish page ---------- }
function PageInFlow(ID: Integer): Boolean;
begin
  Result := not ShouldSkipPage(ID);
end;

procedure UpdateSteps(CurPageID: Integer);
var
  Ids: array of Integer;
  I, N, Cur: Integer;
  Dots, Name: String;
begin
  SetArrayLength(Ids, 10);
  Ids[0] := wpLicense; Ids[1] := wpSelectDir; Ids[2] := wpSelectComponents; Ids[3] := ProjectsPage.ID;
  Ids[4] := KeepPage.ID; Ids[5] := LookPage.ID; Ids[6] := AssistPage.ID; Ids[7] := ModelPage.ID;
  Ids[8] := wpSelectTasks; Ids[9] := wpReady;
  N := 0; Cur := 0; Dots := '';
  for I := 0 to 9 do
    if PageInFlow(Ids[I]) then
    begin
      N := N + 1;
      if Ids[I] = CurPageID then Cur := N;
    end;
  for I := 1 to N do
    if I <= Cur then Dots := Dots + '● ' else Dots := Dots + '○ ';
  StepLabel.Visible := Cur > 0;
  StepDots.Visible := Cur > 0;
  if Cur = 0 then Exit;
  case CurPageID of
    wpLicense: Name := 'лицензия';
    wpSelectDir: Name := 'папка программы';
    wpSelectComponents: Name := 'компоненты';
    wpSelectTasks: Name := 'интеграция';
    wpReady: Name := 'проверка';
  else
    if CurPageID = ProjectsPage.ID then Name := 'проекты'
    else if CurPageID = KeepPage.ID then Name := 'настройки'
    else if CurPageID = LookPage.ID then Name := 'внешний вид'
    else if CurPageID = AssistPage.ID then Name := 'ассистент'
    else Name := 'модель';
  end;
  StepDots.Caption := Dots;
  StepLabel.Caption := Format('Шаг %d из %d · %s', [Cur, N, Name]);
end;

procedure ShowTip(I: Integer);
begin
  if I = LastTip then Exit;
  LastTip := I;
  TipLabel.Caption := 'Совет:  ' + Part(Tips, I mod 6);
end;

procedure CurInstallProgressChanged(CurProgress, MaxProgress: Integer);
begin
  if MaxProgress > 0 then ShowTip((CurProgress * 3) div MaxProgress);
end;

procedure LoadFinishImage;
var
  I, W, Best: Integer;
  F: String;
begin
  W := WizardForm.WizardBitmapImage2.Width;
  Best := 410;
  for I := 6 downto 0 do
    if StrToInt(Part(FinishSizes, I)) >= W then Best := StrToInt(Part(FinishSizes, I));
  F := 'finish-' + IntToStr(Best) + '.bmp';
  try
    if not FileExists(ExpandConstant('{tmp}\' + F)) then ExtractTemporaryFile(F);
    WizardForm.WizardBitmapImage2.Bitmap.LoadFromFile(ExpandConstant('{tmp}\' + F));
  except
  end;
end;

procedure LayoutFinish;
var
  L, W, Y: Integer;
begin
  L := WizardForm.FinishedLabel.Left;
  W := WizardForm.FinishedLabel.Width;
  if WizardForm.RunList.Visible then
    Y := WizardForm.RunList.Top + ScaleY(22) * WizardForm.RunList.Items.Count + ScaleY(18)
  else
    Y := WizardForm.FinishedLabel.Top + WizardForm.FinishedLabel.Height + ScaleY(18);
  FirstStepsTitle.SetBounds(L, Y, W, ScaleY(16));
  FirstSteps.SetBounds(L, Y + ScaleY(22), W, ScaleY(15) * 5);
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = LookPage.ID then UpdatePreview;
  UpdateSteps(CurPageID);
  if CurPageID = wpInstalling then ShowTip(0);
  if CurPageID = wpFinished then
  begin
    LoadFinishImage;
    LayoutFinish;
  end;
end;

procedure CreateChrome;
var
  P: TNewNotebookPage;
begin
  { step indicator: bottom-left, on the button row }
  StepDots := TNewStaticText.Create(WizardForm);
  StepDots.Parent := WizardForm;
  StepDots.Left := ScaleX(20);
  StepDots.Top := WizardForm.NextButton.Top - ScaleY(6);
  StepDots.Font.Color := ClrAccent;
  StepDots.StyleElements := [seClient, seBorder];
  StepDots.Font.Size := 11;
  StepDots.Font.Name := 'Segoe UI Symbol';
  StepLabel := TNewStaticText.Create(WizardForm);
  StepLabel.Parent := WizardForm;
  StepLabel.Left := ScaleX(20);
  StepLabel.Top := WizardForm.NextButton.Top + ScaleY(16);
  StepLabel.Font.Color := ClrMuted;
  StepLabel.StyleElements := [seClient, seBorder];

  { rotating tips under the progress bar }
  P := WizardForm.InstallingPage;
  TipLabel := TNewStaticText.Create(WizardForm);
  TipLabel.Parent := P;
  TipLabel.AutoSize := False;
  TipLabel.WordWrap := True;
  TipLabel.SetBounds(WizardForm.ProgressGauge.Left, WizardForm.ProgressGauge.Top + WizardForm.ProgressGauge.Height + ScaleY(40),
    WizardForm.ProgressGauge.Width, ScaleY(36));
  TipLabel.Font.Color := ClrAccent;
  TipLabel.StyleElements := [seClient, seBorder];
  LastTip := -1;

  { cheat sheet on the finish page }
  P := WizardForm.FinishedPage;
  FirstStepsTitle := TNewStaticText.Create(WizardForm);
  FirstStepsTitle.Parent := P;
  FirstStepsTitle.AutoSize := False;
  FirstStepsTitle.Caption := 'Первые шаги';
  FirstStepsTitle.Font.Style := [fsBold];
  FirstStepsTitle.Font.Color := ClrAccent;
  FirstStepsTitle.StyleElements := [seClient, seBorder];
  FirstSteps := TNewStaticText.Create(WizardForm);
  FirstSteps.Parent := P;
  FirstSteps.AutoSize := False;
  FirstSteps.WordWrap := False;
  FirstSteps.Font.Name := 'Consolas';
  FirstSteps.Font.Color := ClrMuted;
  FirstSteps.StyleElements := [seClient, seBorder];
  FirstSteps.Caption :=
    'nova            запустить в текущей папке' + #13#10 +
    'nova connect    подключить модель' + #13#10 +
    '/init           описать проект в NOVA.md' + #13#10 +
    '/instructions   правила для ассистента' + #13#10 +
    '/help           все команды';
end;

procedure InitializeWizard;
begin
  HadSettings := FileExists(ExpandConstant('{userappdata}\NovaForge\config.json')) or
                 FileExists(ExpandConstant('{%USERPROFILE}\.novaforge\config.json'));

  ProjectsPage := CreateInputDirPage(wpSelectComponents, 'Папка проектов', 'Где хранить ваши проекты?',
    'Программа устанавливается отдельно, а проекты — это ваши обычные папки: они остаются при обновлении и удалении NovaForge. ' +
    'В каждом новом проекте появится NOVA.md — инструкции для ассистента.' + #13#10#13#10 +
    'Папку можно поменять позже: «Настройки → Папка проектов».', False, 'NovaForge');
  ProjectsPage.Add('');
  ProjectsPage.Values[0] := GetPreviousData('ProjectsDir', ExpandConstant('{userdocs}\NovaForge'));

  KeepPage := CreateInputOptionPage(ProjectsPage.ID, 'Настройки', 'NovaForge уже настроен на этом компьютере',
    'Найдены ваши настройки, ключи и сессии. Их можно оставить как есть или пройти настройку заново ' +
    '(ключи API и история сессий в любом случае сохранятся).', True, False);
  KeepPage.Add('Оставить текущие настройки');
  KeepPage.Add('Настроить заново: внешний вид, ассистент, подключение модели');
  KeepPage.SelectedValueIndex := 0;

  CreateLookPage;
  CreateAssistPage;
  CreateModelPage;
  CreateChrome;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = ModelPage.ID) and KeyEdit.Enabled and (KeyText = '') then
  begin
    MsgBox('Вставьте API-ключ или выберите «Настроить позже».', mbError, MB_OK);
    Result := False;
  end;
  if (CurPageID = AssistPage.ID) and (Trim(DefProjectEdit.Text) = '') then
    DefProjectEdit.Text := 'default project';
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'ProjectsDir', ProjectsPage.Values[0]);
  if not ConfigureSettings then Exit;
  SetPreviousData(PreviousDataKey, 'Theme', Part(ThemeIds, ThemeCombo.ItemIndex));
  SetPreviousData(PreviousDataKey, 'IntroMode', Part(IntroIds, IntroCombo.ItemIndex));
  SetPreviousData(PreviousDataKey, 'Speed', Part(SpeedValues, SpeedCombo.ItemIndex));
  SetPreviousData(PreviousDataKey, 'Glyphs', Part(GlyphIds, GlyphCombo.ItemIndex));
  SetPreviousData(PreviousDataKey, 'PermEdit', Part(PermIds, EditCombo.ItemIndex));
  SetPreviousData(PreviousDataKey, 'PermBash', Part(PermIds, BashCombo.ItemIndex));
  SetPreviousData(PreviousDataKey, 'PermWeb', Part(WebIds, WebCombo.ItemIndex));
  SetPreviousData(PreviousDataKey, 'Steps', Part(StepIds, StepsCombo.ItemIndex));
  SetPreviousData(PreviousDataKey, 'DefProject', Trim(DefProjectEdit.Text));
  if TemplateCheck.Checked then SetPreviousData(PreviousDataKey, 'Template', '1') else SetPreviousData(PreviousDataKey, 'Template', '0');
  SetPreviousData(PreviousDataKey, 'Provider', Part(ProvIds, ProvCombo.ItemIndex));
end;

function GetProjectsDir(Param: String): String;
begin
  Result := RemoveBackslashUnlessRoot(ProjectsPage.Values[0]);
end;

function WTFragmentDir(Param: String): String;
begin
  if IsAdminInstallMode then Result := ExpandConstant('{commonappdata}\Microsoft\Windows Terminal\Fragments\NovaForge')
  else Result := ExpandConstant('{localappdata}\Microsoft\Windows Terminal\Fragments\NovaForge');
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var
  S: String;
begin
  S := MemoDirInfo + NewLine + NewLine +
       'Папка проектов:' + NewLine + Space + GetProjectsDir('') + NewLine + NewLine;
  if MemoComponentsInfo <> '' then S := S + MemoComponentsInfo + NewLine + NewLine;
  if ConfigureSettings then
  begin
    S := S + 'Внешний вид:' + NewLine +
         Space + 'Тема: ' + ThemeCombo.Text + NewLine;
    if IntroOn then S := S + Space + 'Заставка: ' + IntroCombo.Text + ', скорость: ' + SpeedCombo.Text + NewLine
    else S := S + Space + 'Заставка: выключена' + NewLine;
    S := S + Space + 'Символы: ' + GlyphCombo.Text + NewLine + NewLine +
         'Ассистент:' + NewLine +
         Space + 'Правки файлов: ' + EditCombo.Text + NewLine +
         Space + 'Команды в терминале: ' + BashCombo.Text + NewLine +
         Space + 'Веб-страницы: ' + WebCombo.Text + NewLine +
         Space + 'Шагов на запрос: ' + Part(StepIds, StepsCombo.ItemIndex) + NewLine +
         Space + 'Проект по умолчанию: ' + Trim(DefProjectEdit.Text) + NewLine + NewLine +
         'Модель:' + NewLine;
    if KeyEdit.Enabled then S := S + Space + ProvCombo.Text + ' (ключ указан)' + NewLine + NewLine
    else S := S + Space + 'настроить позже' + NewLine + NewLine;
  end
  else S := S + 'Настройки:' + NewLine + Space + 'остаются без изменений' + NewLine + NewLine;
  Result := S + MemoTasksInfo;
end;

{ ---------- PATH (user PATH for per-user installs, system PATH for all-users installs) ---------- }
function EnvRoot: Integer;
begin
  if IsAdminInstallMode then Result := HKEY_LOCAL_MACHINE else Result := HKEY_CURRENT_USER;
end;

function EnvKey: String;
begin
  if IsAdminInstallMode then Result := 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
  else Result := 'Environment';
end;

function SamePath(A, B: String): Boolean;
begin
  Result := CompareText(RemoveBackslashUnlessRoot(Trim(A)), RemoveBackslashUnlessRoot(Trim(B))) = 0;
end;

function PathWithout(P, Dir: String; var Found: Boolean): String;
var
  Item: String;
  I: Integer;
begin
  Result := '';
  Found := False;
  while P <> '' do
  begin
    I := Pos(';', P);
    if I = 0 then begin Item := P; P := ''; end
    else begin Item := Copy(P, 1, I - 1); Delete(P, 1, I); end;
    if Trim(Item) = '' then Continue;
    if SamePath(Item, Dir) then begin Found := True; Continue; end;
    if Result <> '' then Result := Result + ';';
    Result := Result + Item;
  end;
end;

procedure AddToPath(Dir: String);
var
  P: String;
  Found: Boolean;
begin
  if not RegQueryStringValue(EnvRoot, EnvKey, 'Path', P) then P := '';
  PathWithout(P, Dir, Found);
  if Found then Exit;
  if (P <> '') and (P[Length(P)] <> ';') then P := P + ';';
  RegWriteExpandStringValue(EnvRoot, EnvKey, 'Path', P + Dir);
end;

procedure RemoveFromPath(Dir: String);
var
  P, NewP: String;
  Found: Boolean;
begin
  if not RegQueryStringValue(EnvRoot, EnvKey, 'Path', P) then Exit;
  NewP := PathWithout(P, Dir, Found);
  if Found then RegWriteExpandStringValue(EnvRoot, EnvKey, 'Path', NewP);
end;

{ ---------- files written after install ---------- }
procedure WriteUtf8(FileName, Text: String);
var
  Lines: TArrayOfString;
begin
  SetArrayLength(Lines, 1);
  Lines[0] := Text;
  SaveStringsToUTF8FileWithoutBOM(FileName, Lines, False);
end;

procedure WriteSettings;
var
  Dir, Json, Prov: String;
begin
  if not ConfigureSettings then Exit;
  Dir := ExpandConstant('{userappdata}\NovaForge');
  ForceDirectories(Dir);
  Json := '{' + #13#10 +
    '  "config": {' + #13#10 +
    '    "theme": "' + Part(ThemeIds, ThemeCombo.ItemIndex) + '",' + #13#10 +
    '    "intro": ' + JsonBool(IntroOn) + ',' + #13#10 +
    '    "intro_mode": "' + Part('always|6h|always', IntroCombo.ItemIndex) + '",' + #13#10 +
    '    "animation_speed": ' + Part(SpeedValues, SpeedCombo.ItemIndex) + ',' + #13#10 +
    '    "glyphs": "' + Part(GlyphIds, GlyphCombo.ItemIndex) + '",' + #13#10 +
    '    "permission": { "edit": "' + Part(PermIds, EditCombo.ItemIndex) + '", "bash": "' + Part(PermIds, BashCombo.ItemIndex) +
         '", "webfetch": "' + Part(WebIds, WebCombo.ItemIndex) + '" },' + #13#10 +
    '    "max_steps": ' + Part(StepIds, StepsCombo.ItemIndex) + ',' + #13#10 +
    '    "default_project": "' + JsonEsc(Trim(DefProjectEdit.Text)) + '",' + #13#10 +
    '    "project_instructions": ' + JsonBool(TemplateCheck.Checked) + #13#10 +
    '  }';
  Prov := Part(ProvIds, ProvCombo.ItemIndex);
  if (Prov <> '') and (KeyText <> '') then
    Json := Json + ',' + #13#10 + '  "provider": "' + Prov + '",' + #13#10 + '  "api_key": "' + JsonEsc(KeyText) + '"';
  Json := Json + #13#10 + '}' + #13#10;
  WriteUtf8(Dir + '\installer-settings.json', Json);
end;

procedure WriteTerminalProfile;
var
  Dir, Exe, Json: String;
begin
  Dir := WTFragmentDir('');
  if not WizardIsTaskSelected('wtprofile') then
  begin
    DelTree(Dir, True, True, True);
    Exit;
  end;
  ForceDirectories(Dir);
  Exe := JsonEsc(ExpandConstant('{app}\nova.exe'));
  Json := '{' + #13#10 +
    '  "profiles": [' + #13#10 +
    '    {' + #13#10 +
    '      "guid": "{7c1a3d5e-2b4f-4e8a-9c6d-0f1e2d3c4b5a}",' + #13#10 +
    '      "name": "NovaForge",' + #13#10 +
    '      "commandline": "\"' + Exe + '\"",' + #13#10 +
    '      "icon": "' + Exe + '",' + #13#10 +
    '      "startingDirectory": "' + JsonEsc(GetProjectsDir('')) + '",' + #13#10 +
    '      "colorScheme": "NovaForge Forge",' + #13#10 +
    '      "tabTitle": "NovaForge",' + #13#10 +
    '      "suppressApplicationTitle": true' + #13#10 +
    '    }' + #13#10 +
    '  ],' + #13#10 +
    '  "schemes": [' + #13#10 +
    '    {' + #13#10 +
    '      "name": "NovaForge Forge",' + #13#10 +
    '      "background": "#140907", "foreground": "#EDE3DA", "cursorColor": "#FFB347", "selectionBackground": "#5A2A1C",' + #13#10 +
    '      "black": "#1E120E", "red": "#E5533D", "green": "#9CC069", "yellow": "#F2C14E",' + #13#10 +
    '      "blue": "#7AA2C8", "purple": "#C08AB0", "cyan": "#7FC4B8", "white": "#D8CEC4",' + #13#10 +
    '      "brightBlack": "#6A6460", "brightRed": "#FF6A4D", "brightGreen": "#B5D88A", "brightYellow": "#FFD08A",' + #13#10 +
    '      "brightBlue": "#9EC2E6", "brightPurple": "#DDA8CE", "brightCyan": "#A6E3D8", "brightWhite": "#FFF6D5"' + #13#10 +
    '    }' + #13#10 +
    '  ]' + #13#10 +
    '}' + #13#10;
  WriteUtf8(Dir + '\novaforge.json', Json);
end;

procedure WriteGlobalInstructions;
var
  F: String;
begin
  F := ExpandConstant('{userappdata}\NovaForge\NOVA.md');
  if FileExists(F) or FileExists(ExpandConstant('{userappdata}\NovaForge\AGENTS.md')) then Exit;
  ForceDirectories(ExtractFileDir(F));
  WriteUtf8(F,
    '# Общие инструкции NovaForge' + #13#10 + #13#10 +
    'Этот файл добавляется к запросам во ВСЕХ проектах. Здесь — ваши личные предпочтения;' + #13#10 +
    'правила конкретного проекта пишите в NOVA.md в папке проекта.' + #13#10 + #13#10 +
    '## Обо мне' + #13#10 +
    '- _(кто вы, какой опыт, ОС и редактор)_' + #13#10 + #13#10 +
    '## Как отвечать' + #13#10 +
    '- Отвечай на русском, кратко и по делу.' + #13#10 +
    '- Код и комментарии в коде — на английском.');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    WriteGlobalInstructions;
    if WizardIsTaskSelected('path') then AddToPath(ExpandConstant('{app}'))
    else RemoveFromPath(ExpandConstant('{app}'));
    WriteSettings;
    WriteTerminalProfile;
  end;
end;

{ ---------- uninstall ---------- }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
begin
  if CurUninstallStep = usUninstall then
    RemoveFromPath(ExpandConstant('{app}'));
  if CurUninstallStep = usPostUninstall then
  begin
    DataDir := ExpandConstant('{userappdata}\NovaForge');
    if DirExists(DataDir) and not UninstallSilent then
      if MsgBox('Удалить также настройки, ключи API и историю сессий?' + #13#10 + DataDir + #13#10#13#10 +
                'Папка с проектами не будет затронута.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        DelTree(DataDir, True, True, True);
  end;
end;
