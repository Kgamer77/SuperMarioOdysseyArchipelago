import { describe, expect, it } from 'vitest';
import { createDefaultWorkspace } from '../data/defaultData';
import { parseProject } from '../lib/persistence';
import { cloneWorkspace } from '../lib/workspace';

describe('logic editor project persistence', () => {
  it('migrates a version-1 project without changing existing graph rows or headers', () => {
    const current = cloneWorkspace(createDefaultWorkspace());
    const legacyWorkspace = {
      ...current,
      schemaVersion: 1,
      csvHeaders: { ...current.csvHeaders }
    } as Record<string, unknown>;
    const legacyHeaders = legacyWorkspace.csvHeaders as Record<string, unknown>;
    delete legacyWorkspace.captureGates;
    delete legacyWorkspace.captureGateObjects;
    delete legacyHeaders.captureGates;
    delete legacyHeaders.captureGateObjects;
    const project = parseProject(JSON.stringify({
      app: 'smo-randomizer-logic-editor',
      schemaVersion: 1,
      savedAt: '2025-01-01T00:00:00.000Z',
      workspace: legacyWorkspace
    }));

    expect(project.schemaVersion).toBe(2);
    expect(project.workspace.schemaVersion).toBe(2);
    expect(project.workspace.logicNodes).toEqual(current.logicNodes);
    expect(project.workspace.routeEdges).toEqual(current.routeEdges);
    expect(project.workspace.moonAccess).toEqual(current.moonAccess);
    expect(project.workspace.captureGates).toEqual([]);
    expect(project.workspace.captureGateObjects).toEqual([]);
    expect(project.workspace.csvHeaders.captureGates).toContain('gate_id');
    expect(project.workspace.csvHeaders.captureGateObjects).toEqual([
      'gate_id', 'placement_stage', 'object_id'
    ]);
  });
});
