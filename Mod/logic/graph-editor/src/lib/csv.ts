import type { CsvDocument, CsvRow } from './types';

export function parseCsvDocument<T extends CsvRow = CsvRow>(text: string): CsvDocument<T> {
  if (text.charCodeAt(0) === 0xFEFF) {
    text = text.slice(1);
  }
  const rows: string[][] = [];
  let row: string[] = [];
  let field = '';
  let inQuotes = false;

  for (let index = 0; index < text.length; index += 1) {
    const char = text[index];
    const next = text[index + 1];

    if (inQuotes) {
      if (char === '"' && next === '"') {
        field += '"';
        index += 1;
      } else if (char === '"') {
        inQuotes = false;
      } else {
        field += char;
      }
      continue;
    }

    if (char === '"') {
      inQuotes = true;
      continue;
    }

    if (char === ',') {
      row.push(field);
      field = '';
      continue;
    }

    if (char === '\n') {
      row.push(field);
      rows.push(row);
      row = [];
      field = '';
      continue;
    }

    if (char === '\r') {
      continue;
    }

    field += char;
  }

  if (field.length > 0 || row.length > 0) {
    row.push(field);
    rows.push(row);
  }

  const nonEmptyRows = rows.filter((candidate) => candidate.some((cell) => cell.length > 0));
  if (nonEmptyRows.length === 0) {
    return { headers: [], rows: [] };
  }

  const headers = nonEmptyRows[0];
  const records = nonEmptyRows.slice(1).map((cells) => {
    const record: CsvRow = {};
    headers.forEach((header, index) => {
      record[header] = cells[index] ?? '';
    });
    return record as T;
  });

  return { headers, rows: records };
}

export function stringifyCsv<T extends CsvRow>(rows: T[], headers: string[]): string {
  const lines = [
    headers.map(escapeCsvField).join(','),
    ...rows.map((row) => headers.map((header) => escapeCsvField(row[header] ?? '')).join(','))
  ];

  return `${lines.join('\n')}\n`;
}

export function cloneRows<T extends CsvRow>(rows: T[]): T[] {
  return rows.map((row) => ({ ...row }));
}

function escapeCsvField(value: string): string {
  if (!/[",\r\n]/.test(value)) {
    return value;
  }

  return `"${value.replace(/"/g, '""')}"`;
}
