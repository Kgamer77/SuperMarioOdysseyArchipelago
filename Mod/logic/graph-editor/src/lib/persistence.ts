import { cloneWorkspace, exportEditableCsvs, sliceWorkspaceForKingdom } from './workspace';
import type { EditorProject, LogicWorkspace } from './types';

const AUTOSAVE_KEY = 'smo-randomizer.logic-editor.autosave.v2';
const LEGACY_AUTOSAVE_KEY = 'smo-randomizer.logic-editor.autosave.v1';
const CAPTURE_GATE_HEADERS = [
  'gate_id', 'kingdom', 'logic_stage', 'edge_id', 'source_capture_token',
  'replacement_capture_token', 'difficulty', 'match_mode', 'minimum_count', 'notes'
];
const CAPTURE_GATE_OBJECT_HEADERS = ['gate_id', 'placement_stage', 'object_id'];
const PROJECT_FILE_TYPES: ProjectFilePickerType[] = [
  {
    description: 'Logic editor project',
    accept: { 'application/json': ['.json'] }
  }
];

interface ProjectFilePickerType {
  description: string;
  accept: Record<string, string[]>;
}

interface ProjectFileHandle {
  name: string;
  getFile(): Promise<File>;
}

interface ProjectWritableFileHandle {
  name: string;
  createWritable(): Promise<ProjectWritableFileStream>;
}

interface ProjectWritableFileStream {
  write(data: string): Promise<void>;
  close(): Promise<void>;
}

interface FilePickerHost {
  showSaveFilePicker?: (options?: {
    suggestedName?: string;
    types?: ProjectFilePickerType[];
  }) => Promise<ProjectWritableFileHandle>;
  showOpenFilePicker?: (options?: {
    multiple?: boolean;
    types?: ProjectFilePickerType[];
  }) => Promise<ProjectFileHandle[]>;
}

export function makeProject(workspace: LogicWorkspace, kingdom?: string): EditorProject {
  return {
    app: 'smo-randomizer-logic-editor',
    schemaVersion: 2,
    ...(kingdom ? { kingdom } : {}),
    savedAt: new Date().toISOString(),
    workspace: cloneWorkspace(workspace)
  };
}

export function makeKingdomProject(workspace: LogicWorkspace, kingdom: string): EditorProject {
  return makeProject(sliceWorkspaceForKingdom(workspace, kingdom), kingdom);
}

export function parseProject(text: string): EditorProject {
  return migrateProject(JSON.parse(text));
}

function migrateProject(value: unknown): EditorProject {
  const project = value as {
    app?: string;
    schemaVersion?: number;
    kingdom?: string;
    savedAt?: string;
    workspace?: Partial<LogicWorkspace> & { schemaVersion?: number };
  };
  if (project.app !== 'smo-randomizer-logic-editor' || !project.workspace
      || (project.schemaVersion !== 1 && project.schemaVersion !== 2)) {
    throw new Error('This is not a compatible logic editor project file.');
  }
  const workspace = project.workspace;
  const captureGateHeaders = [
    ...(workspace.csvHeaders?.captureGates ?? CAPTURE_GATE_HEADERS)
  ];
  if (!captureGateHeaders.includes('difficulty')) {
    const replacementIndex = captureGateHeaders.indexOf('replacement_capture_token');
    captureGateHeaders.splice(
      replacementIndex >= 0 ? replacementIndex + 1 : captureGateHeaders.length,
      0,
      'difficulty'
    );
  }
  const csvHeaders = {
    ...(workspace.csvHeaders ?? {}),
    captureGates: captureGateHeaders,
    captureGateObjects: workspace.csvHeaders?.captureGateObjects ?? CAPTURE_GATE_OBJECT_HEADERS
  } as LogicWorkspace['csvHeaders'];
  return {
    app: 'smo-randomizer-logic-editor',
    schemaVersion: 2,
    ...(project.kingdom ? { kingdom: project.kingdom } : {}),
    savedAt: project.savedAt ?? new Date().toISOString(),
    workspace: {
      ...workspace,
      schemaVersion: 2,
      csvHeaders,
      captureGates: (workspace.captureGates ?? []).map((gate) => ({
        ...gate,
        difficulty: gate.difficulty || 'casual'
      })),
      captureGateObjects: workspace.captureGateObjects ?? []
    } as LogicWorkspace
  };
}

export function saveAutosave(workspace: LogicWorkspace): void {
  window.localStorage.setItem(AUTOSAVE_KEY, JSON.stringify(makeProject(workspace)));
}

