export function parseOptionScenarios(value: unknown): string[][] {
  const raw = String(value ?? '').trim();
  if (!raw) {
    return [];
  }

  return raw.split(';').map((option) => {
    const items = option
      .split(',')
      .map((item) => item.trim())
      .filter(Boolean);
    return items;
  });
}

export function scenariosForOption(
  value: unknown,
  optionIndex: number,
  availableScenarios: string[]
): string[] {
  const parts = parseOptionScenarios(value);
  if (parts.length === 0) {
    return [...availableScenarios];
  }

  if (parts.length === 1) {
    return parts[0].length > 0 ? parts[0] : [...availableScenarios];
  }

  const entry = parts[optionIndex];
  if (entry && entry.length > 0) {
    return entry;
  }

  return [...availableScenarios];
}

export function isScenarioChecked(
  value: unknown,
  optionIndex: number,
  scenario: string,
  availableScenarios: string[]
): boolean {
  const scenarios = scenariosForOption(value, optionIndex, availableScenarios);
  return scenarios.includes(scenario);
}

export function stringifyOptionScenarios(
  perOption: string[][],
  availableScenarios: string[]
): string {
  if (perOption.length === 0) {
    return '';
  }

  const normalized = perOption.map((option) => {
    const sorted = [...option].filter(Boolean);
    if (sorted.length === availableScenarios.length && availableScenarios.every((s) => sorted.includes(s))) {
      return [...availableScenarios];
    }
    return sorted;
  });

  if (normalized.length === 1) {
    const option = normalized[0];
    if (option.length === availableScenarios.length && availableScenarios.every((s) => option.includes(s))) {
      return availableScenarios.join(',');
    }
    return option.join(',');
  }

  const allSame =
    normalized.length > 1 &&
    normalized.every((option) => {
      if (option.length !== normalized[0].length) return false;
      return normalized[0].every((s) => option.includes(s));
    });

  if (allSame) {
    const option = normalized[0];
    if (option.length === availableScenarios.length && availableScenarios.every((s) => option.includes(s))) {
      return availableScenarios.join(',');
    }
    return option.join(',');
  }

  return normalized.map((option) => option.join(',')).join(';');
}