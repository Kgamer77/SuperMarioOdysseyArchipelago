export interface RouteDifficultyOption {
  value: string;
  label: string;
  description: string;
}

export const ROUTE_DIFFICULTY_LEVELS: RouteDifficultyOption[] = [
  {
    value: 'casual',
    label: 'Casual',
    description: 'Expected for normal play and basic movement.'
  },
  {
    value: 'advanced',
    label: 'Advanced',
    description: 'Requires intentional movement skill, setup, or tighter routing.'
  },
  {
    value: 'expert',
    label: 'Expert',
    description: 'Reserved for demanding tricks or highly precise transitions.'
  },
  {
    value: 'glitched',
    label: 'Glitched',
    description: 'Requires glitches or unintended engine behaviour on top of Expert movement.'
  }
];

export const ROUTE_DIFFICULTY_VALUES = new Set(ROUTE_DIFFICULTY_LEVELS.map((level) => level.value));

export function normalizeRouteDifficulty(value: unknown): string {
  const normalized = String(value ?? '').trim().toLowerCase();
  if (ROUTE_DIFFICULTY_VALUES.has(normalized)) {
    return normalized;
  }

  if (normalized === 'easy' || normalized === 'low') {
    return 'casual';
  }

  if (normalized === 'medium' || normalized === 'mid') {
    return 'advanced';
  }

  if (normalized === 'hard') {
    return 'expert';
  }

  if (normalized === 'glitch' || normalized === 'glitches') {
    return 'glitched';
  }

  return normalized || 'casual';
}

export function parseRouteDifficulties(value: unknown): string[] {
  return String(value ?? '')
    .split(';')
    .map((part) => normalizeRouteDifficulty(part))
    .filter(Boolean);
}

export function normalizeRouteDifficulties(value: unknown, optionCount: number): string[] {
  const count = Math.max(1, optionCount);
  const parts = parseRouteDifficulties(value);
  if (parts.length === 0) {
    return Array.from({ length: count }, () => 'casual');
  }

  if (parts.length === 1) {
    return Array.from({ length: count }, () => parts[0]);
  }

  return Array.from({ length: count }, (_, index) => parts[index] ?? 'casual');
}

export function routeDifficultyForOption(value: unknown, optionIndex: number): string {
  const parts = parseRouteDifficulties(value);
  if (parts.length === 0) {
    return 'casual';
  }
  if (parts.length === 1) {
    return parts[0];
  }
  return parts[optionIndex] ?? 'casual';
}

export function stringifyRouteDifficulties(values: unknown[]): string {
  const normalized = values.length > 0 ? values.map((value) => normalizeRouteDifficulty(value)) : ['casual'];
  return normalized.every((value) => value === normalized[0])
    ? normalized[0]
    : normalized.join(';');
}

export function routeDifficultyLabel(value: unknown): string {
  const normalized = normalizeRouteDifficulty(value);
  return ROUTE_DIFFICULTY_LEVELS.find((level) => level.value === normalized)?.label ?? normalized;
}
