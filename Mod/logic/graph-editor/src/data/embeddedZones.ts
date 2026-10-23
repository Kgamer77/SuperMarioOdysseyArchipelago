import embeddedZonesCsv from '../../../templates/embedded_zones.csv?raw';
import { parseCsvDocument } from '../lib/csv';

export interface EmbeddedZoneRow extends Record<string, string> {
  embedded_stage: string;
  parent_stage: string;
  source_parent_stage: string;
  translation_x: string;
  translation_y: string;
  translation_z: string;
  rotation_x: string;
  rotation_y: string;
  rotation_z: string;
  scale_x: string;
  scale_y: string;
  scale_z: string;
  matrix_00: string;
  matrix_01: string;
  matrix_02: string;
  matrix_03: string;
  matrix_10: string;
  matrix_11: string;
  matrix_12: string;
  matrix_13: string;
  matrix_20: string;
  matrix_21: string;
  matrix_22: string;
  matrix_23: string;
  scenario_indices: string;
  status: string;
  notes: string;
}

export const embeddedZoneRows = parseCsvDocument<EmbeddedZoneRow>(embeddedZonesCsv).rows;

export const embeddedZoneByStage = new Map(
  embeddedZoneRows
    .filter((row) => row.status === 'active' && row.embedded_stage && row.parent_stage)
    .map((row) => [row.embedded_stage, row])
);