export function loadAutosave(): EditorProject | undefined {
  const current = window.localStorage.getItem(AUTOSAVE_KEY);
  if (current) {
    return parseProject(current);
  }
  const legacy = window.localStorage.getItem(LEGACY_AUTOSAVE_KEY);
  if (!legacy) {
    return undefined;
  }
  const migrated = parseProject(legacy);
  window.localStorage.setItem(AUTOSAVE_KEY, JSON.stringify(migrated));
  window.localStorage.removeItem(LEGACY_AUTOSAVE_KEY);
  return migrated;
}

export function clearAutosave(): void {
  window.localStorage.removeItem(AUTOSAVE_KEY);
  window.localStorage.removeItem(LEGACY_AUTOSAVE_KEY);
}

export function downloadProject(workspace: LogicWorkspace): void {
  downloadTextFile('logic-editor-project.json', JSON.stringify(makeProject(workspace), null, 2), 'application/json');
}

export async function saveProjectAs(workspace: LogicWorkspace): Promise<string> {
  const text = JSON.stringify(makeProject(workspace), null, 2);
  const pickerHost = window as unknown as FilePickerHost;

  if (pickerHost.showSaveFilePicker) {
    const handle = await pickerHost.showSaveFilePicker({
      suggestedName: defaultProjectFileName(),
      types: PROJECT_FILE_TYPES
    });
    const writable = await handle.createWritable();
    await writable.write(text);
    await writable.close();
    return handle.name;
  }

  const fileName = defaultProjectFileName();
  downloadTextFile(fileName, text, 'application/json');
  return fileName;
}

export async function loadProjectFrom(): Promise<EditorProject> {
  const pickerHost = window as unknown as FilePickerHost;

  if (pickerHost.showOpenFilePicker) {
    const [handle] = await pickerHost.showOpenFilePicker({
      multiple: false,
      types: PROJECT_FILE_TYPES
    });
    if (!handle) {
      throw new DOMException('File selection canceled.', 'AbortError');
    }
    return parseProject(await (await handle.getFile()).text());
  }

  return loadProjectFromInput();
}

export function downloadEditableCsvs(workspace: LogicWorkspace): void {
  const files = exportEditableCsvs(workspace);
  for (const [fileName, text] of Object.entries(files)) {
    downloadTextFile(fileName, text, 'text/csv');
  }
}

export async function saveKingdomSnapshot(workspace: LogicWorkspace, kingdom: string): Promise<string> {
  if (!kingdom) {
    throw new Error('Choose a kingdom before saving.');
  }

  const response = await fetch('/api/logic-editor/save', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      kingdom,
      project: makeKingdomProject(workspace, kingdom)
    })
  });

  const body = (await response.json()) as { ok: boolean; error?: string; projectPath?: string };
  if (!response.ok || !body.ok) {
    throw new Error(body.error || 'Could not save kingdom layout.');
  }

  return body.projectPath || 'saves/kingdoms';
}

export async function loadKingdomSnapshot(kingdom: string): Promise<EditorProject> {
  if (!kingdom) {
    throw new Error('Choose a kingdom before loading.');
  }

  const response = await fetch(`/api/logic-editor/load?kingdom=${encodeURIComponent(kingdom)}`);
  const body = (await response.json()) as { ok: boolean; project?: EditorProject; error?: string };
  if (!response.ok || !body.ok || !body.project) {
    throw new Error(body.error || 'Could not load kingdom layout.');
  }

  return migrateProject(body.project);
}

function downloadTextFile(fileName: string, text: string, mimeType: string): void {
  const blob = new Blob([text], { type: `${mimeType};charset=utf-8` });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = fileName;
  link.click();
  URL.revokeObjectURL(url);
}

function defaultProjectFileName(): string {
  const stamp = new Date().toISOString().slice(0, 19).replace(/:/g, '-');
  return `logic-editor-project-${stamp}.json`;
}

function loadProjectFromInput(): Promise<EditorProject> {
  return new Promise((resolve, reject) => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = 'application/json,.json';
    input.style.display = 'none';

    const cleanup = () => input.remove();

    input.addEventListener(
      'change',
      async () => {
        const file = input.files?.[0];
        cleanup();
        if (!file) {
          reject(new DOMException('File selection canceled.', 'AbortError'));
          return;
        }

        try {
          resolve(parseProject(await file.text()));
        } catch (error) {
          reject(error);
        }
      },
      { once: true }
    );

    input.addEventListener(
      'cancel',
      () => {
        cleanup();
        reject(new DOMException('File selection canceled.', 'AbortError'));
      },
      { once: true }
    );

    document.body.appendChild(input);
    input.click();
  });
}
