export interface EffortLevelOption {
  value: string;
  label: string;
  description: string;
}

export const EFFORT_LEVELS: EffortLevelOption[] = [
  {
    value: 'unrated',
    label: 'Unrated',
    description: 'Needs human classification before effort weighting is meaningful.'
  },
  {
    value: 'low',
    label: 'Low',
    description: 'Quick pickup, small detour, or little waiting once the node is reachable.'
  },
  {
    value: 'mid',
    label: 'Mid',
    description: 'Noticeable detour, setup, travel, or repeated interaction.'
  },
  {
    value: 'high',
    label: 'High',
    description: 'Slow, boring, out-of-the-way, or time-consuming even when logically available.'
  }
];

export const EFFORT_LEVEL_VALUES = new Set(EFFORT_LEVELS.map((level) => level.value));

export function normalizeEffortLevel(value: string): string {
  if (EFFORT_LEVEL_VALUES.has(value)) {
    return value;
  }

  if (value === 'trivial' || value === 'easy') {
    return 'low';
  }

  if (value === 'medium') {
    return 'mid';
  }

  if (value === 'hard' || value === 'expert') {
    return 'high';
  }

  return value || 'unrated';
}

export function effortLabel(value: string): string {
  const normalized = normalizeEffortLevel(value);
  return EFFORT_LEVELS.find((level) => level.value === normalized)?.label ?? normalized;
}
