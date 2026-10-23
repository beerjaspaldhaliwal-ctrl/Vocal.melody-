; Inno Setup script: installs MelodyLift.vst3 into the standard VST3 folder,
; where FL Studio, Ableton, Cubase, Reaper etc. find it automatically.
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build\MelodyLift_artefacts\Release"
#endif
#ifndef GenSourceDir
  #define GenSourceDir "..\..\build\MelodyLiftGen_artefacts\Release"
#endif

[Setup]
AppId={{6C1E7C63-2F7B-4D8E-9E3A-6A1F0B7D4C21}
AppName=MelodyLift
AppVersion={#AppVersion}
AppPublisher=MelodyLift
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
UninstallFilesDir={commonpf64}\MelodyLift
OutputBaseFilename=MelodyLift-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern

[Files]
Source: "{#SourceDir}\VST3\MelodyLift.vst3\*"; DestDir: "{commoncf64}\VST3\MelodyLift.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#GenSourceDir}\VST3\MelodyLift Generator.vst3\*"; DestDir: "{commoncf64}\VST3\MelodyLift Generator.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\Standalone\MelodyLift.exe"; DestDir: "{commonpf64}\MelodyLift"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\MelodyLift (standalone)"; Filename: "{commonpf64}\MelodyLift\MelodyLift.exe"

[Messages]
FinishedLabel=MelodyLift is installed in your VST3 folder.%n%nIn FL Studio open Options > Manage plugins and click Find installed plugins.%n%nMelodyLift Generator: add it to the Channel Rack to write hooks, chords and basslines.%nMelodyLift: add it to your vocal's mixer insert to turn the vocal into MIDI.
